#include "Overlay.h"

#include <cwchar>

using Microsoft::WRL::ComPtr;

namespace
{
    const wchar_t* kClassName = L"BionicAimOverlayClass";

    inline D2D1_COLOR_F ToD2D(COLORREF c, uint8_t alpha)
    {
        return D2D1_COLOR_F{
            static_cast<float>(GetRValue(c)) / 255.0f,
            static_cast<float>(GetGValue(c)) / 255.0f,
            static_cast<float>(GetBValue(c)) / 255.0f,
            static_cast<float>(alpha)        / 255.0f
        };
    }
}

Overlay::~Overlay()
{
    Destroy();
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Overlay::Create(HINSTANCE hInst, const RECT& screenRect, const wchar_t* title)
{
    m_hInst = hInst;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    m_width  = screenRect.right  - screenRect.left;
    m_height = screenRect.bottom - screenRect.top;

    m_hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOPMOST |
        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kClassName, title, WS_POPUP,
        screenRect.left, screenRect.top, m_width, m_height,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd)
        return false;

    if (!InitDirect2D())
    {
        Destroy();
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    return true;
}

bool Overlay::InitDirect2D()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 levels, _countof(levels), D3D11_SDK_VERSION,
                                 &m_d3dDevice, nullptr, &m_d3dCtx)))
    {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_d3dDevice.As(&dxgiDevice)))
        return false;

    ComPtr<IDXGIDevice1> dxgiDevice1;
    if (SUCCEEDED(dxgiDevice.As(&dxgiDevice1)))
        dxgiDevice1->SetMaximumFrameLatency(1);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory1), nullptr,
                                 reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()))))
        return false;

    if (FAILED(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice)))
        return false;

    if (FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dCtx)))
        return false;

    // 1 единица D2D = 1 физический пиксель независимо от масштаба Windows.
    m_d2dCtx->SetDpi(96.0f, 96.0f);
    m_d2dCtx->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
    m_d2dCtx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(dxgiDevice->GetAdapter(&adapter)))
        return false;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                                  reinterpret_cast<void**>(dxgiFactory.GetAddressOf()))))
        return false;

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width            = static_cast<UINT>(m_width);
    sd.Height           = static_cast<UINT>(m_height);
    sd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.Stereo           = FALSE;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount      = 2;
    sd.Scaling          = DXGI_SCALING_STRETCH;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;

    if (FAILED(dxgiFactory->CreateSwapChainForComposition(
            m_d3dDevice.Get(), &sd, nullptr, &m_swapChain)))
        return false;

    ComPtr<IDXGISurface> surface;
    if (FAILED(m_swapChain->GetBuffer(0, __uuidof(IDXGISurface),
                                      reinterpret_cast<void**>(surface.GetAddressOf()))))
        return false;

    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX                  = 96.0f;
    bp.dpiY                  = 96.0f;
    bp.bitmapOptions         = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    if (FAILED(m_d2dCtx->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &m_targetBitmap)))
        return false;

    m_d2dCtx->SetTarget(m_targetBitmap.Get());

    if (FAILED(m_d2dCtx->CreateSolidColorBrush(D2D1_COLOR_F{ 1.f, 1.f, 1.f, 1.f }, &m_brush)))
        return false;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()))))
        return false;

    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                                        reinterpret_cast<void**>(m_dcompDevice.GetAddressOf()))))
        return false;

    if (FAILED(m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget)))
        return false;

    if (FAILED(m_dcompDevice->CreateVisual(&m_dcompVisual)))
        return false;

    if (FAILED(m_dcompVisual->SetContent(m_swapChain.Get())))
        return false;

    if (FAILED(m_dcompTarget->SetRoot(m_dcompVisual.Get())))
        return false;

    if (FAILED(m_dcompDevice->Commit()))
        return false;

    return true;
}

void Overlay::Destroy()
{
    m_textFormat.Reset();
    m_brush.Reset();
    m_targetBitmap.Reset();
    if (m_d2dCtx) m_d2dCtx->SetTarget(nullptr);
    m_d2dCtx.Reset();
    m_d2dDevice.Reset();
    m_d2dFactory.Reset();
    m_dwrite.Reset();
    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();
    m_swapChain.Reset();
    m_d3dCtx.Reset();
    m_d3dDevice.Reset();

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void Overlay::BeginDraw()
{
    if (!m_d2dCtx) return;
    m_d2dCtx->BeginDraw();
    m_d2dCtx->Clear(D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 0.0f });
}

void Overlay::EndDraw()
{
    if (!m_d2dCtx || !m_swapChain) return;
    m_d2dCtx->EndDraw();
    m_swapChain->Present(0, 0);
}

void Overlay::DrawRect(int x, int y, int w, int h, COLORREF color, int thickness, uint8_t alpha)
{
    if (!m_d2dCtx || w <= 0 || h <= 0) return;
    const float t    = static_cast<float>(thickness < 1 ? 1 : thickness);
    const float half = t * 0.5f;
    m_brush->SetColor(ToD2D(color, alpha));
    m_d2dCtx->DrawRectangle(
        D2D1_RECT_F{
            static_cast<float>(x) + half,
            static_cast<float>(y) + half,
            static_cast<float>(x + w) - half,
            static_cast<float>(y + h) - half
        },
        m_brush.Get(), t);
}

void Overlay::DrawFilledRect(int x, int y, int w, int h, COLORREF color, uint8_t alpha)
{
    if (!m_d2dCtx || w <= 0 || h <= 0) return;
    m_brush->SetColor(ToD2D(color, alpha));
    m_d2dCtx->FillRectangle(
        D2D1_RECT_F{
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(x + w),
            static_cast<float>(y + h)
        },
        m_brush.Get());
}

void Overlay::DrawLine(int x1, int y1, int x2, int y2, COLORREF color, int thickness, uint8_t alpha)
{
    if (!m_d2dCtx) return;
    const float t = static_cast<float>(thickness < 1 ? 1 : thickness);
    m_brush->SetColor(ToD2D(color, alpha));
    m_d2dCtx->DrawLine(
        D2D1_POINT_2F{ static_cast<float>(x1), static_cast<float>(y1) },
        D2D1_POINT_2F{ static_cast<float>(x2), static_cast<float>(y2) },
        m_brush.Get(), t);
}

void Overlay::DrawText(int x, int y, const wchar_t* text, int height, COLORREF color, bool bold, uint8_t alpha)
{
    if (!m_d2dCtx || !m_dwrite || !text) return;

    if (!m_textFormat || m_txtH != height || m_txtBold != bold)
    {
        m_textFormat.Reset();
        m_dwrite->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(height) * 0.90f,
            L"",
            &m_textFormat);
        m_txtH    = height;
        m_txtBold = bold;
    }
    if (!m_textFormat) return;

    m_brush->SetColor(ToD2D(color, alpha));
#pragma push_macro("DrawText")
#undef DrawText
    m_d2dCtx->DrawText(
        text,
        static_cast<UINT32>(wcslen(text)),
        m_textFormat.Get(),
        D2D1_RECT_F{
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(m_width),
            static_cast<float>(m_height)
        },
        m_brush.Get());
#pragma pop_macro("DrawText")
}
