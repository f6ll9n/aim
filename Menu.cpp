#include "Menu.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <string>
#include <cstdio>

// ============================================================================
//  меню на имгуи + дх11. без рамки, тёмная тема, сегов кириллица. лезет по N,
//  пока открыта — тыкабельная.
// ============================================================================

// прокидываю обработчик вхин32, иначе windows.h лезет в хедер имгуи. не трогать
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    const wchar_t* kMenuClass = L"BionicAimMenuClass";

    // кислотный лаймовый акцент, чуть приглушённый (потемнее рефа)
    ImVec4 Accent(float a = 1.f)       { return ImVec4(0.44f, 0.62f, 0.16f, a); }
    ImVec4 AccentBright(float a = 1.f) { return ImVec4(0.58f, 0.80f, 0.26f, a); }

    // шрифт с иконками. грузится один раз, не загрузился — откат на текстовые кнопки
    ImFont* g_iconFont = nullptr;

    // жирный для заголовков/лого
    ImFont* g_boldFont = nullptr;

    // иконки фа-солид, коды в утф8
    constexpr const char* ICON_AIM    = "\xEF\x81\x9B";  // crosshairs  U+F05B
    constexpr const char* ICON_VISUAL = "\xEF\x81\xAE";  // eye         U+F06E
    constexpr const char* ICON_KEYS   = "\xEF\x84\x9C";  // keyboard    U+F11C
    constexpr const char* ICON_INFO   = "\xEF\x81\x9A";  // circle-info U+F05A

    // тащим только эти глифы, иначе атлас толстеет
    static const ImWchar kIconRanges[] =
    {
        0xF05A, 0xF05B,
        0xF06E, 0xF06E,
        0xF11C, 0xF11C,
        0,
    };

    // где exe, там и шрифт
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

    // жирный белый заголовок секции (как название панели на рефе)
    void SectionHeader(const char* title)
    {
        if (g_boldFont) ImGui::PushFont(g_boldFont);
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "%s", title);
        if (g_boldFont) ImGui::PopFont();
    }

    // панель-бокс с тонкой рамкой и белым заголовком. высота растёт сама.
    void BeginPanel(const char* title)
    {
        ImGui::BeginChild(title, ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));
        SectionHeader(title);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    void EndPanel()
    {
        ImGui::PopStyleVar();   // ItemSpacing
        ImGui::PopStyleVar();   // WindowPadding
        ImGui::EndChild();
        ImGui::Spacing();
    }

    // слайдер: серая дорожка с рамкой, зелёная заливка слева, белый хэндл
    // с тёмной обводкой по центру. тянется мышью, хэндл на границе заливки.
    bool SlideFill(const char* id, float* v, float mn, float mx)
    {
        bool changed = false;
        const float h      = 16.0f;   // высота зоны клика
        const float trackH = 6.0f;    // толщина полосы
        const float knob   = 8.0f;    // сторона белого хэндла

        ImVec2 p = ImGui::GetCursorScreenPos();
        float  w = ImGui::GetContentRegionAvail().x;

        ImGui::InvisibleButton(id, ImVec2(w, h));
        if (ImGui::IsItemActive())
        {
            float f = (ImGui::GetIO().MousePos.x - p.x) / w;
            if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
            float nv = mn + f * (mx - mn);
            if (nv != *v) { *v = nv; changed = true; }
        }
        bool active  = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float ty = p.y + (h - trackH) * 0.5f;
        ImVec2 t0(p.x, ty), t1(p.x + w, ty + trackH);

        // дорожка (тёмная + тонкая рамка)
        dl->AddRectFilled(t0, t1, ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.13f, 1.0f)), 3.0f);
        dl->AddRect(t0, t1, ImGui::GetColorU32(ImVec4(0.20f, 0.20f, 0.22f, 1.0f)), 3.0f, 0, 1.0f);

        float f = (*v - mn) / (mx - mn);
        if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
        float fillX = p.x + f * w;

        // зелёная заливка слева до хэндла
        if (fillX > p.x + 1.0f)
            dl->AddRectFilled(t0, ImVec2(fillX, ty + trackH), ImGui::GetColorU32(Accent()), 3.0f);

        // белый хэндл с тёмной обводкой, при ховере/драге чуть крупнее
        ImVec2 kc(fillX, p.y + h * 0.5f);
        float hs = (hovered || active) ? knob + 2.0f : knob;
        dl->AddRectFilled(ImVec2(kc.x - hs * 0.5f, kc.y - hs * 0.5f),
                          ImVec2(kc.x + hs * 0.5f, kc.y + hs * 0.5f),
                          IM_COL32(235, 236, 238, 255), 2.0f);
        dl->AddRect(ImVec2(kc.x - hs * 0.5f, kc.y - hs * 0.5f),
                    ImVec2(kc.x + hs * 0.5f, kc.y + hs * 0.5f),
                    IM_COL32(28, 28, 30, 255), 2.0f, 0, 1.5f);
        return changed;
    }

    // слайдер: подпись и значение сверху, полоса ниже — чтоб цифры не лезли на ползунок
    bool LabeledSliderF(const char* label, const char* id, float* v, float mn, float mx, const char* fmt)
    {
        ImGui::TextUnformatted(label);
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 vs = ImGui::CalcTextSize(buf);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - vs.x);
        ImGui::TextColored(AccentBright(), "%s", buf);
        ImGui::PushID(id);
        bool changed = SlideFill("##v", v, mn, mx);
        ImGui::PopID();
        return changed;
    }

    bool LabeledSliderI(const char* label, const char* id, int* v, int mn, int mx, const char* fmt)
    {
        ImGui::TextUnformatted(label);
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 vs = ImGui::CalcTextSize(buf);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - vs.x);
        ImGui::TextColored(AccentBright(), "%s", buf);
        ImGui::PushID(id);
        float fv = static_cast<float>(*v);
        bool changed = SlideFill("##v", &fv, static_cast<float>(mn), static_cast<float>(mx));
        if (changed) *v = static_cast<int>(fv + 0.5f);
        ImGui::PopID();
        return changed;
    }

    // квадратный чекбокс: вкл = залит кислотой, БЕЗ галочки. как на рефе.
    bool CheckBox(const char* id, const char* label, bool* v)
    {
        bool changed = false;
        const float s = 14.0f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton(id, ImVec2(s, s));
        if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
        bool hovered = ImGui::IsItemHovered();

        ImVec2 p2(p.x + s, p.y + s);
        ImVec4 fill = *v ? Accent() : ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
        dl->AddRectFilled(p, p2, ImGui::GetColorU32(fill), 2.0f);
        dl->AddRect(p, p2, ImGui::GetColorU32(ImVec4(0.22f, 0.22f, 0.24f, 1.0f)), 2.0f, 0, 1.0f);
        if (hovered)
            dl->AddRect(p, p2, IM_COL32(255, 255, 255, 30), 2.0f, 0, 1.0f);

        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p.x + s + 9.0f, p.y + (s - ts.y) * 0.5f),
                    ImGui::GetColorU32(*v ? ImVec4(0.90f, 0.92f, 0.94f, 1.0f)
                                          : ImVec4(0.70f, 0.70f, 0.72f, 1.0f)), label);
        return changed;
    }

    // вкладка навигации: прямоугольный блок, активная — белая иконка
    bool NavButton(const char* id, const char* label, const ImVec2& size, bool active)
    {
        bool clicked = false;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        clicked      = ImGui::IsItemClicked();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p2(p.x + size.x, p.y + size.y);

        ImVec4 bg, txt;
        if (active)       { bg = ImVec4(0.15f, 0.15f, 0.15f, 1.0f); txt = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); }
        else if (hovered) { bg = ImVec4(0.12f, 0.12f, 0.12f, 1.0f); txt = ImVec4(0.70f, 0.70f, 0.70f, 1.0f); }
        else              { bg = ImVec4(0.09f, 0.09f, 0.09f, 1.0f); txt = ImVec4(0.45f, 0.45f, 0.45f, 1.0f); }

        dl->AddRectFilled(p, p2, ImGui::GetColorU32(bg), 2.0f);

        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
                    ImGui::GetColorU32(txt), label);
        return clicked;
    }

}

// имя клавиши по vk
static std::string KeyName(int vk)
{
    switch (vk)
    {
    case VK_LBUTTON:  return "ЛКМ";
    case VK_RBUTTON:  return "ПКМ";
    case VK_MBUTTON:  return "СКМ";
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

// можно ли вешать на клавишу
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

// название слева, значение справа
static void InfoRow(const char* label, const char* value, ImVec4 color)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    ImGui::TextColored(color, value);
}

Menu::~Menu()
{
    Destroy();
}

LRESULT CALLBACK Menu::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 0;

    switch (msg)
    {
    case WM_NCHITTEST:
    {
        // верх — таскаем окно, кроме правого угла с крестиком
        POINT p{ static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)) };
        RECT r;
        GetWindowRect(hwnd, &r);
        constexpr int kTitleH  = 40;
        constexpr int kBtnZone = 110;
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
    RegisterClassExW(&wc);   // повторно — пофиг на ошибку

    int x = (screenW - m_w) / 2;
    int y = (screenH - m_h) / 2 - 60;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kMenuClass, L"aim menu", WS_POPUP,
        x, y, m_w, m_h,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd)
        return false;

    if (!CreateDeviceAndSwapChain())
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_ctx);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename  = nullptr;
    io.LogFilename  = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // сегов регулярный + жирный, кириллица в диапазоне
    static const ImWchar ranges[] =
    {
        0x0020, 0x00FF,   // Basic Latin + Latin-1 (включая ×)
        0x0400, 0x052F,   // Cyrillic + Cyrillic Supplement
        0x2010, 0x205E,   // тире/кавычки/многоточие
        0x25A0, 0x25FF,   // ●○ и геометрия, иначе знаки вопроса лезут
        0,
    };
    io.Fonts->Clear();
    io.FontDefault = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf", 17.0f, nullptr, ranges);
    g_boldFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 22.0f, nullptr, ranges);

    // ищем шрифт иконок рядом с exe, иначе в fonts/
    std::string faPath = ExeDir() + "fa-solid-900.ttf";
    g_iconFont = io.Fonts->AddFontFromFileTTF(faPath.c_str(), 26.0f, nullptr, kIconRanges);
    if (!g_iconFont)
        g_iconFont = io.Fonts->AddFontFromFileTTF(
            (ExeDir() + "fonts\\fa-solid-900.ttf").c_str(), 26.0f, nullptr, kIconRanges);
    if (!g_iconFont)
        g_iconFont = io.Fonts->AddFontFromFileTTF(
            "fonts\\fa-solid-900.ttf", 26.0f, nullptr, kIconRanges);

    SetStyle();
    return true;
}

void Menu::SetStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();
    // плоский минимализм: почти нет скруглений, никаких теней. как на рефе.
    st.WindowRounding    = 3.0f;
    st.ChildRounding     = 3.0f;
    st.FrameRounding     = 2.0f;
    st.PopupRounding     = 3.0f;
    st.GrabRounding      = 2.0f;
    st.TabRounding       = 3.0f;
    st.ScrollbarRounding = 2.0f;
    st.WindowBorderSize  = 0.0f;
    st.FrameBorderSize   = 1.0f;
    st.TabBorderSize     = 1.0f;
    st.WindowPadding     = ImVec2(18, 14);
    st.FramePadding      = ImVec2(12, 6);
    st.ItemSpacing       = ImVec2(10, 9);
    st.ItemInnerSpacing  = ImVec2(6, 4);
    st.ScrollbarSize     = 8.0f;
    st.GrabMinSize       = 12.0f;

    ImVec4* c = st.Colors;
    ImVec4 accent = Accent();
    ImVec4 accentBright = AccentBright();
    c[ImGuiCol_Text]                 = ImVec4(0.78f, 0.78f, 0.80f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.42f, 0.43f, 0.46f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.05f, 0.05f, 0.06f, 0.98f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.08f, 0.09f, 0.98f);
    c[ImGuiCol_Border]               = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.07f, 0.07f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.07f, 0.07f, 0.08f, 1.00f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.02f, 0.02f, 0.02f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f, 0.38f, 0.40f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);  // галочку рисуем руками
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentBright;
    c[ImGuiCol_Button]               = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = accent;
    c[ImGuiCol_SeparatorActive]      = accentBright;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.32f, 0.32f, 0.34f, 1.00f);
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
    c[ImGuiCol_TabSelected]          = accent;
    c[ImGuiCol_TabDimmed]            = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_PlotLines]            = accent;
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_TextSelectedBg]       = ImVec4(0.66f, 0.88f, 0.29f, 0.30f);
}

bool Menu::CreateDeviceAndSwapChain()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width  = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags        = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed  = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

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
    if (m_rtv)    { m_rtv->Release();       m_rtv = nullptr; }
    if (m_ctx)    { m_ctx->ClearState();    m_ctx->Release(); m_ctx = nullptr; }
    if (m_swap)   { m_swap->Release();      m_swap = nullptr; }
    if (m_device) { m_device->Release();     m_device = nullptr; }
    if (m_hwnd)   { DestroyWindow(m_hwnd);   m_hwnd = nullptr; }

    // имгуи тушим после дх11, и только если контекст был
    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void Menu::SetVisible(bool v)
{
    m_visible = v;
    if (!m_hwnd)
        return;

    if (v)
    {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        ShowWindow(m_hwnd, SW_SHOW);

        // юнити держит мышь залоченной по центру. активируем окно — мышь отдаёт,
        // как при альт-табе, но игру не сворачивает
        INPUT in[1]{};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_MENU;
        in[0].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, in, sizeof(INPUT));   // сброс лимита, чтоб SetForegroundWindow сработало
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }
    else
    {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void Menu::TextKeyBind(const char* label, int* key)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);

    char btn[64];
    if (m_listening && m_listenTarget == key)
        std::snprintf(btn, sizeof(btn), "...##kb%p", static_cast<void*>(key));
    else
        std::snprintf(btn, sizeof(btn), "%s##kb%p", KeyName(*key).c_str(),
                      static_cast<void*>(key));

    if (ImGui::Button(btn, ImVec2(100, 0)))
    {
        m_listening    = true;
        m_listenTarget = key;
        m_listenActive = false;   // снимок клавы снимем след. кадром
    }
}

void Menu::Render(AimSettings& s, std::atomic<float>& conf, const RuntimeInfo& info)
{
    if (!m_visible || !m_device || !m_swap || !m_rtv)
        return;

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

    ImGui::Begin("##aimmenu", nullptr, kFlags);

    // радужная полоска сверху — фишка рефа. во всю ширину окна.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a0 = ImGui::GetWindowPos();
        float w = ImGui::GetWindowWidth();
        const ImU32 cols[5] = {
            IM_COL32(86, 165, 255, 255),   // синий
            IM_COL32(150, 95, 240, 255),   // фиолет
            IM_COL32(240, 95, 180, 255),   // розовый
            IM_COL32(255, 150, 60, 255),   // оранж
            IM_COL32(200, 220, 65, 255),   // лайм
        };
        float seg = w / 5.0f;
        for (int i = 0; i < 5; ++i)
            dl->AddRectFilled(ImVec2(a0.x + seg * i, a0.y),
                              ImVec2(a0.x + seg * (i + 1), a0.y + 3.0f), cols[i]);
    }

    // шапка
    {
        ImGui::SetCursorPos(ImVec2(22, 18));
        if (g_iconFont)
        {
            ImGui::PushFont(g_iconFont);
            ImGui::TextColored(AccentBright(), ICON_AIM);
            ImGui::PopFont();
            ImGui::SameLine(0.0f, 10.0f);
        }

        if (g_boldFont) ImGui::PushFont(g_boldFont);
        ImGui::TextColored(ImVec4(0.94f, 0.96f, 0.98f, 1.0f), "f6");
        if (g_boldFont) ImGui::PopFont();

        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(ImVec4(0.38f, 0.41f, 0.48f, 1.0f), "aim");

        // статус-пилл: зелёная точка = айм активен
        float pillW = 60.0f, pillH = 22.0f;
        ImVec2 pp(m_w - pillW - 52, 18);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pp, ImVec2(pp.x + pillW, pp.y + pillH),
                          info.aimHeld ? ImGui::GetColorU32(Accent(0.18f))
                                       : IM_COL32(255, 255, 255, 12),
                          6.0f);
        dl->AddCircleFilled(ImVec2(pp.x + 13, pp.y + pillH * 0.5f), 3.0f,
                            info.aimHeld ? ImGui::GetColorU32(AccentBright())
                                         : IM_COL32(130, 133, 140, 255));
        dl->AddText(ImVec2(pp.x + 21, pp.y + 3.5f),
                    info.aimHeld ? ImGui::GetColorU32(AccentBright())
                                 : IM_COL32(150, 153, 160, 255),
                    "АИМ");

        // крестик, ровно по центру, при наведении красный
        float bsz = 26.0f;
        ImVec2 cp(m_w - bsz - 16, 16);
        ImGui::SetCursorPos(ImVec2(cp.x, cp.y));
        ImGui::InvisibleButton("##close", ImVec2(bsz, bsz));
        bool closeHover = ImGui::IsItemHovered();
        if (closeHover)
            dl->AddRectFilled(cp, ImVec2(cp.x + bsz, cp.y + bsz),
                              ImGui::GetColorU32(ImVec4(0.85f, 0.25f, 0.25f, 1.0f)), 6.0f);
        ImVec2 xs = ImGui::CalcTextSize("×");
        dl->AddText(ImVec2(cp.x + (bsz - xs.x) * 0.5f, cp.y + (bsz - xs.y) * 0.5f),
                    ImGui::GetColorU32(closeHover ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                                  : ImVec4(0.60f, 0.63f, 0.70f, 1.0f)),
                    "×");
        if (ImGui::IsItemClicked())
            SetVisible(false);
    }
    ImGui::Spacing();
    ImGui::Separator();

    // слева сайдбар, справа контент
    static int section = 0;             // живёт между кадрами. НЕ ТРОГАЙ
    static int   prevSection = 0;       // для фейда на переключение
    static float contentAlpha = 1.0f;

    {
        float dt = ImGui::GetIO().DeltaTime;
        if (section != prevSection) { contentAlpha = 0.0f; prevSection = section; }
        float kf = dt * 10.0f; if (kf > 1.0f) kf = 1.0f;
        contentAlpha += (1.0f - contentAlpha) * kf;
    }

    const char* kTabs[4]  = { "AIM", "ВИЗУАЛ", "КЛАВИШИ", "ИНФО" };
    const char* kIcons[4] = { ICON_AIM, ICON_VISUAL, ICON_KEYS, ICON_INFO };

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 8));
    ImGui::BeginChild("##side", ImVec2(90, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 kBtn(74, 38);
    for (int i = 0; i < 4; ++i)
    {
        ImGui::SetCursorPosX((90.0f - kBtn.x) * 0.5f);

        bool sel = (i == section);
        const char* label = g_iconFont ? kIcons[i] : kTabs[i];
        if (g_iconFont)
            ImGui::PushFont(g_iconFont);
        ImGui::PushID(i);
        if (NavButton("##nav", label, kBtn, sel))
            section = i;
        ImGui::PopID();
        if (g_iconFont)
            ImGui::PopFont();

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", kTabs[i]);
        ImGui::Dummy(ImVec2(0, 4));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine();

    // полоса между сайдбаром и контентом
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 v = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(v.x - 4, v.y), ImVec2(v.x - 4, v.y + m_h - 84),
                    IM_COL32(34, 37, 46, 255), 1.0f);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
    ImGui::BeginChild("##content", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2(0, 10));
    switch (section)
    {
    case 0:   // AIM
    {
        BeginPanel("Наведение");
        LabeledSliderF("Высота (голова)", "headFrac", &s.headFrac,
                       AimSettings::HD_MIN, AimSettings::HD_MAX, "%.2f");
        ImGui::Spacing();
        // проверка что это реально голова, а не мусор. работает.
        // ниже порога целимся в тело, а не дёргаемся в фантом
        LabeledSliderF("Точность головы (в разработке)", "headConf", &s.headConf,
                       AimSettings::HC_MIN, AimSettings::HC_MAX, "%.2f");
        ImGui::Spacing();
        LabeledSliderF("Плавность", "smooth", &s.smoothness,
                       AimSettings::SM_MIN, AimSettings::SM_MAX, "%.2f");
        EndPanel();

        BeginPanel("Зона наводки");
        LabeledSliderI("FOV аима", "aimFov", &s.aimFov,
                       AimSettings::AIMFOV_MIN, AimSettings::AIMFOV_MAX, "%d px");
        EndPanel();

        BeginPanel("Детекция");
        {
            float c = conf.load();
            if (LabeledSliderF("Точность проверки", "conf", &c, 0.05f, 0.95f, "%.2f"))
                conf.store(c);
        }
        EndPanel();
        break;
    }
    case 1:   // ВИЗУАЛ
    {
        BeginPanel("Индикаторы");
        CheckBox("##t_cross", "Крестик", &s.showCrosshair);
        ImGui::Spacing();
        CheckBox("##t_boxes", "Боксы целей", &s.showBoxes);
        ImGui::Spacing();
        CheckBox("##t_line", "Линия до цели", &s.showLine);
        ImGui::Spacing();
        CheckBox("##t_log", "Лог-панель", &s.showLog);
        EndPanel();

        BeginPanel("Оверлей");
        CheckBox("##t_fov", "Зона наводки (FOV)", &s.showFov);
        EndPanel();
        break;
    }
    case 2:   // КЛАВИШИ
    {
        BeginPanel("Аим");
        TextKeyBind("Клавиша аима 1", &s.aimKey1);
        ImGui::Spacing();
        TextKeyBind("Клавиша аима 2", &s.aimKey2);
        EndPanel();

        BeginPanel("Управление");
        ImGui::TextWrapped("Открытие/закрытие меню:   N");
        ImGui::Spacing();
        ImGui::TextWrapped("Выход из приложения:      F8");
        EndPanel();
        break;
    }
    case 3:   // ИНФО
    {
        char buf[48];
        BeginPanel("Текущее состояние");
        InfoRow("Статус", info.aimHeld ? "АКТИВЕН" : "выкл",
                info.aimHeld ? ImVec4(0.2f, 1.0f, 0.5f, 1.f) : ImVec4(0.6f, 0.6f, 0.6f, 1.f));
        std::snprintf(buf, sizeof(buf), "%d", info.targets);
        InfoRow("Целей", buf, AccentBright());
        InfoRow("Режим", info.gpu ? "DML (GPU)" : "CPU",
                info.gpu ? AccentBright() : ImVec4(0.9f, 0.7f, 0.3f, 1.f));
        EndPanel();

        BeginPanel("Производительность");
        std::snprintf(buf, sizeof(buf), "%.0f", info.detFps);
        InfoRow("DET fps", buf, ImVec4(0.45f, 0.9f, 0.45f, 1.f));
        std::snprintf(buf, sizeof(buf), "%.0f", info.uiFps);
        InfoRow("UI fps", buf, ImVec4(0.45f, 0.65f, 0.9f, 1.f));
        std::snprintf(buf, sizeof(buf), "%.3f", info.maxConf);
        InfoRow("Max conf", buf, ImVec4(0.9f, 0.9f, 0.9f, 1.f));
        EndPanel();
        break;
    }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();   // WindowPadding
    ImGui::PopStyleVar();   // Alpha

    ImGui::End();

    // ребинд: снимаю снимок зажатых, потом жду НОВОГО нажатия. иначе ловит клик,
    // которым ты окно и открыл. магия, не трогай
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
                    m_listening = false;
                    m_listenTarget = nullptr;
                    m_listenActive = false;
                    break;
                }
                m_snapshot[vk] = down ? 1 : 0;
            }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            m_listening = false;
            m_listenTarget = nullptr;
            m_listenActive = false;
        }
    }

    ImGui::Render();

    // рендер в свопчейн
    D3D11_VIEWPORT vp{};
    vp.Width  = static_cast<float>(m_w);
    vp.Height = static_cast<float>(m_h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);
    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
    const float clear[4] = { 0.05f, 0.06f, 0.08f, 1.0f };
    m_ctx->ClearRenderTargetView(m_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swap->Present(1, 0);
}
