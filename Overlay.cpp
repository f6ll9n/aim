#include "Overlay.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>

namespace
{
    const wchar_t* kClassName = L"BionicAimOverlayClass";
}

Overlay::~Overlay()
{
    Destroy();
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
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
    RegisterClassExW(&wc);   // повторная регистрация вернёт ошибку — игнорируем

    m_width  = screenRect.right  - screenRect.left;
    m_height = screenRect.bottom - screenRect.top;

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT |
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kClassName, title, WS_POPUP,
        screenRect.left, screenRect.top, m_width, m_height,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd)
        return false;

    // 32-битный DIB (top-down) + совместимый DC для отрисовки.
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = m_width;
    bmi.bmiHeader.biHeight      = -m_height;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_memDC = CreateCompatibleDC(nullptr);
    m_dib   = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    if (!m_dib)
        return false;
    m_oldGdi = SelectObject(m_memDC, m_dib);

    m_font = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    return true;
}

void Overlay::Destroy()
{
    if (m_oldGdi)  { SelectObject(m_memDC, m_oldGdi); m_oldGdi = nullptr; }
    if (m_font)    { DeleteObject(m_font);            m_font = nullptr; }
    if (m_txtFont) { DeleteObject(m_txtFont);         m_txtFont = nullptr; }
    if (m_dib)     { DeleteObject(m_dib);             m_dib = nullptr; }
    if (m_memDC)   { DeleteDC(m_memDC);               m_memDC = nullptr; }
    if (m_hwnd)    { DestroyWindow(m_hwnd);           m_hwnd = nullptr; }
}

void Overlay::BeginDraw()
{
    // Заливаем кадр ключевым цветом (прозрачность через color-key).
    // Используем стоковую DC-кисть (SetDCBrushColor) — без Create/Delete.
    RECT r{ 0, 0, m_width, m_height };
    SetDCBrushColor(m_memDC, m_key);
    FillRect(m_memDC, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Overlay::EndDraw()
{
    POINT srcPt{ 0, 0 };
    SIZE  size{ m_width, m_height };
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, nullptr, nullptr, &size, m_memDC,
                        &srcPt, m_key, &blend, ULW_COLORKEY);
}

void Overlay::DrawRect(int x, int y, int w, int h, COLORREF color, int thickness)
{
    // Рисуем через FillRect (а не перо), т.к. перьевая отрисовка на
    // 32-битном DIB-секции не рендерится в layered-окне.
    if (w <= 0 || h <= 0)
        return;

    int t = thickness < 1 ? 1 : thickness;
    if (w < 2 * t) t = w / 2;
    if (h < 2 * t) t = h / 2;
    if (t < 1) t = 1;

    // верх + низ
    DrawFilledRect(x, y,         w, t, color);
    DrawFilledRect(x, y + h - t, w, t, color);
    // лево + право (без перекрытия углов уже залитых)
    DrawFilledRect(x,         y + t, t, h - 2 * t, color);
    DrawFilledRect(x + w - t, y + t, t, h - 2 * t, color);
}

void Overlay::DrawFilledRect(int x, int y, int w, int h, COLORREF color)
{
    RECT r{ x, y, x + w, y + h };
    SetDCBrushColor(m_memDC, color);
    FillRect(m_memDC, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Overlay::DrawLine(int x1, int y1, int x2, int y2, COLORREF color, int thickness)
{
    // DDA-линия из залитых квадратов (бруш), обходя проблему с пером.
    int t = thickness < 1 ? 1 : thickness;
    int dx = x2 - x1;
    int dy = y2 - y1;
    int steps = std::max(std::abs(dx), std::abs(dy));
    int half = t / 2;

    if (steps == 0)
    {
        DrawFilledRect(x1 - half, y1 - half, t, t, color);
        return;
    }

    for (int i = 0; i <= steps; ++i)
    {
        int x = x1 + (int)((double)dx * i / steps);
        int y = y1 + (int)((double)dy * i / steps);
        DrawFilledRect(x - half, y - half, t, t, color);
    }
}

void Overlay::DrawText(int x, int y, const wchar_t* text, int height, COLORREF color, bool bold)
{
    // Кешируем шрифт: пересоздаём только если изменились высота/жирность.
    if (!m_txtFont || m_txtH != height || m_txtBold != bold)
    {
        if (m_txtFont) DeleteObject(m_txtFont);
        m_txtFont = CreateFontW(-height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        m_txtH = height;
        m_txtBold = bold;
    }
    HGDIOBJ oldFont = SelectObject(m_memDC, m_txtFont);
    SetBkMode(m_memDC, TRANSPARENT);
    SetTextColor(m_memDC, color);
    TextOutW(m_memDC, x, y, text, (int)wcslen(text));
    SelectObject(m_memDC, oldFont);
}

bool Overlay::DumpRaw(const wchar_t* path) const
{
    if (!m_bits)
        return false;
    FILE* f = _wfopen(path, L"wb");
    if (!f)
        return false;
    const uint32_t w = static_cast<uint32_t>(m_width);
    const uint32_t h = static_cast<uint32_t>(m_height);
    fwrite(&w, sizeof(w), 1, f);
    fwrite(&h, sizeof(h), 1, f);
    fwrite(m_bits, 1, static_cast<size_t>(w) * h * 4, f);
    fclose(f);
    return true;
}
