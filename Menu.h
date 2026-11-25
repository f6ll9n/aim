#pragma once
#include <Windows.h>
#include <atomic>
#include "Settings.h"

// форварды, чтоб не тащить тяжёлые инклюды
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

// ============================================================================
//  Menu — своё topmost-окно на дх11 с имгуи (тёмная тема).
//
//  GDI-оверлей с боксами не трогает: меню живёт в отдельном окне и вылазит по N.
//  открытое — тыкабельное (слайдеры/ребинд), закрытое — спрятано.
// ============================================================================
class Menu
{
public:
    Menu() = default;
    ~Menu();

    // девайс дх11 + окно + имгуи. screenW/H для центровки.
    bool Create(HINSTANCE hInst, int screenW, int screenH);

    void Destroy();
    void Toggle() { SetVisible(!m_visible); }
    void SetVisible(bool v);
    bool IsVisible() const noexcept { return m_visible; }
    HWND Handle()  const noexcept { return m_hwnd; }

    // кадр меню. conf — порог, шарят с потоком инференса.
    void Render(AimSettings& s, std::atomic<float>& conf, const RuntimeInfo& info);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool CreateDeviceAndSwapChain();
    void SetStyle();
    void TextKeyBind(const char* label, int* key);   // строка ребинда

    HINSTANCE m_hInst  = nullptr;
    HWND      m_hwnd   = nullptr;
    bool      m_visible = false;

    ID3D11Device*            m_device = nullptr;
    ID3D11DeviceContext*     m_ctx    = nullptr;
    IDXGISwapChain*          m_swap   = nullptr;
    ID3D11RenderTargetView*  m_rtv    = nullptr;

    int m_w = 540, m_h = 580;

    // ребинд: пока listening — ждём нажатия и пишем в *m_listenTarget.
    bool  m_listening     = false;
    int*  m_listenTarget  = nullptr;
    bool  m_listenActive  = false;      // снимок клавы уже снят
    int   m_snapshot[256] = { 0 };      // че было зажато на старте
};
