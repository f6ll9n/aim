#pragma once
#include <Windows.h>
#include <atomic>
#include "Settings.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

// ============================================================================
//  Menu — Skeet / GameSense-стиль меню на DX11 + ImGui.
//  По умолчанию использует WS_EX_NOACTIVATE + MA_NOACTIVATE: открывается поверх
//  игры (в режиме без рамок / окне), не перехватывая фокус и не сворачивая её.
// ============================================================================
class Menu
{
public:
    enum class Action { None, Save, Load, Unload };

    Menu() = default;
    ~Menu();

    bool Create(HINSTANCE hInst, int screenW, int screenH);

    void Destroy();
    void Toggle(bool grabFocus = false) { SetVisible(!m_visible, grabFocus); }
    void SetVisible(bool v, bool grabFocus = false);
    bool IsVisible()   const noexcept { return m_visible; }
    bool IsListening() const noexcept { return m_listening; }
    HWND Handle()      const noexcept { return m_hwnd; }

    Action Render(AimSettings& s,
                  std::atomic<float>& conf,
                  std::atomic<float>& iou,
                  const RuntimeInfo& info);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool CreateDeviceAndSwapChain();
    void SetStyle();
    void KeyBindTag(int* key, bool allowClear);

    HINSTANCE m_hInst   = nullptr;
    HWND      m_hwnd    = nullptr;
    bool      m_visible = false;
    bool      m_grabFocus = false;

    ID3D11Device*           m_device = nullptr;
    ID3D11DeviceContext*    m_ctx    = nullptr;
    IDXGISwapChain*         m_swap   = nullptr;
    ID3D11RenderTargetView* m_rtv    = nullptr;

    int m_w = 660, m_h = 540;

    bool  m_listening     = false;
    int*  m_listenTarget  = nullptr;
    bool  m_listenActive  = false;
    int   m_snapshot[256] = { 0 };
};
