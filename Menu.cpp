#include "Menu.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <unordered_map>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    const wchar_t* kMenuClass = L"BionicAimMenuClass";

    ImFont* g_iconFont  = nullptr;
    ImFont* g_boldFont  = nullptr;
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

    // Экспоненциальное сглаживание (не зависит от FPS).
    inline float SmoothExp(float cur, float target, float speed, float dt)
    {
        float k = 1.0f - std::exp(-speed * std::clamp(dt, 0.0f, 0.1f));
        return cur + (target - cur) * k;
    }

    inline ImU32 LerpCol(ImU32 a, ImU32 b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        int ar = (a >>  0) & 0xFF, ag = (a >>  8) & 0xFF, ab = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
        int br = (b >>  0) & 0xFF, bg = (b >>  8) & 0xFF, bb = (b >> 16) & 0xFF, ba = (b >> 24) & 0xFF;
        int r = ar + static_cast<int>((br - ar) * t);
        int g = ag + static_cast<int>((bg - ag) * t);
        int bl = ab + static_cast<int>((bb - ab) * t);
        int al = aa + static_cast<int>((ba - aa) * t);
        return IM_COL32(r, g, bl, al);
    }

    // Персистентное состояние плавных анимаций виджетов (по ImGuiID).
    static std::unordered_map<ImGuiID, float> g_animState;
    inline float AnimVal(const char* idStr, float target, float speed = 16.0f)
    {
        ImGuiID id = ImGui::GetID(idStr);
        auto it = g_animState.find(id);
        float cur = (it == g_animState.end()) ? target : it->second;
        cur = SmoothExp(cur, target, speed, ImGui::GetIO().DeltaTime);
        if (std::abs(cur - target) < 0.001f) cur = target;
        g_animState[id] = cur;
        return cur;
    }

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

        const float ga = ImGui::GetStyle().Alpha;
        const int a255 = static_cast<int>(255.0f * ga);
        const int a230 = static_cast<int>(230.0f * ga);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, a255));

        dl->ChannelsSetCurrent(1);
        dl->AddRect(p0, p1, IM_COL32(42, 42, 42, a255));
        dl->AddRect(ImVec2(p0.x + 1.0f, p0.y + 1.0f),
                    ImVec2(p1.x - 1.0f, p1.y - 1.0f),
                    IM_COL32(10, 10, 10, a255));

        if (g_smallBold) ImGui::PushFont(g_smallBold);
        ImVec2 ts = ImGui::CalcTextSize(g_box.title);
        float tx = p0.x + 12.0f;
        float ty = p0.y - ts.y * 0.5f;
        dl->AddRectFilled(ImVec2(tx - 4.0f, p0.y - 2.0f),
                          ImVec2(tx + ts.x + 4.0f, p0.y + 3.0f),
                          IM_COL32(20, 20, 20, a255));
        dl->AddText(ImVec2(tx + 1.0f, ty + 1.0f), IM_COL32(0, 0, 0, a230), g_box.title);
        dl->AddText(ImVec2(tx, ty), IM_COL32(226, 226, 226, a255), g_box.title);
        if (g_smallBold) ImGui::PopFont();

        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 14.0f));
    }

    // Чекбокс 9x9 с плавным переливом заливки и hover-подсветкой.
    bool SkeetCheck(const char* id, const char* label, bool* v)
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

        const float a = ImGui::GetStyle().Alpha;
        char animKey[96];
        std::snprintf(animKey, sizeof(animKey), "%s_on", id);
        float tOn = AnimVal(animKey, *v ? 1.0f : 0.0f, 18.0f);
        std::snprintf(animKey, sizeof(animKey), "%s_hov", id);
        float tHov = AnimVal(animKey, hovered ? 1.0f : 0.0f, 20.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float by = p.y + (rowH - boxSz) * 0.5f;
        ImVec2 b0(p.x, by), b1(p.x + boxSz, by + boxSz);

        ImU32 topC = LerpCol(IM_COL32(48, 48, 48, 255), IM_COL32(154, 222, 42, 255), tOn);
        ImU32 botC = LerpCol(IM_COL32(34, 34, 34, 255), IM_COL32(104, 154, 22, 255), tOn);
        topC = (topC & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        botC = (botC & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        dl->AddRectFilledMultiColor(b0, b1, topC, topC, botC, botC);

        ImU32 brd = LerpCol(IM_COL32(8, 8, 8, 255), IM_COL32(85, 85, 85, 255), tHov);
        brd = (brd & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        dl->AddRect(ImVec2(b0.x - 1.0f, b0.y - 1.0f),
                    ImVec2(b1.x + 1.0f, b1.y + 1.0f), brd);

        ImU32 txtCol = LerpCol(IM_COL32(165, 165, 165, 255), IM_COL32(222, 222, 222, 255), std::max(tOn, tHov * 0.6f));
        txtCol = (txtCol & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        dl->AddText(ImVec2(p.x + boxSz + padX + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(190 * a)), label);
        dl->AddText(ImVec2(p.x + boxSz + padX, p.y), txtCol, label);
        return changed;
    }

    // Слайдер в стиле рефа с плавно скользящей заливкой.
    bool SkeetSliderF(const char* label, const char* id, float* v, float mn, float mx, const char* fmt)
    {
        bool changed = false;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float a = ImGui::GetStyle().Alpha;

        ImVec2 lp = ImGui::GetCursorScreenPos();
        float xOff = lp.x + 20.0f;
        float w    = g_box.innerW - 20.0f;

        dl->AddText(ImVec2(xOff + 1.0f, lp.y + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(190 * a)), label);
        dl->AddText(ImVec2(xOff, lp.y), IM_COL32(204, 204, 204, static_cast<int>(255 * a)), label);

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

        char animKey[96];
        std::snprintf(animKey, sizeof(animKey), "%s_frac", id);
        float targetFrac = std::clamp((*v - mn) / (mx - mn), 0.0f, 1.0f);
        float visFrac    = ImGui::IsItemActive() ? targetFrac : AnimVal(animKey, targetFrac, 20.0f);
        if (ImGui::IsItemActive()) g_animState[ImGui::GetID(animKey)] = targetFrac;

        std::snprintf(animKey, sizeof(animKey), "%s_hov", id);
        float tHov = AnimVal(animKey, hovered ? 1.0f : 0.0f, 18.0f);

        const int a255 = static_cast<int>(255 * a);
        ImVec2 t0(xOff, barY), t1(xOff + w, barY + barH);
        dl->AddRectFilledMultiColor(t0, t1,
            IM_COL32(34, 34, 34, a255), IM_COL32(34, 34, 34, a255),
            IM_COL32(46, 46, 46, a255), IM_COL32(46, 46, 46, a255));

        float fillX = xOff + visFrac * w;
        if (fillX > xOff + 1.0f)
        {
            dl->AddRectFilledMultiColor(t0, ImVec2(fillX, barY + barH),
                IM_COL32(154, 222, 42, a255), IM_COL32(154, 222, 42, a255),
                IM_COL32(102, 152, 20, a255), IM_COL32(102, 152, 20, a255));
        }
        ImU32 brd = LerpCol(IM_COL32(8, 8, 8, 255), IM_COL32(78, 78, 78, 255), tHov);
        brd = (brd & 0x00FFFFFF) | (static_cast<ImU32>(a255) << 24);
        dl->AddRect(ImVec2(t0.x - 1.0f, t0.y - 1.0f), ImVec2(t1.x + 1.0f, t1.y + 1.0f), brd);

        dl->AddText(ImVec2(xOff - 8.0f, barY - 4.0f), IM_COL32(100, 100, 100, a255), "-");
        dl->AddText(ImVec2(xOff + w + 3.0f, barY - 4.0f), IM_COL32(100, 100, 100, a255), "+");

        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 vs = ImGui::CalcTextSize(buf);
        float vx = std::clamp(fillX - vs.x * 0.5f, xOff, xOff + w - vs.x);
        float vy = barY + (barH - vs.y) * 0.5f;
        dl->AddText(ImVec2(vx + 1.0f, vy + 1.0f), IM_COL32(0, 0, 0, a255), buf);
        dl->AddText(ImVec2(vx - 1.0f, vy),        IM_COL32(0, 0, 0, static_cast<int>(200 * a)), buf);
        dl->AddText(ImVec2(vx, vy),               IM_COL32(236, 236, 236, a255), buf);

        ImGui::SetCursorScreenPos(ImVec2(lp.x, barY + barH + 7.0f));
        return changed;
    }

    bool SkeetSliderI(const char* label, const char* id, int* v, int mn, int mx, const char* fmt, const char* maxText = nullptr)
    {
        float fv = static_cast<float>(*v);
        bool changed = false;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float a = ImGui::GetStyle().Alpha;

        ImVec2 lp = ImGui::GetCursorScreenPos();
        float xOff = lp.x + 20.0f;
        float w    = g_box.innerW - 20.0f;

        dl->AddText(ImVec2(xOff + 1.0f, lp.y + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(190 * a)), label);
        dl->AddText(ImVec2(xOff, lp.y), IM_COL32(204, 204, 204, static_cast<int>(255 * a)), label);

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

        char animKey[96];
        std::snprintf(animKey, sizeof(animKey), "%s_frac", id);
        float targetFrac = std::clamp((fv - mn) / static_cast<float>(mx - mn), 0.0f, 1.0f);
        float visFrac    = ImGui::IsItemActive() ? targetFrac : AnimVal(animKey, targetFrac, 20.0f);
        if (ImGui::IsItemActive()) g_animState[ImGui::GetID(animKey)] = targetFrac;

        std::snprintf(animKey, sizeof(animKey), "%s_hov", id);
        float tHov = AnimVal(animKey, hovered ? 1.0f : 0.0f, 18.0f);

        const int a255 = static_cast<int>(255 * a);
        ImVec2 t0(xOff, barY), t1(xOff + w, barY + barH);
        dl->AddRectFilledMultiColor(t0, t1,
            IM_COL32(34, 34, 34, a255), IM_COL32(34, 34, 34, a255),
            IM_COL32(46, 46, 46, a255), IM_COL32(46, 46, 46, a255));

        float fillX = xOff + visFrac * w;
        if (fillX > xOff + 1.0f)
        {
            dl->AddRectFilledMultiColor(t0, ImVec2(fillX, barY + barH),
                IM_COL32(154, 222, 42, a255), IM_COL32(154, 222, 42, a255),
                IM_COL32(102, 152, 20, a255), IM_COL32(102, 152, 20, a255));
        }
        ImU32 brd = LerpCol(IM_COL32(8, 8, 8, 255), IM_COL32(78, 78, 78, 255), tHov);
        brd = (brd & 0x00FFFFFF) | (static_cast<ImU32>(a255) << 24);
        dl->AddRect(ImVec2(t0.x - 1.0f, t0.y - 1.0f), ImVec2(t1.x + 1.0f, t1.y + 1.0f), brd);

        dl->AddText(ImVec2(xOff - 8.0f, barY - 4.0f), IM_COL32(100, 100, 100, a255), "-");
        dl->AddText(ImVec2(xOff + w + 3.0f, barY - 4.0f), IM_COL32(100, 100, 100, a255), "+");

        char buf[32];
        if (maxText && *v >= mx)
            std::snprintf(buf, sizeof(buf), "%s", maxText);
        else
            std::snprintf(buf, sizeof(buf), fmt, *v);

        ImVec2 vs = ImGui::CalcTextSize(buf);
        float vx = std::clamp(fillX - vs.x * 0.5f, xOff, xOff + w - vs.x);
        float vy = barY + (barH - vs.y) * 0.5f;
        dl->AddText(ImVec2(vx + 1.0f, vy + 1.0f), IM_COL32(0, 0, 0, a255), buf);
        dl->AddText(ImVec2(vx - 1.0f, vy),        IM_COL32(0, 0, 0, static_cast<int>(200 * a)), buf);
        dl->AddText(ImVec2(vx, vy),               IM_COL32(236, 236, 236, a255), buf);

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

        const float a = ImGui::GetStyle().Alpha;
        char animKey[96];
        std::snprintf(animKey, sizeof(animKey), "%s_btn", label);
        float tHov = AnimVal(animKey, active ? 0.0f : (hovered ? 1.0f : 0.35f), 20.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 b0(xOff, p.y), b1(xOff + w, p.y + height);

        ImU32 topC = LerpCol(IM_COL32(28, 28, 28, 255), IM_COL32(48, 48, 48, 255), tHov);
        ImU32 botC = LerpCol(IM_COL32(20, 20, 20, 255), IM_COL32(34, 34, 34, 255), tHov);
        topC = (topC & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        botC = (botC & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);

        dl->AddRectFilledMultiColor(b0, b1, topC, topC, botC, botC);
        ImU32 innerBrd = LerpCol(IM_COL32(46, 46, 46, 255), IM_COL32(78, 92, 42, 255), hovered ? tHov * 0.7f : 0.0f);
        innerBrd = (innerBrd & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        dl->AddRect(b0, b1, innerBrd);
        dl->AddRect(ImVec2(b0.x - 1.0f, b0.y - 1.0f),
                    ImVec2(b1.x + 1.0f, b1.y + 1.0f),
                    IM_COL32(8, 8, 8, static_cast<int>(255 * a)));

        if (g_smallBold) ImGui::PushFont(g_smallBold);
        ImVec2 ts = ImGui::CalcTextSize(label);
        float tx = xOff + (w - ts.x) * 0.5f;
        float ty = p.y  + (height - ts.y) * 0.5f;
        dl->AddText(ImVec2(tx + 1.0f, ty + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(220 * a)), label);
        dl->AddText(ImVec2(tx, ty), IM_COL32(220, 220, 220, static_cast<int>(255 * a)), label);
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
        const float a = ImGui::GetStyle().Alpha;
        const int a255 = static_cast<int>(255 * a);

        if (label && label[0])
        {
            dl->AddText(ImVec2(xOff + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(190 * a)), label);
            dl->AddText(ImVec2(xOff, p.y), IM_COL32(204, 204, 204, a255), label);
            p.y += ImGui::GetTextLineHeight() + 3.0f;
        }

        const float h = 21.0f;
        ImGui::SetCursorScreenPos(ImVec2(xOff, p.y));
        if (ImGui::InvisibleButton(id, ImVec2(w, h)))
            ImGui::OpenPopup(id);
        bool hovered = ImGui::IsItemHovered();

        char animKey[96];
        std::snprintf(animKey, sizeof(animKey), "%s_hov", id);
        float tHov = AnimVal(animKey, hovered ? 1.0f : 0.0f, 18.0f);

        ImVec2 b0(xOff, p.y), b1(xOff + w, p.y + h);
        ImU32 topC = LerpCol(IM_COL32(32, 32, 32, 255), IM_COL32(42, 42, 42, 255), tHov);
        topC = (topC & 0x00FFFFFF) | (static_cast<ImU32>(a255) << 24);
        ImU32 botC = IM_COL32(24, 24, 24, a255);
        dl->AddRectFilledMultiColor(b0, b1, topC, topC, botC, botC);
        dl->AddRect(b0, b1, IM_COL32(46, 46, 46, a255));
        dl->AddRect(ImVec2(b0.x - 1.0f, b0.y - 1.0f),
                    ImVec2(b1.x + 1.0f, b1.y + 1.0f),
                    IM_COL32(8, 8, 8, a255));

        const char* curTxt = (*current >= 0 && *current < count) ? items[*current] : "";
        ImVec2 ts = ImGui::CalcTextSize(curTxt);
        dl->AddText(ImVec2(xOff + 8.0f, p.y + (h - ts.y) * 0.5f),
                    IM_COL32(196, 196, 196, a255), curTxt);

        float ax = b1.x - 11.0f, ay = p.y + h * 0.5f - 1.5f;
        dl->AddTriangleFilled(ImVec2(ax - 3.5f, ay),
                              ImVec2(ax + 3.5f, ay),
                              ImVec2(ax, ay + 4.0f),
                              IM_COL32(140, 140, 140, a255));

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
        const float a = ImGui::GetStyle().Alpha;
        const int a255 = static_cast<int>(255 * a);

        dl->AddText(ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(190 * a)), label);
        dl->AddText(ImVec2(p.x, p.y), IM_COL32(175, 175, 175, a255), label);

        float vx = p.x + g_box.innerW - vs.x;
        ImU32 vc = (valCol & 0x00FFFFFF) | (static_cast<ImU32>(a255) << 24);
        dl->AddText(ImVec2(vx + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(210 * a)), value);
        dl->AddText(ImVec2(vx, p.y), vc, value);

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

    // WS_EX_LAYERED позволяет плавно анимировать прозрачность окна при открытии/закрытии.
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kMenuClass, L"aim menu", WS_POPUP,
        x, y, m_w, m_h,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd)
        return false;

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
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
    m_grabFocus = grabFocus;

    if (v)
    {
        m_visible = true;
        m_closing = false;
        if (!m_hwnd) return;

        ClipCursor(nullptr);
        SetLayeredWindowAttributes(m_hwnd, 0, static_cast<BYTE>(std::clamp(m_openAnim, 0.05f, 1.0f) * 255.0f), LWA_ALPHA);

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
        m_listening    = false;
        m_listenTarget = nullptr;
        m_listenActive = false;
        if (m_visible)
            m_closing = true;   // плавно уезжаем в прозрачность, затем прячем HWND
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

    const float a = ImGui::GetStyle().Alpha;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = (m_listening && m_listenTarget == key) ? IM_COL32(154, 222, 42, 255)
              : hovered                                ? IM_COL32(220, 220, 220, 255)
                                                       : IM_COL32(125, 125, 125, 255);
    col = (col & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
    dl->AddText(ImVec2(tagX + 1.0f, tagY + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(200 * a)), tag);
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

    const float dt = ImGui::GetIO().DeltaTime;
    m_openAnim = SmoothExp(m_openAnim, m_closing ? 0.0f : 1.0f, 18.0f, dt);

    if (m_closing && m_openAnim <= 0.02f)
    {
        m_openAnim = 0.0f;
        m_closing  = false;
        m_visible  = false;
        if (m_hwnd)
            ShowWindow(m_hwnd, SW_HIDE);
        ImGui::EndFrame();
        return action;
    }

    if (m_hwnd)
    {
        BYTE winAlpha = static_cast<BYTE>(std::clamp(m_openAnim * 255.0f, 5.0f, 255.0f));
        SetLayeredWindowAttributes(m_hwnd, 0, winAlpha, LWA_ALPHA);
    }

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

    // 1) Двойная тёмная рамка окна
    winDl->AddRect(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 8, 8, 255));
    winDl->AddRect(ImVec2(1, 1), ImVec2(W - 1, H - 1), IM_COL32(46, 46, 46, 255));
    winDl->AddRect(ImVec2(2, 2), ImVec2(W - 2, H - 2), IM_COL32(12, 12, 12, 255));

    // 2) Плавно переливающаяся радужная полоса сверху
    {
        static float rainbowPhase = 0.0f;
        rainbowPhase += dt * 0.35f;
        if (rainbowPhase > 1.0f) rainbowPhase -= 1.0f;

        auto rgbAt = [&](float u) -> ImU32
        {
            float p = std::fmod(u + rainbowPhase, 1.0f) * 3.0f;
            float r = 0, g = 0, b = 0;
            if (p < 1.0f)      { r = 55 + 103 * p;      g = 177 - 91 * p;     b = 218 + 6 * p; }
            else if (p < 2.0f) { float k = p - 1.0f; r = 158 + 59 * k; g = 86 - 18 * k;  b = 224 - 86 * k; }
            else               { float k = p - 2.0f; r = 217 - 13 * k; g = 68 + 159 * k; b = 138 - 85 * k; }
            return IM_COL32(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), 255);
        };

        const float y0 = 3.0f, y1 = 5.0f;
        const float x0 = 3.0f, x1 = W - 3.0f;
        constexpr int kSegs = 12;
        const float seg = (x1 - x0) / kSegs;
        for (int i = 0; i < kSegs; ++i)
        {
            ImU32 cl = rgbAt(static_cast<float>(i) / kSegs);
            ImU32 cr = rgbAt(static_cast<float>(i + 1) / kSegs);
            winDl->AddRectFilledMultiColor(
                ImVec2(x0 + seg * i, y0), ImVec2(x0 + seg * (i + 1), y1),
                cl, cr, cr, cl);
        }
        winDl->AddLine(ImVec2(x0, y1), ImVec2(x1, y1), IM_COL32(8, 8, 8, 255));
    }

    // 3) Сайдбар слева с плавно скользящей подсветкой активной вкладки
    static int   section      = 0;
    static int   prevSection  = 0;
    static float tabSlideY    = -1.0f;
    static float contentAlpha = 1.0f;
    static float contentSlide = 0.0f;
    static int   presetIdx    = 0;
    static float flashT       = 0.0f;
    static const char* flashMsg = "";

    if (flashT > 0.0f) flashT = (flashT > dt) ? (flashT - dt) : 0.0f;

    if (section != prevSection)
    {
        contentAlpha = 0.0f;
        contentSlide = 10.0f;
        prevSection  = section;
    }
    contentAlpha = SmoothExp(contentAlpha, 1.0f, 16.0f, dt);
    contentSlide = SmoothExp(contentSlide, 0.0f, 16.0f, dt);

    constexpr float kSideW = 74.0f;
    const float contentTop = 6.0f;
    const float bodyH      = H - contentTop - 3.0f;
    const float tabStartY  = contentTop + 26.0f;
    const float tabH       = 72.0f;

    if (tabSlideY < 0.0f) tabSlideY = tabStartY + section * tabH;
    tabSlideY = SmoothExp(tabSlideY, tabStartY + section * tabH, 18.0f, dt);

    // Фон сайдбара + разделитель
    winDl->AddRectFilled(ImVec2(3.0f, contentTop), ImVec2(3.0f + kSideW, H - 3.0f),
                         IM_COL32(12, 12, 12, 255));
    winDl->AddLine(ImVec2(3.0f + kSideW, contentTop), ImVec2(3.0f + kSideW, H - 3.0f),
                   IM_COL32(38, 38, 38, 255));

    // Скользящий блок активной вкладки (вырез в рамке)
    {
        float ay0 = tabSlideY;
        float ay1 = ay0 + tabH;
        winDl->AddRectFilled(ImVec2(3.0f, ay0), ImVec2(3.0f + kSideW + 1.0f, ay1),
                             IM_COL32(17, 17, 17, 255));
        winDl->AddLine(ImVec2(3.0f, ay0), ImVec2(3.0f + kSideW, ay0), IM_COL32(38, 38, 38, 255));
        winDl->AddLine(ImVec2(3.0f, ay1), ImVec2(3.0f + kSideW, ay1), IM_COL32(38, 38, 38, 255));
        winDl->AddLine(ImVec2(3.0f, ay0 + 1.0f), ImVec2(3.0f + kSideW, ay0 + 1.0f), IM_COL32(8, 8, 8, 255));
        winDl->AddLine(ImVec2(3.0f, ay1 - 1.0f), ImVec2(3.0f + kSideW, ay1 - 1.0f), IM_COL32(8, 8, 8, 255));
        // Тонкий лаймовый акцент слева от активной вкладки
        winDl->AddRectFilled(ImVec2(3.0f, ay0 + 14.0f), ImVec2(5.0f, ay1 - 14.0f),
                             IM_COL32(140, 206, 36, 220));
    }

    const char* kTabs[4]  = { "Аимбот", "Визуал", "Настройки", "Инфо" };
    const char* kIcons[4] = { ICON_AIM, ICON_VISUAL, ICON_GEARS, ICON_USER };

    for (int i = 0; i < 4; ++i)
    {
        float ty0 = tabStartY + i * tabH;
        ImGui::SetCursorScreenPos(ImVec2(3.0f, ty0));
        char tid[16];
        std::snprintf(tid, sizeof(tid), "##tab%d", i);
        if (ImGui::InvisibleButton(tid, ImVec2(kSideW, tabH)))
            section = i;
        bool hovered = ImGui::IsItemHovered();
        bool active  = (section == i);

        char animKey[24];
        std::snprintf(animKey, sizeof(animKey), "tab_col_%d", i);
        float tCol = AnimVal(animKey, active ? 1.0f : (hovered ? 0.45f : 0.0f), 16.0f);

        const char* lbl = g_iconFont ? kIcons[i] : kTabs[i];
        if (g_iconFont) ImGui::PushFont(g_iconFont);
        ImVec2 is = ImGui::CalcTextSize(lbl);
        ImU32 col = LerpCol(IM_COL32(88, 88, 88, 255), IM_COL32(238, 238, 238, 255), tCol);
        float ix = 3.0f + (kSideW - is.x) * 0.5f;
        float iy = ty0  + (tabH  - is.y) * 0.5f;
        winDl->AddText(ImVec2(ix + 1.0f, iy + 1.0f), IM_COL32(0, 0, 0, 220), lbl);
        winDl->AddText(ImVec2(ix, iy), col, lbl);
        if (g_iconFont) ImGui::PopFont();

        if (hovered)
            ImGui::SetTooltip("%s", kTabs[i]);
    }

    // Кнопка закрытия '×'
    {
        float bsz = 20.0f;
        ImVec2 cp(W - bsz - 8.0f, 9.0f);
        ImGui::SetCursorScreenPos(cp);
        ImGui::InvisibleButton("##close", ImVec2(bsz, bsz));
        bool ch = ImGui::IsItemHovered();
        float tClose = AnimVal("close_hov", ch ? 1.0f : 0.0f, 20.0f);
        ImVec2 xs = ImGui::CalcTextSize("×");
        winDl->AddText(ImVec2(cp.x + (bsz - xs.x) * 0.5f, cp.y + (bsz - xs.y) * 0.5f),
                       LerpCol(IM_COL32(115, 115, 115, 255), IM_COL32(240, 90, 90, 255), tClose), "×");
        if (ImGui::IsItemClicked())
            SetVisible(false, m_grabFocus);
    }

    // 4) Контентная область с плавным fade-in + slide при смене вкладки
    const float contentX = 3.0f + kSideW + 18.0f;
    const float contentW = W - contentX - 18.0f;
    const float colGap   = 16.0f;
    const float colW     = (contentW - colGap) * 0.5f;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha);
    ImGui::SetCursorScreenPos(ImVec2(contentX, contentTop + 18.0f + contentSlide));
    ImGui::BeginChild("##content", ImVec2(contentW + 8.0f, bodyH - 22.0f), false);

    const ImVec2 base   = ImGui::GetCursorScreenPos();
    const float  leftX  = base.x;
    const float  rightX = base.x + colW + colGap;

    switch (section)
    {
    case 0:   // АИМБОТ
    {
        ImGui::SetCursorScreenPos(ImVec2(leftX, base.y + 6.0f));
        BeginGroupBox("Aimbot", colW);
        SkeetCheck("##aim_en", "Включить аим", &s.aimEnabled);
        KeyBindTag(&s.aimKey1, false);
        SkeetCheck("##aim_sticky", "Лок на одной цели", &s.stickyLock);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Держит одну цель, пока она жива в FOV (не прыгает на других).\nКогда цель умирает/пропадает — автосвап на следующую без отпускания клавиши.");
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

        ImGui::SetCursorScreenPos(ImVec2(rightX, base.y + 6.0f));
        BeginGroupBox("Target & Detection", colW);
        {
            const char* kHeadModes[3] = { "Голова -> тело", "Только голова", "Только тело" };
            SkeetCombo("##headMode", "Режим наведения", &s.headMode, kHeadModes, 3);
        }
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
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, contentAlpha), "Основная клавиша");
            KeyBindTag(&s.aimKey1, false);
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + ImGui::GetTextLineHeight() + 6.0f));

            rp = ImGui::GetCursorScreenPos();
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, contentAlpha), "Доп. клавиша");
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
        SkeetCheck("##v_heads", "Боксы голов", &s.showHeadBoxes);
        SkeetCheck("##v_line",  "Линия до цели", &s.showLine);
        SkeetCheck("##v_cross", "Крестик по центру", &s.showCrosshair);
        SkeetCheck("##v_fov",   "Квадрат зоны наводки", &s.showFov);
        SkeetCheck("##v_log",   "Панель телеметрии", &s.showLog);
        EndGroupBox();

        ImGui::SetCursorScreenPos(ImVec2(rightX, base.y + 6.0f));
        BeginGroupBox("Performance", colW);
        SkeetSliderI("Лимит DET FPS (YOLO)", "##detMaxFps", &s.detMaxFps,
                     AimSettings::DETFPS_MIN, AimSettings::DETFPS_MAX, "%d fps", "Без лимита");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ограничивает нагрузку DirectML на видеокарту.\n100-120 fps обычно убирают фризы игры без потери плавности аима.");
        SkeetSliderI("Лимит UI FPS", "##uiMaxFps", &s.uiMaxFps,
                     AimSettings::UIFPS_MIN, AimSettings::UIFPS_MAX, "%d fps", "240 fps");
        ImGui::Dummy(ImVec2(0, 2));
        SkeetCheck("##v_grab", "Перехват фокуса (для курсора)", &s.grabFocus);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Выкл (по умолч.): меню открывается поверх игры без сворачивания.\nВкл: отдаёт фокус окну меню (если игра жёстко держит мышь).");
        EndGroupBox();
        break;
    }

    case 2:   // НАСТРОЙКИ И ПРЕСЕТЫ
    {
        ImGui::SetCursorScreenPos(ImVec2(leftX, base.y + 6.0f));
        BeginGroupBox("Miscellaneous", colW);
        SkeetSliderI("Override FOV", "##m_fov", &s.aimFov,
                     AimSettings::AIMFOV_MIN, AimSettings::AIMFOV_MAX, "%d px");
        SkeetCheck("##m_aim",   "Aimbot active", &s.aimEnabled);
        KeyBindTag(&s.aimKey1, false);
        SkeetCheck("##m_boxes", "Show player boxes", &s.showBoxes);
        SkeetCheck("##m_heads", "Show head boxes", &s.showHeadBoxes);
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
                ImGui::TextColored(ImVec4(0.60f, 0.87f, 0.16f, std::min(1.0f, flashT * 2.0f) * contentAlpha),
                                   "%s", flashMsg);
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
        ImGui::TextColored(ImVec4(0.58f, 0.58f, 0.58f, contentAlpha),
            "Для отображения поверх игры без сворачивания используй в игре режим "
            "\"Без рамок\" (Borderless / Fullscreen Window).");
        ImGui::PopTextWrapPos();
        EndGroupBox();
        break;
    }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(); // Alpha
    ImGui::End();

    // Ребинд клавиш
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
