#pragma once

// ============================================================================
//  ScreenCapture — сверхбыстрый захват центральной зоны (FOV) экрана
//  через Desktop Duplication API (DXGI 1.2+), для реального времени
//  компьютерного зрения + YOLO (ONNX Runtime / OpenCV).
//
//  Ключевые свойства:
//   * Кроп 640x640 строго по центру основного дисплея, на GPU
//     (CopySubresourceRegion) — в системную память уходит только FOV,
//     а не весь рабочий стол.
//   * Нативный BGRA8 из видеопамяти; cv::Mat оборачивает буфер без копий,
//     опционально SSSE3-конвертация BGRA->BGR для YOLO-препроцессинга.
//   * Корректная работа в вечном потоке: таймауты AcquireNextFrame,
//     RAII ReleaseFrame, авто-пересоздание при DXGI_ERROR_ACCESS_LOST,
//     без аллокаций в hot path и без утечек (ComPtr).
//
//  Не потокобезопасен сам по себе (данные одной текстуры) — предполагается
//  владение одним выделенным потоком захвата (см. main.cpp).
// ============================================================================

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>

class ScreenCapture
{
public:
    struct Config
    {
        UINT  fovWidth      = 640;   // ширина FOV (по центру экрана)
        UINT  fovHeight     = 640;   // высота FOV
        UINT  frameTimeoutMs = 8;    // таймаут AcquireNextFrame (0 = polling)
        bool  convertToBGR  = true;  // true -> BGR(CV_8UC3); false -> BGRA(CV_8UC4)

        // Динамический FOV: если true, размер можно менять «на лету» через
        // SetFovSize() (пересоздаёт staging-текстуру за O(1) в отдельном
        // синхронизированном вызове, а не в горячем цикле).
        bool  dynamicFov    = false;
    };

    // Снимок данных последнего кадра. Валиден до следующего Capture().
    struct Frame
    {
        const uint8_t* data    = nullptr; // плотный буфер BGR|BGRA, без pitch
        UINT   width   = 0;
        UINT   height  = 0;
        UINT   channels= 0;               // 3 или 4
        size_t stride  = 0;               // байт/строка (плотная)
        size_t sizeBytes = 0;
        INT64  timestampQpc = 0;
    };

    explicit ScreenCapture(Config cfg = {});
    ~ScreenCapture();

    ScreenCapture(const ScreenCapture&)            = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Инициализация D3D11 device + DuplicateOutput + staging-текстуры.
    void Initialize();

    // Захват одного кадра FOV.
    //  true  -> свежий кадр в GetFrame()
    //  false -> таймаут (экран не менялся) либо ACCESS_LOST (пересоздано).
    bool Capture();

    const Frame& GetFrame() const noexcept { return m_frame; }

    // Динамическое изменение FOV (только если dynamicFov=true).
    void SetFovSize(UINT w, UINT h);

    UINT FovWidth()  const noexcept { return m_fovW; }
    UINT FovHeight() const noexcept { return m_fovH; }
    bool IsBGR()     const noexcept { return m_cfg.convertToBGR; }

    // Кроп относительно самого монитора (0..w, 0..h) — то, что идёт в SrcBox.
    const RECT& CropRect() const noexcept { return m_cropRect; }

    // Тот же кроп, но в координатах виртуального десктопа (для привязки aim'а).
    RECT DesktopCropRect() const noexcept
    {
        RECT r = m_cropRect;
        r.left   += m_deskRect.left;
        r.right  += m_deskRect.left;
        r.top    += m_deskRect.top;
        r.bottom += m_deskRect.top;
        return r;
    }

    // Полный прямоугольник монитора в координатах десктопа (для оверлея).
    RECT DesktopRect() const noexcept { return m_deskRect; }

    static std::string HrToString(HRESULT hr);

private:
    void CreateDevice();
    void CreateDuplication();
    void CreateStaging();
    void ComputeFovRect(const RECT& desktopCoords);
    void DestroyDuplication();
    bool ProcessFrame(ID3D11Texture2D* frameTex, INT64 qpc);
    void BgraToBgr_SSSE3(const uint8_t* src, size_t srcPitch,
                         uint8_t* dst, UINT w, UINT h);
    static void Check(HRESULT hr, const char* what);

private:
    Config m_cfg;

    Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_dup;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_staging;

    RECT m_deskRect{};   // полный прямоугольник монитора в десктопе
    RECT m_cropRect{};   // кроп ОТНОСИТЕЛЬНО монитора (0..w, 0..h)
    UINT m_fovW = 0;
    UINT m_fovH = 0;

    std::vector<uint8_t> m_buffer;
    Frame  m_frame{};
    bool   m_frameHeld = false;
};
