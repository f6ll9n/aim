#pragma once
#include <Windows.h>
#include <cstdint>
#include <d3d11.h>
#include <dxgi1_2.h>
#pragma push_macro("DrawText")
#undef DrawText
#include <d2d1_1.h>
#include <dwrite.h>
#pragma pop_macro("DrawText")
#include <dcomp.h>
#include <wrl/client.h>

// ============================================================================
//  Overlay — прозрачное аппаратное окно поверх игры (Direct2D + DirectComposition).
//
//  Работает на GPU без CPU-копирования и без GDI UpdateLayeredWindow:
//  WS_EX_NOREDIRECTIONBITMAP + FLIP_SEQUENTIAL + DXGI_ALPHA_MODE_PREMULTIPLIED.
//  Окно не перехватывает ввод (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | HTTRANSPARENT).
// ============================================================================
class Overlay
{
public:
    Overlay() = default;
    ~Overlay();

    bool Create(HINSTANCE hInst, const RECT& screenRect, const wchar_t* title = L"Overlay");
    void Destroy();

    void BeginDraw();
    void EndDraw();

    void DrawRect(int x, int y, int w, int h, COLORREF color, int thickness = 2, uint8_t alpha = 255);
    void DrawFilledRect(int x, int y, int w, int h, COLORREF color, uint8_t alpha = 255);
    void DrawLine(int x1, int y1, int x2, int y2, COLORREF color, int thickness = 1, uint8_t alpha = 255);
    void DrawText(int x, int y, const wchar_t* text, int height, COLORREF color, bool bold = true, uint8_t alpha = 255);

    HINSTANCE Instance() const noexcept { return m_hInst; }
    HWND Handle()   const noexcept { return m_hwnd; }
    int   Width()   const noexcept { return m_width; }
    int   Height()  const noexcept { return m_height; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool InitDirect2D();

    HINSTANCE m_hInst  = nullptr;
    HWND      m_hwnd   = nullptr;
    int       m_width  = 0;
    int       m_height = 0;

    Microsoft::WRL::ComPtr<ID3D11Device>           m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_d3dCtx;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>        m_swapChain;

    Microsoft::WRL::ComPtr<ID2D1Factory1>          m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device>            m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>     m_d2dCtx;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1>           m_targetBitmap;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>   m_brush;

    Microsoft::WRL::ComPtr<IDWriteFactory>         m_dwrite;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>      m_textFormat;
    int                                            m_txtH    = 0;
    bool                                           m_txtBold = false;

    Microsoft::WRL::ComPtr<IDCompositionDevice>    m_dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget>    m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual>    m_dcompVisual;
};
