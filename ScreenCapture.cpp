#include "ScreenCapture.h"

#include <immintrin.h>
#include <comdef.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace
{
    // RAII: ReleaseFrame даже при исключении — иначе дупликация «зависает».
    struct ReleaseGuard
    {
        IDXGIOutputDuplication* dup;
        bool* held;
        ~ReleaseGuard()
        {
            if (*held) { dup->ReleaseFrame(); *held = false; }
        }
    };
}

ScreenCapture::ScreenCapture(Config cfg) : m_cfg(cfg) {}

ScreenCapture::~ScreenCapture()
{
    if (m_frameHeld && m_dup) m_dup->ReleaseFrame();
}

void ScreenCapture::Check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
        throw std::runtime_error(std::string(what) + ": " + HrToString(hr));
}

void ScreenCapture::Initialize()
{
    CreateDevice();
    CreateDuplication();
    CreateStaging();
}

void ScreenCapture::CreateDevice()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                            levels, _countof(levels), D3D11_SDK_VERSION,
                            &m_device, nullptr, &m_context),
          "D3D11CreateDevice");
}

void ScreenCapture::CreateDuplication()
{
    ComPtr<IDXGIDevice>  dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIOutput>  output;
    Check(m_device.As(&dxgiDev), "device.As<IDXGIDevice>");
    Check(dxgiDev->GetParent(__uuidof(IDXGIAdapter),
                             reinterpret_cast<void**>(adapter.GetAddressOf())),
          "GetParent(IDXGIAdapter)");
    Check(adapter->EnumOutputs(0, &output), "EnumOutputs"); // 0 = primary

    DXGI_OUTPUT_DESC desc{};
    Check(output->GetDesc(&desc), "GetDesc");
    ComputeFovRect(desc.DesktopCoordinates);

    ComPtr<IDXGIOutput1> output1;
    Check(output.As(&output1), "output.As<IDXGIOutput1>");
    Check(output1->DuplicateOutput(m_device.Get(), &m_dup), "DuplicateOutput");
}

void ScreenCapture::ComputeFovRect(const RECT& desk)
{
    m_deskRect = desk;
    const LONG w = desk.right - desk.left;   // физический размер монитора
    const LONG h = desk.bottom - desk.top;

    m_fovW = std::min<UINT>(m_cfg.fovWidth,  static_cast<UINT>(w));
    m_fovH = std::min<UINT>(m_cfg.fovHeight, static_cast<UINT>(h));

    // Кроп строго по центру МОНИТОРА, в его относительных координатах
    // (0..w, 0..h). Кадр DuplicateOutput покрывает именно этот диапазон,
    // поэтому SrcBox = m_cropRect без каких-либо смещений десктопа.
    const LONG left = (w - static_cast<LONG>(m_fovW)) / 2;
    const LONG top  = (h - static_cast<LONG>(m_fovH)) / 2;
    m_cropRect.left   = left;
    m_cropRect.top    = top;
    m_cropRect.right  = left + static_cast<LONG>(m_fovW);
    m_cropRect.bottom = top  + static_cast<LONG>(m_fovH);
}

void ScreenCapture::CreateStaging()
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width            = m_fovW;
    td.Height           = m_fovH;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_STAGING;
    td.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

    m_staging.Reset();
    Check(m_device->CreateTexture2D(&td, nullptr, &m_staging),
          "CreateTexture2D(staging)");

    const UINT ch = m_cfg.convertToBGR ? 3 : 4;
    m_buffer.resize(static_cast<size_t>(m_fovW) * m_fovH * ch);
    m_frame = Frame{}; // инвалидировать старый буфер после ресайза
}

void ScreenCapture::SetFovSize(UINT w, UINT h)
{
    if (!m_cfg.dynamicFov) return;
    m_cfg.fovWidth  = w;
    m_cfg.fovHeight = h;
    ComputeFovRect(m_deskRect);   // пересчитать центр на сохранённом десктопе
    CreateStaging();
}

bool ScreenCapture::Capture()
{
    m_frame = Frame{}; // инвалидируем, чтобы случайно не отдать стейл

    for (;;)
    {
        ComPtr<IDXGIResource> resource;
        DXGI_OUTDUPL_FRAME_INFO info{};

        HRESULT hr = m_dup->AcquireNextFrame(m_cfg.frameTimeoutMs,
                                             &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return false;                      // экран не изменился — GPU спит

        if (hr == DXGI_ERROR_ACCESS_LOST)
        {
            // Смена разрешения / Win+L / UAC / Ctrl+Alt+Del.
            DestroyDuplication();
            Sleep(50);
            CreateDuplication();
            CreateStaging();
            continue;
        }
        Check(hr, "AcquireNextFrame");

        m_frameHeld = true;
        ReleaseGuard guard{ m_dup.Get(), &m_frameHeld };

        if (info.LastPresentTime.QuadPart == 0)
            return false;                      // «грязный» буфер, новой картинки нет

        ComPtr<ID3D11Texture2D> frameTex;
        Check(resource->QueryInterface(__uuidof(ID3D11Texture2D),
              reinterpret_cast<void**>(frameTex.GetAddressOf())),
              "QI ID3D11Texture2D");

        return ProcessFrame(frameTex.Get(), info.LastPresentTime.QuadPart);
    }
}

bool ScreenCapture::ProcessFrame(ID3D11Texture2D* frameTex, INT64 qpc)
{
    // --- GPU-кроп: только FOV из полного кадра. ---
    // m_cropRect уже задан в координатах монитора (0..w, 0..h), и кадр
    // дупликации покрывает ровно этот монитор — SrcBox берём как есть,
    // без смещения десктопа (раньше тут и был баг с захватом угла).
    D3D11_TEXTURE2D_DESC fd{};
    frameTex->GetDesc(&fd);

    D3D11_BOX box{};
    box.left   = std::min<UINT>(static_cast<UINT>(m_cropRect.left),  fd.Width);
    box.top    = std::min<UINT>(static_cast<UINT>(m_cropRect.top),   fd.Height);
    box.right  = std::min<UINT>(static_cast<UINT>(m_cropRect.right), fd.Width);
    box.bottom = std::min<UINT>(static_cast<UINT>(m_cropRect.bottom), fd.Height);
    box.back   = 1;
    box.front  = 0;

    if (box.right - box.left != m_fovW || box.bottom - box.top != m_fovH)
        return false;                          // FOV вышел за границы кадра

    m_context->CopySubresourceRegion(m_staging.Get(), 0, 0, 0, 0,
                                     frameTex, 0, &box);

    // --- Маппинг VRAM -> RAM ---
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
          "Map(staging)");

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t*       dst = m_buffer.data();

    if (m_cfg.convertToBGR)
    {
        BgraToBgr_SSSE3(src, mapped.RowPitch, dst, m_fovW, m_fovH);
    }
    else
    {
        const size_t rowBytes = static_cast<size_t>(m_fovW) * 4;
        if (mapped.RowPitch == rowBytes)
            memcpy(dst, src, rowBytes * m_fovH);
        else
            for (UINT y = 0; y < m_fovH; ++y)
                memcpy(dst + y * rowBytes, src + y * mapped.RowPitch, rowBytes);
    }

    m_context->Unmap(m_staging.Get(), 0);

    const UINT ch = m_cfg.convertToBGR ? 3 : 4;
    m_frame.data        = m_buffer.data();
    m_frame.width       = m_fovW;
    m_frame.height      = m_fovH;
    m_frame.channels    = ch;
    m_frame.stride      = static_cast<size_t>(m_fovW) * ch;
    m_frame.sizeBytes   = m_buffer.size();
    m_frame.timestampQpc = qpc;
    return true;
}

// BGRA -> BGR: 4 пикселя (16 байт) -> 12 байт за один pshufb.
void ScreenCapture::BgraToBgr_SSSE3(const uint8_t* src, size_t srcPitch,
                                    uint8_t* dst, UINT w, UINT h)
{
    const __m128i mask = _mm_setr_epi8(
        0,1,2, 4,5,6, 8,9,10, 12,13,14, -1,-1,-1,-1);

    for (UINT y = 0; y < h; ++y)
    {
        const uint8_t* s = src + static_cast<size_t>(y) * srcPitch;
        uint8_t*       d = dst + static_cast<size_t>(y) * w * 3;

        UINT x = 0;
        for (; x + 4 <= w; x += 4)
        {
            __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + x*4));
            px = _mm_shuffle_epi8(px, mask);
            if (x + 4 < w)
                _mm_storeu_si128(reinterpret_cast<__m128i*>(d + x*3), px);
            else
            {
                alignas(16) uint8_t tmp[16];
                _mm_store_si128(reinterpret_cast<__m128i*>(tmp), px);
                memcpy(d + x*3, tmp, 12);   // не выходим за строку
            }
        }
        for (; x < w; ++x)                   // хвост
        {
            d[x*3+0] = s[x*4+0];
            d[x*3+1] = s[x*4+1];
            d[x*3+2] = s[x*4+2];
        }
    }
}

void ScreenCapture::DestroyDuplication()
{
    if (m_frameHeld && m_dup) { m_dup->ReleaseFrame(); m_frameHeld = false; }
    m_dup.Reset();
}

std::string ScreenCapture::HrToString(HRESULT hr)
{
    _com_error err(hr);
    return std::to_string(static_cast<unsigned long>(hr)) + " (" +
           std::string(err.ErrorMessage()) + ")";
}
