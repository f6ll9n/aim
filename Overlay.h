#pragma once
#include <Windows.h>

// ============================================================================
//  Overlay — прозрачное окно поверх игры (topmost + click-through).
//
//  Техника: layered window с цветовым ключом (magenta RGB(255,0,255)).
//  Фон красится ключевым цветом -> полностью прозрачен; то, что рисуем
//  поверх (боксы/текст), отображается. Окно не перехватывает ввод
//  (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE), т.е. играть можно как обычно.
// ============================================================================
class Overlay
{
public:
    Overlay() = default;
    ~Overlay();

    // Создать окно-оверлей, покрывающее заданный прямоугольник экрана.
    bool Create(HINSTANCE hInst, const RECT& screenRect, const wchar_t* title = L"Overlay");
    void Destroy();

    void BeginDraw();                     // очистить кадр (прозрачный фон)
    void EndDraw();                       // применить к окну (UpdateLayeredWindow)

    void DrawRect(int x, int y, int w, int h, COLORREF color, int thickness = 2);
    void DrawFilledRect(int x, int y, int w, int h, COLORREF color);
    void DrawLine(int x1, int y1, int x2, int y2, COLORREF color, int thickness = 1);
    void DrawText(int x, int y, const wchar_t* text, int height, COLORREF color, bool bold = true);

    HINSTANCE Instance() const noexcept { return m_hInst; }
    HWND Handle()   const noexcept { return m_hwnd; }
    int   Width()   const noexcept { return m_width; }
    int   Height()  const noexcept { return m_height; }

    // DEBUG: дамп сырых пикселей текущего кадра (диагностика отрисовки).
    bool DumpRaw(const wchar_t* path) const;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HINSTANCE m_hInst = nullptr;
    HWND      m_hwnd  = nullptr;
    int       m_width = 0;
    int       m_height = 0;

    HDC       m_memDC  = nullptr;
    HBITMAP   m_dib    = nullptr;
    void*     m_bits   = nullptr;
    HGDIOBJ   m_oldGdi = nullptr;
    HFONT     m_font   = nullptr;
    HFONT     m_txtFont = nullptr;   // кешированный шрифт для DrawText
    int       m_txtH    = 0;
    bool      m_txtBold = false;
    COLORREF  m_key = RGB(255, 0, 255);   // прозрачный фон
};
