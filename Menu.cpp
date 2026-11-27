#include "Menu.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <string>
#include <cstdio>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    const wchar_t* kMenuClass = L"BionicAimMenuClass";

    ImFont* g_iconFont = nullptr;
    ImFont* g_boldFont = nullptr;
    ImFont* g_smallBold = nullptr;

    constexpr const char* ICON_AIM    = "\xEF\x81\x9B";  // crosshairs  U+F05B
    constexpr const char* ICON_VISUAL = "\xEF\x81\xAE";  // eye         U+F06E
    constexpr const char* ICON_GEARS  = "\xEF\x82\x85";  // gears       U+F085
    constexpr const char* ICON_USER   = "\xEF\x80\x87";  // user        U+F007

    static const ImWchar kIconRanges[] =
    {
        0xF007, 0xF007,
        0xF05B, 0xF05B,
        0xF06E, 0xF06E,
        0xF085, 0xF085,
        0,
    };

    static std::string ExeDir()
    {
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return std::string();
        std::string s(buf, n);
        size_t pos = s.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        return s.substr(0, pos + 1);
    }

    // --- состояние текущего групбокса (чтобы колонки не пересекались) ---
    struct GroupBoxState
    {
        ImVec2      p0{};
        float       width  = 250.0f;
        float       innerW = 222.0f;
        const char* title  = "";
    };
    static GroupBoxState g_box;

    void BeginGroupBox(const char* title, float width)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);

        g_box.p0     = ImGui::GetCursorScreenPos();
        g_box.width  = width;
        g_box.innerW = width - 28.0f;
        g_box.title  = title;

        ImGui::BeginGroup();
        ImGui::Indent(14.0f);
        ImGui::PushItemWidth(g_box.innerW);
        ImGui::Dummy(ImVec2(g_box.innerW, 10.0f));
    }

    void EndGroupBox()
    {
        ImGui::Dummy(ImVec2(g_box.innerW, 6.0f));
        ImGui::PopItemWidth();
        ImGui::Unindent(14.0f);
        ImGui::EndGroup();

        float maxY = ImGui::GetItemRectMax().y;
        ImVec2 p0  = g_box.p0;
        ImVec2 p1(p0.x + g_box.width, maxY);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 255));

        dl->ChannelsSetCurrent(1);
        dl->AddRect(p0, p1, IM_COL32(42, 42, 42, 255));
        dl->AddRect(ImVec2(p0.x + 1.0f, p0.y + 1.0f),
                    ImVec2(p1.x - 1.0f, p1.y - 1.0f),
                    IM_COL32(10, 10, 10, 255));

        // Заголовок разрывает верхнюю рамку (фирменный стиль рефа).
        if (g_smallBold) ImGui::PushFont(g_smallBold);
        ImVec2 ts = ImGui::CalcTextSize(g_box.title);
        float tx = p0.x + 12.0f;
        float ty = p0.y - ts.y * 0.5f;
        dl->AddRectFilled(ImVec2(tx - 4.0f, p0.y - 2.0f),
                          ImVec2(tx + ts.x + 4.0f, p0.y + 3.0f),
                          IM_COL32(20, 20, 20, 255));
        dl->AddText(ImVec2(tx + 1.0f, ty + 1.0f), IM_COL32(0, 0, 0, 230), g_box.title);
        dl->AddText(ImVec2(tx, ty), IM_COL32(226, 226, 226, 255), g_box.title);
        if (g_smallBold) ImGui::PopFont();

        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 14.0f));
    }

    // Чекбокс 9x9: кислотный градиент при включении + 1px чёрная обводка.
    bool SkeetCheck(const char* id, const char* label, bool* v, bool* outHovered = nullptr)
    {
        bool changed = false;
        const float boxSz = 9.0f;
        const float padX  = 11.0f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 ts = ImGui::CalcTextSize(label);
        float rowH = (ts.y > boxSz) ? ts.y : boxSz;

        ImGui::InvisibleButton(id, ImVec2(boxSz + padX + ts.x, rowH));
        if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
        bool hovered = ImGui::IsItemHovered();
        if (outHovered) *outHovered = hovered;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float by = p.y + (rowH - boxSz) * 0.5f;
        ImVec2 b0(p.x, by), b1(p.x + boxSz, by + boxSz);

        if (*v)
        {
            dl->AddRectFilledMultiColor(b0, b1,
                IM_COL32(154, 222, 42, 255), IM_COL32(154, 222, 42, 255),
                IM_COL32(104, 154, 22, 255), IM_COL32(104, 154, 22, 255));
        }
        else
        {
            dl->AddRectFilledMultiColor(b0, b1,
                IM_COL32(48, 48, 48, 255), IM_COL32(48, 48, 48, 255),
                IM_COL32(34, 34, 34, 255), IM_COL32(34, 34, 34, 255));
        }
        dl->AddRect(ImVec2(b0.x - 1.0f, b0.y - 1.0f),
                    ImVec2(b1.x + 1.0f, b1.y + 1.0f),
                    hovered ? IM_COL32(75, 75, 75, 255) : IM_COL32(8, 8, 8, 255));

        ImU32 txtCol = *v ? IM_COL32(218, 218, 218, 255) : IM_COL32(165, 165, 165, 255);
        dl->AddText(ImVec2(p.x + boxSz + padX + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 190), label);
        dl->AddText(ImVec2(p.x + boxSz + padX, p.y), txtCol, label);
        return changed;
    }

    // Слайдер в стиле рефа: подпись сверху, тонкая полоса, лаймовый градиент, цифра на краю заливки.
    bool SkeetSliderF(const char* label, const char* id, float* v, float mn, float mx, const char* fmt)
    {
        bool changed = false;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImVec2 lp = ImGui::GetCursorScreenPos();
        float xOff = lp.x + 20.0f;  // отступ слева как у текста чекбокса
        float w    = g_box.innerW - 20.0f;

        dl->AddText(ImVec2(xOff + 1.0f, lp.y + 1.0f), IM_COL32(0, 0, 0, 190), label);
        dl->AddText(ImVec2(xOff, lp.y), IM_COL32(204, 204, 204, 255), label);

        const float labelH = ImGui::GetTextLineHeight();
        const float barY   = lp.y + labelH + 3.0f;
        const float barH   = 7.0f;
        const float hitH   = 14.0f;

        ImGui::SetCursorScreenPos(ImVec2(xOff, barY - 3.0f));
        ImGui::InvisibleButton(id, ImVec2(w, hitH));
        if (ImGui::IsItemActive())
        {
            float f = (ImGui::GetIO().MousePos.x - xOff) / w;
            f = std::clamp(f, 0.0f, 1.0f);
            float nv = mn + f * (mx - mn);
            if (nv != *v) { *v = nv; changed = true; }
        }
        bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

        ImVec2 t0(xOff, barY), t1(xOff + w, barY + barH);
        dl->AddRectFilledMultiColor(t0, t1,
            IM_COL32(34, 34, 34, 255), IM_COL32(34, 34, 34, 255),
            IM_COL32(46, 46, 46, 255), IM_COL32(46, 46, 46, 255));

        float frac = std::clamp((*v - mn) / (mx - mn), 0.0f, 1.0f);
        float fillX = xOff + frac * w;
        if (fillX > xOff + 1.0f)
        {
            dl->AddRectFilledMultiColor(t0, ImVec2(fillX, barY + barH),
                IM_COL32(154, 222, 42, 255), IM_COL32(154, 222, 42, 255),
                IM_COL32(102, 152, 20, 255), IM_COL32(102, 152, 20, 255));
        }
        dl->AddRect(ImVec2(t0.x - 1.0f, t0.y - 1.0f),
                    ImVec2(t1.x + 1.0f, t1.y + 1.0f),
                    hovered ? IM_COL32(68, 68, 68, 255) : IM_COL32(8, 8, 8, 255));

        // Маленькие '-' и '+' по бокам как на рефе
        dl->AddText(ImVec2(xOff - 8.0f, barY - 4.0f), IM_COL32(100, 100, 100, 255), "-");
        dl->AddText(ImVec2(xOff + w + 3.0f, barY - 4.0f), IM_COL32(100, 100, 100, 255), "+");

        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 vs = ImGui::CalcTextSize(buf);
        float vx = std::clamp(fillX - vs.x * 0.5f, xOff, xOff + w - vs.x);
        float vy = barY + (barH - vs.y) * 0.5f;
        dl->AddText(ImVec2(vx + 1.0f, vy + 1.0f), IM_COL32(0, 0, 0, 255), buf);
        dl->AddText(ImVec2(vx - 1.0f, vy),        IM_COL32(0, 0, 0, 200), buf);
        dl->AddText(ImVec2(vx, vy),               IM_COL32(236, 236, 236, 255), buf);

        ImGui::SetCursorScreenPos(ImVec2(lp.x, barY + barH + 7.0f));
        return changed;
    }

    bool SkeetSliderI(const char* label, const char* id, int* v, int mn, int mx, const char* fmt, const char* maxText = nullptr)
    {
        float fv = static_cast<float>(*v);
        bool changed = false;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImVec2 lp = ImGui::GetCursorScreenPos();
        float xOff = lp.x + 20.0f;
        float w    = g_box.innerW - 20.0f;

        dl->AddText(ImVec2(xOff + 1.0f, lp.y + 1.0f), IM_COL32(0, 0, 0, 190), label);
        dl->AddText(ImVec2(xOff, lp.y), IM_COL32(204, 204, 204, 255), label);

        const float labelH = ImGui::GetTextLineHeight();
        const float barY   = lp.y + labelH + 3.0f;
        const float barH   = 7.0f;
        const float hitH   = 14.0f;

        ImGui::SetCursorScreenPos(ImVec2(xOff, barY - 3.0f));
        ImGui::InvisibleButton(id, ImVec2(w, hitH));
        if (ImGui::IsItemActive())
        {
            float f = (ImGui::GetIO().MousePos.x - xOff) / w;
            f = std::clamp(f, 0.0f, 1.0f);
            int nv = static_cast<int>(mn + f * (mx - mn) + 0.5f);
            if (nv != *v) { *v = nv; changed = true; }
        }
        bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

        ImVec2 t0(xOff, barY), t1(xOff + w, barY + barH);
        dl->AddRectFilledMultiColor(t0, t1,
            IM_COL32(34, 34, 34, 255), IM_COL32(34, 34, 34, 255),
            IM_COL32(46, 46, 46, 255), IM_COL32(46, 46, 46, 255));

        float frac = std::clamp((fv - mn) / static_cast<float>(mx - mn), 0.0f, 1.0f);
        float fillX = xOff + frac * w;
        if (fillX > xOff + 1.0f)
        {
            dl->AddRectFilledMultiColor(t0, ImVec2(fillX, barY + barH),
                IM_COL32(154, 222, 42, 255), IM_COL32(154, 222, 42, 255),
                IM_COL32(102, 152, 20, 255), IM_COL32(102, 152, 20, 255));
        }
        dl->AddRect(ImVec2(t0.x - 1.0f, t0.y - 1.0f),
                    ImVec2(t1.x + 1.0f, t1.y + 1.0f),
                    hovered ? IM_COL32(68, 68, 68, 255) : IM_COL32(8, 8, 8, 255));

        dl->AddText(ImVec2(xOff - 8.0f, barY - 4.0f), IM_COL32(100, 100, 100, 255), "-");
        dl->AddText(ImVec2(xOff + w + 3.0f, barY - 4.0f), IM_COL32(100, 100, 100, 255), "+");

        char buf[32];
        if (maxText && *v >= mx)
            std::snprintf(buf, sizeof(buf), "%s", maxText);
        else
            std::snprintf(buf, sizeof(buf), fmt, *v);

        ImVec2 vs = ImGui::CalcTextSize(buf);
        float vx = std::clamp(fillX - vs.x * 0.5f, xOff, xOff + w - vs.x);
        float vy = barY + (barH - vs.y) * 0.5f;
        dl->AddText(ImVec2(vx + 1.0f, vy + 1.0f), IM_COL32(0, 0, 0, 255), buf);
        dl->AddText(ImVec2(vx - 1.0f, vy),        IM_COL32(0, 0, 0, 200), buf);
        dl->AddText(ImVec2(vx, vy),               IM_COL32(236, 236, 236, 255), buf);

        ImGui::SetCursorScreenPos(ImVec2(lp.x, barY + barH + 7.0f));
        return changed;
    }

    bool SkeetButton(const char* label, float height = 24.0f)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float xOff = p.x + 20.0f;
        float w    = g_box.innerW - 20.0f;

        ImGui::SetCursorScreenPos(ImVec2(xOff, p.y));
        bool clicked = ImGui::InvisibleButton(label, ImVec2(w, height));
        bool hovered = ImGui::IsItemHovered();
        bool active  = ImGui::IsItemActive();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 b0(xOff, p.y), b1(xOff + w, p.y + height);

        ImU32 topC = active  ? IM_COL32(28, 28, 28, 255)
                   : hovered ? IM_COL32(46, 46, 46, 255)
                             : IM_COL32(38, 38, 38, 255);
        ImU32 botC = active  ? IM_COL32(22, 22, 22, 255)
                   : hovered ? IM_COL32(34, 34, 34, 255)
                             : IM_COL32(26, 26, 26, 255);

        dl->AddRectFilledMultiColor(b0, b1, topC, topC, botC, botC);
        dl->AddRect(b0, b1, IM_COL32(52, 52, 52, 255));
        dl->AddRect(ImVec2(b0.x - 1.0f, b0.y - 1.0f),
                    ImVec2(b1.x + 1.0f, b1.y + 1.0f),
                    IM_COL32(8, 8, 8, 255));

        if (g_smallBold) ImGui::PushFont(g_smallBold);
        ImVec2 ts = ImGui::CalcTextSize(label);
        float tx = xOff + (w - ts.x) * 0.5f;
        float ty = p.y  + (height - ts.y) * 0.5f;
        dl->AddText(ImVec2(tx + 1.0f, ty + 1.0f), IM_COL32(0, 0, 0, 220), label);
        dl->AddText(ImVec2(tx, ty), IM_COL32(220, 220, 220, 255), label);
        if (g_smallBold) ImGui::PopFont();

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height + 6.0f));
        return clicked;
    }

    bool SkeetCombo(const char* id, const char* label, int* current, const char* const items[], int count)
    {
        bool changed = false;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float xOff = p.x + 20.0f;
        float w    = g_box.innerW - 20.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (label && label[0])
        {
            dl->AddText(ImVec2(xOff + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 190), label);
            dl->AddText(ImVec2(xOff, p.y), IM_COL32(204, 204, 204, 255), label);
            p.y += ImGui::GetTextLineHeight() + 3.0f;
        }

        const float h = 21.0f;
        ImGui::SetCursorScreenPos(ImVec2(xOff, p.y));
        if (ImGui::InvisibleButton(id, ImVec2(w, h)))
            ImGui::OpenPopup(id);
        bool hovered = ImGui::IsItemHovered();

        ImVec2 b0(xOff, p.y), b1(xOff + w, p.y + h);
        dl->AddRectFilledMultiColor(b0, b1,
            hovered ? IM_COL32(40, 40, 40, 255) : IM_COL32(32, 32, 32, 255),
            hovered ? IM_COL32(40, 40, 40, 255) : IM_COL32(32, 32, 32, 255),
            IM_COL32(24, 24, 24, 255), IM_COL32(24, 24, 24, 255));
        dl->AddRect(b0, b1, IM_COL32(46, 46, 46, 255));
        dl->AddRect(ImVec2(b0.x - 1.0f, b0.y - 1.0f),
                    ImVec2(b1.x + 1.0f, b1.y + 1.0f),
                    IM_COL32(8, 8, 8, 255));

        const char* curTxt = (*current >= 0 && *current < count) ? items[*current] : "";
        ImVec2 ts = ImGui::CalcTextSize(curTxt);
        dl->AddText(ImVec2(xOff + 8.0f, p.y + (h - ts.y) * 0.5f),
                    IM_COL32(190, 190, 190, 255), curTxt);

        // Треугольник стрелки справа
        float ax = b1.x - 11.0f, ay = p.y + h * 0.5f - 1.5f;
        dl->AddTriangleFilled(ImVec2(ax - 3.5f, ay),
                              ImVec2(ax + 3.5f, ay),
                              ImVec2(ax, ay + 4.0f),
                              IM_COL32(140, 140, 140, 255));

        ImGui::SetNextWindowPos(ImVec2(xOff, p.y + h + 1.0f));
        ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
        if (ImGui::BeginPopup(id, ImGuiWindowFlags_NoMove))
        {
            for (int i = 0; i < count; ++i)
            {
                bool sel = (i == *current);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    sel ? ImVec4(0.60f, 0.87f, 0.16f, 1.0f) : ImVec4(0.78f, 0.78f, 0.78f, 1.0f));
                if (ImGui::Selectable(items[i], sel))
                {
                    *current = i;
                    changed = true;
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndPopup();
        }

        ImGui::SetCursorScreenPos(ImVec2(p.x - 20.0f, p.y + h + 7.0f));
        return changed;
    }

    void SkeetInfoRow(const char* label, const char* value, ImU32 valCol)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 vs = ImGui::CalcTextSize(value);

        dl->AddText(ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 190), label);
        dl->AddText(ImVec2(p.x, p.y), IM_COL32(175, 175, 175, 255), label);

        float vx = p.x + g_box.innerW - vs.x;
        dl->AddText(ImVec2(vx + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 210), value);
        dl->AddText(ImVec2(vx, p.y), valCol, value);

        ImGui::Dummy(ImVec2(g_box.innerW, ImGui::GetTextLineHeight() + 2.0f));
    }
}

static std::string KeyName(int vk)
{
    if (vk <= 0) return "-";
    switch (vk)
    {
    case VK_LBUTTON:  return "LMB";
    case VK_RBUTTON:  return "RMB";
    case VK_MBUTTON:  return "MMB";
    case VK_XBUTTON1: return "X1";
    case VK_XBUTTON2: return "X2";
    case VK_MENU:     return "ALT";
    case VK_SHIFT:    return "SHIFT";
    case VK_LSHIFT:   return "LSHIFT";
    case VK_RSHIFT:   return "RSHIFT";
    case VK_CONTROL:  return "CTRL";
    case VK_LCONTROL: return "LCTRL";
    case VK_RCONTROL: return "RCTRL";
    case VK_SPACE:    return "SPACE";
    case VK_TAB:      return "TAB";
    case VK_RETURN:   return "ENTER";
    case VK_BACK:     return "BACK";
    case VK_DELETE:   return "DEL";
    case VK_INSERT:   return "INS";
    case VK_HOME:     return "HOME";
    case VK_END:      return "END";
    case VK_PRIOR:    return "PGUP";
    case VK_NEXT:     return "PGDN";
    case VK_LEFT:     return "LEFT";
    case VK_RIGHT:    return "RIGHT";
    case VK_UP:       return "UP";
    case VK_DOWN:     return "DOWN";
    case VK_CAPITAL:  return "CAPS";
    case VK_ESCAPE:   return "ESC";
    }
    if (vk >= VK_F1 && vk <= VK_F24)
        return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= '0' && vk <= '9')
        return std::string(1, static_cast<char>(vk));
    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, static_cast<char>(vk));
    char b[16];
    std::snprintf(b, sizeof(b), "0x%02X", vk);
    return b;
}

static bool IsBindable(int vk)
{
    switch (vk)
    {
    case VK_ESCAPE: case VK_LWIN: case VK_RWIN:
    case VK_SHIFT:  case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        return false;
    default:
        return true;
    }
}

Menu::~Menu()
{
    Destroy();
}

LRESULT CALLBACK Menu::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 0;

    Menu* self = reinterpret_cast<Menu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        if (!self || !self->m_grabFocus)
            return MA_NOACTIVATE;
        return MA_ACTIVATE;

    case WM_NCHITTEST:
    {
        POINT p{ static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)) };
        RECT r;
        GetWindowRect(hwnd, &r);
        constexpr int kTitleH  = 28;
        constexpr int kBtnZone = 44;
        if (p.y >= r.top && p.y <= r.top + kTitleH &&
            p.x >= r.left && p.x <= r.right - kBtnZone)
            return HTCAPTION;
        return HTCLIENT;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Menu::Create(HINSTANCE hInst, int screenW, int screenH)
{
    m_hInst = hInst;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kMenuClass;
    RegisterClassExW(&wc);

    int x = (screenW - m_w) / 2;
    int y = (screenH - m_h) / 2 - 20;

    // WS_EX_NOACTIVATE гарантирует, что меню не крадёт фокус у игры и не сворачивает её.
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kMenuClass, L"aim menu", WS_POPUP,
        x, y, m_w, m_h,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd)
        return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (!CreateDeviceAndSwapChain())
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_ctx);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename  = nullptr;
    io.LogFilename  = nullptr;

    static const ImWchar ranges[] =
    {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0x2010, 0x205E,
        0x25A0, 0x25FF,
        0,
    };
    io.Fonts->Clear();
    io.FontDefault = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf", 14.5f, nullptr, ranges);
    g_smallBold = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 14.5f, nullptr, ranges);
    g_boldFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 18.0f, nullptr, ranges);

    std::string faPath = ExeDir() + "fa-solid-900.ttf";
    g_iconFont = io.Fonts->AddFontFromFileTTF(faPath.c_str(), 26.0f, nullptr, kIconRanges);
    if (!g_iconFont)
        g_iconFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Users\\f6ll9n\\Desktop\\aim\\fonts\\fa-solid-900.ttf", 26.0f, nullptr, kIconRanges);

    SetStyle();
    return true;
}

void Menu::SetStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding    = 0.0f;
    st.ChildRounding     = 0.0f;
    st.FrameRounding     = 0.0f;
    st.PopupRounding     = 0.0f;
    st.GrabRounding      = 0.0f;
    st.TabRounding       = 0.0f;
    st.ScrollbarRounding = 0.0f;
    st.WindowBorderSize  = 0.0f;
    st.FrameBorderSize   = 1.0f;
    st.WindowPadding     = ImVec2(0, 0);
    st.FramePadding      = ImVec2(6, 4);
    st.ItemSpacing       = ImVec2(8, 5);
    st.ItemInnerSpacing  = ImVec2(6, 4);
    st.ScrollbarSize     = 5.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.42f, 0.43f, 0.46f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.05f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
}

bool Menu::CreateDeviceAndSwapChain()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = m_hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd,
        &m_swap, &m_device, &got, &m_ctx);
    if (FAILED(hr))
        return false;

    ID3D11Texture2D* bb = nullptr;
    m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb));
    if (!bb)
        return false;
    m_device->CreateRenderTargetView(bb, nullptr, &m_rtv);
    bb->Release();
    return m_rtv != nullptr;
}

void Menu::Destroy()
{
    if (m_rtv)    { m_rtv->Release();    m_rtv = nullptr; }
    if (m_ctx)    { m_ctx->ClearState(); m_ctx->Release(); m_ctx = nullptr; }
    if (m_swap)   { m_swap->Release();   m_swap = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    if (m_hwnd)   { DestroyWindow(m_hwnd); m_hwnd = nullptr; }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void Menu::SetVisible(bool v, bool grabFocus)
{
    m_visible   = v;
    m_grabFocus = grabFocus;

    if (!v)
    {
        m_listening    = false;
        m_listenTarget = nullptr;
        m_listenActive = false;
    }
    if (!m_hwnd)
        return;

    if (v)
    {
        // Снимаем ограничение курсора, чтобы можно было кликать по меню, не сворачивая игру.
        ClipCursor(nullptr);

        if (grabFocus)
        {
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            ShowWindow(m_hwnd, SW_SHOW);
            SetForegroundWindow(m_hwnd);
        }
        else
        {
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
    }
    else
    {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void Menu::KeyBindTag(int* key, bool allowClear)
{
    char tag[32];
    if (m_listening && m_listenTarget == key)
        std::snprintf(tag, sizeof(tag), "[...]");
    else
        std::snprintf(tag, sizeof(tag), "[%s]", KeyName(*key).c_str());

    ImVec2 ts = ImGui::CalcTextSize(tag);
    ImVec2 cur = ImGui::GetCursorScreenPos();
    // Ставим в ту же строку справа по ширине бокса
    ImVec2 itemMin = ImGui::GetItemRectMin();
    ImVec2 itemMax = ImGui::GetItemRectMax();
    float tagX = g_box.p0.x + 14.0f + g_box.innerW - ts.x;
    float tagY = itemMin.y;

    ImGui::SetCursorScreenPos(ImVec2(tagX, tagY));
    char idBuf[32];
    std::snprintf(idBuf, sizeof(idBuf), "##kb%p", static_cast<void*>(key));
    ImGui::InvisibleButton(idBuf, ImVec2(ts.x + 4.0f, itemMax.y - itemMin.y));
    bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        m_listening    = true;
        m_listenTarget = key;
        m_listenActive = false;
    }
    if (allowClear && ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        *key = 0;
        if (m_listenTarget == key)
        {
            m_listening    = false;
            m_listenTarget = nullptr;
            m_listenActive = false;
        }
    }
    if (hovered)
        ImGui::SetTooltip(allowClear ? "ЛКМ — бинд, ПКМ — убрать" : "ЛКМ — переназначить");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = (m_listening && m_listenTarget == key) ? IM_COL32(154, 222, 42, 255)
              : hovered                                ? IM_COL32(220, 220, 220, 255)
                                                       : IM_COL32(125, 125, 125, 255);
    dl->AddText(ImVec2(tagX + 1.0f, tagY + 1.0f), IM_COL32(0, 0, 0, 200), tag);
    dl->AddText(ImVec2(tagX, tagY), col, tag);

    ImGui::SetCursorScreenPos(cur);
}

Menu::Action Menu::Render(AimSettings& s,
                          std::atomic<float>& conf,
                          std::atomic<float>& iou,
                          const RuntimeInfo& info)
{
    Action action = Action::None;
    if (!m_visible || !m_device || !m_swap || !m_rtv)
        return action;

    m_grabFocus = s.grabFocus;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(m_w), static_cast<float>(m_h)));
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##skeetmenu", nullptr, kFlags);

    ImDrawList* winDl = ImGui::GetWindowDrawList();
    const float W = static_cast<float>(m_w);
    const float H = static_cast<float>(m_h);

    // 1) Двойная тёмная рамка окна (1px чёрная + 1px тёмно-серая + тонкая подсветка)
    winDl->AddRect(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 8, 8, 255));
    winDl->AddRect(ImVec2(1, 1), ImVec2(W - 1, H - 1), IM_COL32(46, 46, 46, 255));
    winDl->AddRect(ImVec2(2, 2), ImVec2(W - 2, H - 2), IM_COL32(12, 12, 12, 255));

    // 2) Фирменная радужная полоса сверху (плавный градиент во всю ширину)
    {
        const float y0 = 3.0f, y1 = 5.0f;
        const float x0 = 3.0f, x1 = W - 3.0f;
        const float seg = (x1 - x0) / 3.0f;
        const ImU32 c0 = IM_COL32(55,  177, 218, 255);
        const ImU32 c1 = IM_COL32(158,  86, 224, 255);
        const ImU32 c2 = IM_COL32(217,  68, 138, 255);
        const ImU32 c3 = IM_COL32(204, 227,  53, 255);
        winDl->AddRectFilledMultiColor(ImVec2(x0,           y0), ImVec2(x0 + seg,     y1), c0, c1, c1, c0);
        winDl->AddRectFilledMultiColor(ImVec2(x0 + seg,     y0), ImVec2(x0 + seg * 2, y1), c1, c2, c2, c1);
        winDl->AddRectFilledMultiColor(ImVec2(x0 + seg * 2, y0), ImVec2(x1,           y1), c2, c3, c3, c2);
        winDl->AddLine(ImVec2(x0, y1), ImVec2(x1, y1), IM_COL32(8, 8, 8, 255));
    }

    // 3) Сайдбар слева (как на рефе: узкая вертикальная колонка с крупными иконками)
    static int section    = 0;
    static int presetIdx  = 0;
    static float flashT   = 0.0f;
    static const char* flashMsg = "";

    {
        float dt = ImGui::GetIO().DeltaTime;
        if (flashT > 0.0f) flashT = (flashT > dt) ? (flashT - dt) : 0.0f;
    }

    constexpr float kSideW = 74.0f;
    const float contentTop = 6.0f;
    const float bodyH      = H - contentTop - 3.0f;

    // Фон сайдбара
    winDl->AddRectFilled(ImVec2(3.0f, contentTop), ImVec2(3.0f + kSideW, H - 3.0f),
                         IM_COL32(12, 12, 12, 255));
    // Вертикальный разделитель
    winDl->AddLine(ImVec2(3.0f + kSideW, contentTop), ImVec2(3.0f + kSideW, H - 3.0f),
                   IM_COL32(38, 38, 38, 255));

    const char* kTabs[4]  = { "Аимбот", "Визуал", "Настройки", "Инфо" };
    const char* kIcons[4] = { ICON_AIM, ICON_VISUAL, ICON_GEARS, ICON_USER };

    const float tabStartY = contentTop + 26.0f;
    const float tabH      = 72.0f;

    for (int i = 0; i < 4; ++i)
    {
        float ty0 = tabStartY + i * tabH;
        float ty1 = ty0 + tabH;
        ImGui::SetCursorScreenPos(ImVec2(3.0f, ty0));
        char tid[16];
        std::snprintf(tid, sizeof(tid), "##tab%d", i);
        if (ImGui::InvisibleButton(tid, ImVec2(kSideW, tabH)))
            section = i;
        bool hovered = ImGui::IsItemHovered();
        bool active  = (section == i);

        if (active)
        {
            // Вырез в рамке как в Skeet: фон совпадает с основной областью
            winDl->AddRectFilled(ImVec2(3.0f, ty0), ImVec2(3.0f + kSideW + 1.0f, ty1),
                                 IM_COL32(17, 17, 17, 255));
            winDl->AddLine(ImVec2(3.0f, ty0), ImVec2(3.0f + kSideW, ty0), IM_COL32(38, 38, 38, 255));
            winDl->AddLine(ImVec2(3.0f, ty1), ImVec2(3.0f + kSideW, ty1), IM_COL32(38, 38, 38, 255));
            winDl->AddLine(ImVec2(3.0f, ty0 + 1.0f), ImVec2(3.0f + kSideW, ty0 + 1.0f), IM_COL32(8, 8, 8, 255));
            winDl->AddLine(ImVec2(3.0f, ty1 - 1.0f), ImVec2(3.0f + kSideW, ty1 - 1.0f), IM_COL32(8, 8, 8, 255));
        }

        const char* lbl = g_iconFont ? kIcons[i] : kTabs[i];
        if (g_iconFont) ImGui::PushFont(g_iconFont);
        ImVec2 is = ImGui::CalcTextSize(lbl);
        ImU32 col = active  ? IM_COL32(236, 236, 236, 255)
                  : hovered ? IM_COL32(150, 150, 150, 255)
                            : IM_COL32(88,  88,  88,  255);
        float ix = 3.0f + (kSideW - is.x) * 0.5f;
        float iy = ty0  + (tabH  - is.y) * 0.5f;
        winDl->AddText(ImVec2(ix + 1.0f, iy + 1.0f), IM_COL32(0, 0, 0, 220), lbl);
        winDl->AddText(ImVec2(ix, iy), col, lbl);
        if (g_iconFont) ImGui::PopFont();

        if (hovered)
            ImGui::SetTooltip("%s", kTabs[i]);
    }

    // Маленькая кнопка закрытия '×' в правом верхнем углу (и по N / Insert)
    {
        float bsz = 20.0f;
        ImVec2 cp(W - bsz - 8.0f, 9.0f);
        ImGui::SetCursorScreenPos(cp);
        ImGui::InvisibleButton("##close", ImVec2(bsz, bsz));
        bool ch = ImGui::IsItemHovered();
        ImVec2 xs = ImGui::CalcTextSize("×");
        winDl->AddText(ImVec2(cp.x + (bsz - xs.x) * 0.5f, cp.y + (bsz - xs.y) * 0.5f),
                       ch ? IM_COL32(240, 90, 90, 255) : IM_COL32(115, 115, 115, 255), "×");
        if (ImGui::IsItemClicked())
            SetVisible(false, m_grabFocus);
    }

    // 4) Контентная область (2 колонки групбоксов, как в референсе)
    const float contentX = 3.0f + kSideW + 18.0f;
    const float contentW = W - contentX - 18.0f;
    const float colGap   = 16.0f;
    const float colW     = (contentW - colGap) * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(contentX, contentTop + 18.0f));
    ImGui::BeginChild("##content", ImVec2(contentW + 8.0f, bodyH - 22.0f), false);

    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float  leftX  = base.x;
    const float  rightX = base.x + colW + colGap;

    switch (section)
    {
    case 0:   // АИМБОТ
    {
        // --- Левая колонка ---
        ImGui::SetCursorScreenPos(ImVec2(leftX, base.y + 6.0f));
        BeginGroupBox("Aimbot", colW);
        SkeetCheck("##aim_en", "Включить аим", &s.aimEnabled);
        KeyBindTag(&s.aimKey1, false);
        ImGui::Dummy(ImVec2(0, 4));
        SkeetSliderI("FOV аима", "##aimFov", &s.aimFov,
                     AimSettings::AIMFOV_MIN, AimSettings::AIMFOV_MAX, "%d px");
        SkeetSliderF("Сила доводки", "##strength", &s.aimStrength,
                     AimSettings::ST_MIN, AimSettings::ST_MAX, "%.2f");
        SkeetSliderF("Сглаживание", "##smooth", &s.smoothness,
                     AimSettings::SM_MIN, AimSettings::SM_MAX, "%.2f");
        SkeetSliderF("Макс. шаг мыши", "##maxStep", &s.aimMaxStep,
                     AimSettings::MS_MIN, AimSettings::MS_MAX, "%.0f px");
        SkeetSliderF("Мёртвая зона", "##deadzone", &s.aimDeadzone,
                     AimSettings::DZ_MIN, AimSettings::DZ_MAX, "%.0f px");
        EndGroupBox();

        // --- Правая колонка ---
        ImGui::SetCursorScreenPos(ImVec2(rightX, base.y + 6.0f));
        BeginGroupBox("Target & Detection", colW);
        SkeetSliderF("Высота точки (тело)", "##headFrac", &s.headFrac,
                     AimSettings::HD_MIN, AimSettings::HD_MAX, "%.2f");
        SkeetSliderF("Порог головы", "##headConf", &s.headConf,
                     AimSettings::HC_MIN, AimSettings::HC_MAX, "%.2f");
        {
            float c = conf.load();
            if (SkeetSliderF("Порог детекции (Conf)", "##conf", &c, 0.05f, 0.95f, "%.2f"))
                conf.store(c);
            float u = iou.load();
            if (SkeetSliderF("Порог NMS (IoU)", "##iou", &u, 0.10f, 0.85f, "%.2f"))
                iou.store(u);
        }
        EndGroupBox();

        BeginGroupBox("Keybinds", colW);
        {
            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Основная клавиша");
            KeyBindTag(&s.aimKey1, false);
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + ImGui::GetTextLineHeight() + 6.0f));

            rp = ImGui::GetCursorScreenPos();
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Доп. клавиша");
            KeyBindTag(&s.aimKey2, true);
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + ImGui::GetTextLineHeight() + 4.0f));
        }
        EndGroupBox();
        break;
    }

    case 1:   // ВИЗУАЛ
    {
        ImGui::SetCursorScreenPos(ImVec2(leftX, base.y + 6.0f));
        BeginGroupBox("ESP & Overlay", colW);
        SkeetCheck("##v_boxes", "Боксы целей", &s.showBoxes);
        SkeetCheck("##v_line",  "Линия до цели", &s.showLine);
        SkeetCheck("##v_cross", "Крестик по центру", &s.showCrosshair);
        SkeetCheck("##v_fov",   "Квадрат зоны наводки", &s.showFov);
        SkeetCheck("##v_log",   "Панель телеметрии", &s.showLog);
        EndGroupBox();

        ImGui::SetCursorScreenPos(ImVec2(rightX, base.y + 6.0f));
        BeginGroupBox("Performance", colW);
        SkeetSliderI("Лимит UI FPS", "##uiMaxFps", &s.uiMaxFps,
                     AimSettings::UIFPS_MIN, AimSettings::UIFPS_MAX, "%d fps", "Без лимита");
        ImGui::Dummy(ImVec2(0, 2));
        SkeetCheck("##v_grab", "Перехват фокуса (для курсора)", &s.grabFocus);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Выкл (по умолч.): меню открывается поверх игры без сворачивания.\nВкл: отдаёт фокус окну меню (если игра жёстко держит мышь).");
        EndGroupBox();
        break;
    }

    case 2:   // НАСТРОЙКИ И ПРЕСЕТЫ (как на референсе)
    {
        ImGui::SetCursorScreenPos(ImVec2(leftX, base.y + 6.0f));
        BeginGroupBox("Miscellaneous", colW);
        SkeetSliderI("Override FOV", "##m_fov", &s.aimFov,
                     AimSettings::AIMFOV_MIN, AimSettings::AIMFOV_MAX, "%d px");
        SkeetCheck("##m_aim",   "Aimbot active", &s.aimEnabled);
        KeyBindTag(&s.aimKey1, false);
        SkeetCheck("##m_boxes", "Show player boxes", &s.showBoxes);
        SkeetCheck("##m_line",  "Show snapline", &s.showLine);
        SkeetCheck("##m_fov",   "Show FOV square", &s.showFov);
        SkeetCheck("##m_log",   "Show debug HUD", &s.showLog);
        SkeetCheck("##m_grab",  "Grab focus on menu open", &s.grabFocus);
        ImGui::Dummy(ImVec2(0, 4));
        if (SkeetButton("Apply Legit Preset"))
        {
            s.aimStrength = 0.12f; s.smoothness = 0.22f; s.aimMaxStep = 20.0f;
            s.aimDeadzone = 9.0f;  s.aimFov = 320; s.headFrac = 0.30f; s.headConf = 0.60f;
            presetIdx = 0; flashMsg = "Пресет Legit применён"; flashT = 1.5f;
        }
        if (SkeetButton("Apply Fast Preset"))
        {
            s.aimStrength = 0.24f; s.smoothness = 0.10f; s.aimMaxStep = 36.0f;
            s.aimDeadzone = 6.0f;  s.aimFov = 420; s.headFrac = 0.26f; s.headConf = 0.50f;
            presetIdx = 1; flashMsg = "Пресет Fast применён"; flashT = 1.5f;
        }
        EndGroupBox();

        // Правая колонка: Settings + Presets (1-в-1 компоновка скрина)
        ImGui::SetCursorScreenPos(ImVec2(rightX, base.y + 6.0f));
        BeginGroupBox("Settings", colW);
        {
            char buf[48];
            SkeetInfoRow("Статус аима", info.aimHeld ? "АКТИВЕН" : (s.aimEnabled ? "готов" : "откл"),
                         info.aimHeld ? IM_COL32(154, 222, 42, 255) : IM_COL32(170, 170, 170, 255));
            std::snprintf(buf, sizeof(buf), "%s", info.gpu ? "DirectML (GPU)" : "CPU");
            SkeetInfoRow("Инференс", buf,
                         info.gpu ? IM_COL32(154, 222, 42, 255) : IM_COL32(230, 180, 80, 255));
            std::snprintf(buf, sizeof(buf), "%.0f / %.0f fps", info.capFps, info.detFps);
            SkeetInfoRow("CAP / DET", buf, IM_COL32(140, 220, 140, 255));
            std::snprintf(buf, sizeof(buf), "%.0f fps (%.1f ms)", info.uiFps, info.infMs);
            SkeetInfoRow("UI / Время", buf, IM_COL32(140, 185, 240, 255));
        }
        EndGroupBox();

        BeginGroupBox("Presets", colW);
        {
            const char* kPresets[3] = { "Legit", "Fast", "Rage" };
            if (SkeetCombo("##preset", nullptr, &presetIdx, kPresets, 3))
            {
                if (presetIdx == 0)
                {
                    s.aimStrength = 0.12f; s.smoothness = 0.22f; s.aimMaxStep = 20.0f;
                    s.aimDeadzone = 9.0f;  s.aimFov = 320;
                }
                else if (presetIdx == 1)
                {
                    s.aimStrength = 0.24f; s.smoothness = 0.10f; s.aimMaxStep = 36.0f;
                    s.aimDeadzone = 6.0f;  s.aimFov = 420;
                }
                else
                {
                    s.aimStrength = 0.45f; s.smoothness = 0.00f; s.aimMaxStep = 65.0f;
                    s.aimDeadzone = 2.0f;  s.aimFov = 640;
                }
                flashMsg = "Пресет применён"; flashT = 1.5f;
            }
            if (SkeetButton("Load"))
            {
                action = Action::Load;
                flashMsg = "Загружено из f6.cfg"; flashT = 1.5f;
            }
            if (SkeetButton("Save"))
            {
                action = Action::Save;
                flashMsg = "Сохранено в f6.cfg"; flashT = 1.5f;
            }
            if (SkeetButton("Reset"))
            {
                s = AimSettings{};
                conf.store(0.30f);
                iou.store(0.45f);
                action = Action::Save;
                flashMsg = "Сброшено по умолчанию"; flashT = 1.5f;
            }
            if (SkeetButton("Unload"))
            {
                action = Action::Unload;
            }
            if (flashT > 0.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f);
                ImGui::TextColored(ImVec4(0.60f, 0.87f, 0.16f, 1.0f), "%s", flashMsg);
            }
        }
        EndGroupBox();
        break;
    }

    case 3:   // ИНФО
    {
        char buf[64];
        ImGui::SetCursorScreenPos(ImVec2(leftX, base.y + 6.0f));
        BeginGroupBox("Telemetry", colW);
        SkeetInfoRow("Статус", info.aimHeld ? "АКТИВЕН" : "выкл",
                     info.aimHeld ? IM_COL32(154, 222, 42, 255) : IM_COL32(150, 150, 150, 255));
        std::snprintf(buf, sizeof(buf), "%d", info.targets);
        SkeetInfoRow("Целей в кадре", buf, IM_COL32(154, 222, 42, 255));
        SkeetInfoRow("Ускорение", info.gpu ? "DirectML (GPU)" : "CPU",
                     info.gpu ? IM_COL32(154, 222, 42, 255) : IM_COL32(230, 180, 80, 255));
        std::snprintf(buf, sizeof(buf), "%.0f fps", info.capFps);
        SkeetInfoRow("Захват (CAP)", buf, IM_COL32(140, 210, 240, 255));
        std::snprintf(buf, sizeof(buf), "%.0f fps", info.detFps);
        SkeetInfoRow("Детекция (DET)", buf, IM_COL32(140, 230, 140, 255));
        std::snprintf(buf, sizeof(buf), "%.2f ms", info.infMs);
        SkeetInfoRow("Время YOLO", buf, IM_COL32(230, 210, 110, 255));
        std::snprintf(buf, sizeof(buf), "%.0f fps", info.uiFps);
        SkeetInfoRow("Оверлей (UI)", buf, IM_COL32(140, 175, 240, 255));
        std::snprintf(buf, sizeof(buf), "%.3f", info.maxConf);
        SkeetInfoRow("Max conf", buf, IM_COL32(220, 220, 220, 255));
        EndGroupBox();

        ImGui::SetCursorScreenPos(ImVec2(rightX, base.y + 6.0f));
        BeginGroupBox("Controls", colW);
        SkeetInfoRow("Открыть / скрыть", "N / INSERT", IM_COL32(215, 215, 215, 255));
        SkeetInfoRow("Выгрузить софт",   "F8",         IM_COL32(215, 215, 215, 255));
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + g_box.innerW);
        ImGui::TextColored(ImVec4(0.58f, 0.58f, 0.58f, 1.0f),
            "Для отображения поверх игры без сворачивания используй в игре режим "
            "\"Без рамок\" (Borderless / Fullscreen Window).");
        ImGui::PopTextWrapPos();
        EndGroupBox();
        break;
    }
    }

    ImGui::EndChild();
    ImGui::End();

    // Ребинд клавиш: снимок зажатых, затем ловим новое нажатие.
    if (m_listening && m_listenTarget)
    {
        if (!m_listenActive)
        {
            for (int vk = 1; vk < 256; ++vk)
                m_snapshot[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
            m_listenActive = true;
        }
        else
        {
            for (int vk = 1; vk < 256; ++vk)
            {
                bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (down && !m_snapshot[vk] && IsBindable(vk))
                {
                    *m_listenTarget = vk;
                    m_listening    = false;
                    m_listenTarget = nullptr;
                    m_listenActive = false;
                    break;
                }
                m_snapshot[vk] = down ? 1 : 0;
            }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            m_listening    = false;
            m_listenTarget = nullptr;
            m_listenActive = false;
        }
    }

    ImGui::Render();

    D3D11_VIEWPORT vp{};
    vp.Width    = static_cast<float>(m_w);
    vp.Height   = static_cast<float>(m_h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);
    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
    const float clear[4] = { 0.067f, 0.067f, 0.067f, 1.0f };
    m_ctx->ClearRenderTargetView(m_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swap->Present(0, 0);
    return action;
}
