// ============================================================================
//  capture_demo — DXGI-захват + YOLO (ONNX Runtime / DirectML) + D2D оверлей + aim.
//
//  Потоковая модель (3 потока, zero-copy swap каналов):
//    [захват]   Desktop Duplication (BGRA) -> swap в FrameChannel
//    [инференс] берёт самый свежий кадр -> YOLO (SSSE3 + DML) -> трекинг + aim
//               (наведение сразу по готовности детекции) -> RenderState в DetChannel
//    [main]     Win32 сообщения + D2D/DirectComposition оверлей + ImGui меню
//
//  Клавиши: N  — открыть/закрыть меню настроек
//           F8 — выход (конфиг сохраняется в f6.cfg рядом с exe)
// ============================================================================

#include "ScreenCapture.h"
#include "YoloDetector.h"
#include "Overlay.h"
#include "Menu.h"
#include "Settings.h"

#include <opencv2/core.hpp>

#include <Windows.h>
#include <chrono>
#include <string>
#include <vector>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

constexpr float AIM_SWITCH_HYST = 0.70f;  // гистерезис переключения цели
constexpr bool  FORCE_CPU       = false;  // false = пробуем DirectML; фолбэк на CPU

// --- разделяемые между потоками параметры (обновляются в main, читаются в infThread) ---
struct SharedAimParams
{
    std::atomic<bool>  aimEnabled  { true  };
    std::atomic<float> confThresh  { 0.30f };
    std::atomic<float> iouThresh   { 0.45f };
    std::atomic<float> headFrac    { 0.30f };
    std::atomic<float> headConf    { 0.55f };
    std::atomic<float> smoothness  { 0.15f };
    std::atomic<float> aimStrength { 0.15f };
    std::atomic<float> aimDeadzone { 8.0f  };
    std::atomic<float> aimMaxStep  { 25.0f };
    std::atomic<int>   aimFov      { 400   };
    std::atomic<int>   aimKey1     { VK_LBUTTON };
    std::atomic<int>   aimKey2     { VK_MENU    };
    std::atomic<bool>  menuVisible { false };
};

static SharedAimParams gParams;

static void SyncParamsFromSettings(const AimSettings& s, bool menuVisible)
{
    gParams.aimEnabled.store(s.aimEnabled,   std::memory_order_relaxed);
    gParams.headFrac.store(s.headFrac,       std::memory_order_relaxed);
    gParams.headConf.store(s.headConf,       std::memory_order_relaxed);
    gParams.smoothness.store(s.smoothness,   std::memory_order_relaxed);
    gParams.aimStrength.store(s.aimStrength, std::memory_order_relaxed);
    gParams.aimDeadzone.store(s.aimDeadzone, std::memory_order_relaxed);
    gParams.aimMaxStep.store(s.aimMaxStep,   std::memory_order_relaxed);
    gParams.aimFov.store(s.aimFov,           std::memory_order_relaxed);
    gParams.aimKey1.store(s.aimKey1,         std::memory_order_relaxed);
    gParams.aimKey2.store(s.aimKey2,         std::memory_order_relaxed);
    gParams.menuVisible.store(menuVisible,   std::memory_order_relaxed);
}

// --- пути рядом с exe ---
static std::string ExeDir()
{
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s;
    if (n > 0 && n < MAX_PATH) s.assign(buf, n);
    size_t pos = s.find_last_of("\\/");
    return (pos == std::string::npos) ? std::string() : s.substr(0, pos + 1);
}

static std::string ConfigPath()
{
    return ExeDir() + "f6.cfg";
}

static void LoadConfig(AimSettings& s)
{
    std::string path = ConfigPath();
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "r") != 0 || !f) return;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        char* val = eq + 1;
        char* nl = strchr(val, '\n');
        if (nl) *nl = '\0';
        if      (!strcmp(key, "aimEnabled"))    s.aimEnabled    = atoi(val) != 0;
        else if (!strcmp(key, "headFrac"))      s.headFrac      = std::clamp((float)atof(val), AimSettings::HD_MIN, AimSettings::HD_MAX);
        else if (!strcmp(key, "headConf"))      s.headConf      = std::clamp((float)atof(val), AimSettings::HC_MIN, AimSettings::HC_MAX);
        else if (!strcmp(key, "smoothness"))    s.smoothness    = std::clamp((float)atof(val), AimSettings::SM_MIN, AimSettings::SM_MAX);
        else if (!strcmp(key, "aimStrength"))   s.aimStrength   = std::clamp((float)atof(val), AimSettings::ST_MIN, AimSettings::ST_MAX);
        else if (!strcmp(key, "aimDeadzone"))   s.aimDeadzone   = std::clamp((float)atof(val), AimSettings::DZ_MIN, AimSettings::DZ_MAX);
        else if (!strcmp(key, "aimMaxStep"))    s.aimMaxStep    = std::clamp((float)atof(val), AimSettings::MS_MIN, AimSettings::MS_MAX);
        else if (!strcmp(key, "aimFov"))        s.aimFov        = std::clamp(atoi(val), AimSettings::AIMFOV_MIN, AimSettings::AIMFOV_MAX);
        else if (!strcmp(key, "uiMaxFps"))      s.uiMaxFps      = std::clamp(atoi(val), AimSettings::UIFPS_MIN,  AimSettings::UIFPS_MAX);
        else if (!strcmp(key, "grabFocus"))     s.grabFocus     = atoi(val) != 0;
        else if (!strcmp(key, "showCrosshair")) s.showCrosshair = atoi(val) != 0;
        else if (!strcmp(key, "showBoxes"))     s.showBoxes     = atoi(val) != 0;
        else if (!strcmp(key, "showLine"))      s.showLine      = atoi(val) != 0;
        else if (!strcmp(key, "showLog"))       s.showLog       = atoi(val) != 0;
        else if (!strcmp(key, "showFov"))       s.showFov       = atoi(val) != 0;
        else if (!strcmp(key, "aimKey1"))       s.aimKey1       = atoi(val);
        else if (!strcmp(key, "aimKey2"))       s.aimKey2       = atoi(val);
        else if (!strcmp(key, "conf"))          gParams.confThresh.store(std::clamp((float)atof(val), 0.05f, 0.95f));
        else if (!strcmp(key, "iou"))           gParams.iouThresh.store(std::clamp((float)atof(val),  0.10f, 0.85f));
    }
    fclose(f);
}

static void SaveConfig(const AimSettings& s)
{
    std::string path = ConfigPath();
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "w") != 0 || !f) return;
    fprintf(f, "aimEnabled=%d\n",      s.aimEnabled ? 1 : 0);
    fprintf(f, "headFrac=%.3f\n",      s.headFrac);
    fprintf(f, "headConf=%.3f\n",      s.headConf);
    fprintf(f, "smoothness=%.3f\n",    s.smoothness);
    fprintf(f, "aimStrength=%.3f\n",   s.aimStrength);
    fprintf(f, "aimDeadzone=%.1f\n",   s.aimDeadzone);
    fprintf(f, "aimMaxStep=%.1f\n",    s.aimMaxStep);
    fprintf(f, "aimFov=%d\n",          s.aimFov);
    fprintf(f, "uiMaxFps=%d\n",        s.uiMaxFps);
    fprintf(f, "grabFocus=%d\n",       s.grabFocus ? 1 : 0);
    fprintf(f, "showCrosshair=%d\n",   s.showCrosshair ? 1 : 0);
    fprintf(f, "showBoxes=%d\n",       s.showBoxes ? 1 : 0);
    fprintf(f, "showLine=%d\n",        s.showLine ? 1 : 0);
    fprintf(f, "showLog=%d\n",         s.showLog ? 1 : 0);
    fprintf(f, "showFov=%d\n",         s.showFov ? 1 : 0);
    fprintf(f, "aimKey1=%d\n",         s.aimKey1);
    fprintf(f, "aimKey2=%d\n",         s.aimKey2);
    fprintf(f, "conf=%.3f\n",          gParams.confThresh.load());
    fprintf(f, "iou=%.3f\n",           gParams.iouThresh.load());
    fclose(f);
}

static std::wstring GetOnnxPath()
{
    int n = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &n);
    std::wstring path = L"best.onnx";
    if (n >= 2 && args && args[1] && wcslen(args[1]) > 0)
        path = args[1];
    if (args) LocalFree(args);
    return path;
}

static void Fatal(const wchar_t* msg)
{
    MessageBoxW(nullptr, msg, L"capture_demo", MB_ICONERROR | MB_OK);
}

static void MoveMouse(int dx, int dy)
{
    if (dx == 0 && dy == 0) return;
    INPUT in[1]{};
    in[0].type       = INPUT_MOUSE;
    in[0].mi.dx      = dx;
    in[0].mi.dy      = dy;
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, in, sizeof(INPUT));
}

// --- трекер: подавление ложных срабатываний временной стабилизацией ---
constexpr float TRACK_IOU = 0.30f;
constexpr int   MIN_AGE   = 2;
constexpr int   MAX_GAP   = 3;

struct Track
{
    float x = 0, y = 0, w = 0, h = 0, conf = 0;
    int age = 0;
    int gap = 0;
    int id  = 0;
};

static int NextTrackId() { static int n = 0; return ++n; }

static float BoxIoU(float ax, float ay, float aw, float ah,
                    float bx, float by, float bw, float bh)
{
    float ix1 = std::max(ax, bx), iy1 = std::max(ay, by);
    float ix2 = std::min(ax + aw, bx + bw), iy2 = std::min(ay + ah, by + bh);
    float inter = std::max(0.f, ix2 - ix1) * std::max(0.f, iy2 - iy1);
    float un = (aw * ah) + (bw * bh) - inter;
    return un > 0.f ? inter / un : 0.f;
}

static void ApplyDetections(const std::vector<Detection>& last,
                            std::vector<Track>& tracks,
                            std::vector<Detection>& stable,
                            std::vector<int>& stableIds,
                            float ema)
{
    for (auto& t : tracks) ++t.gap;
    for (const auto& d : last)
    {
        Track* match = nullptr;
        float best = TRACK_IOU;
        for (auto& t : tracks)
        {
            float iou = BoxIoU(t.x, t.y, t.w, t.h, d.x, d.y, d.w, d.h);
            if (iou > best) { best = iou; match = &t; }
        }
        if (match)
        {
            match->x = match->x * (1.f - ema) + d.x * ema;
            match->y = match->y * (1.f - ema) + d.y * ema;
            match->w = match->w * (1.f - ema) + d.w * ema;
            match->h = match->h * (1.f - ema) + d.h * ema;
            match->conf = d.confidence;
            ++match->age;
            match->gap = 0;
        }
        else
        {
            tracks.push_back({ d.x, d.y, d.w, d.h, d.confidence, 1, 0, NextTrackId() });
        }
    }
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
                       [](const Track& t) { return t.gap > MAX_GAP; }),
        tracks.end());

    stable.clear();
    stableIds.clear();
    for (const auto& t : tracks)
        if (t.age >= MIN_AGE)
        {
            stable.push_back({ t.x, t.y, t.w, t.h, t.conf, 0 });
            stableIds.push_back(t.id);
        }
}

// Канал «кадр»: zero-copy swap из потока захвата в поток инференса.
struct FrameChannel
{
    std::mutex              mtx;
    std::condition_variable cv;
    std::vector<uint8_t>    buf;
    uint32_t                w = 0, h = 0;
    size_t                  stride = 0;
    uint64_t                stamp = 0;
};

// Снапшот целей и точки наведения для отрисовки в main.
struct RenderSnapshot
{
    std::vector<Detection> stable;
    int   targetIdx = -1;
    float aimPx     = 320.f;
    float aimPy     = 320.f;
    bool  aimIsHead = false;
    float maxConf   = 0.f;
};

struct DetChannel
{
    std::mutex     mtx;
    RenderSnapshot snap;
    uint64_t       stamp = 0;
};

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 1) Детектор YOLO (ONNX Runtime, DirectML/CPU)
    std::wstring onnxPath = GetOnnxPath();
    YoloDetector det;
    if (!det.Initialize(onnxPath, !FORCE_CPU))
    {
        Fatal((L"Не удалось загрузить ONNX: " + onnxPath).c_str());
        return 1;
    }

    // 2) Захват (нативный BGRA без лишней конвертации в BGR — YoloDetector читает BGRA напрямую)
    ScreenCapture::Config cfg;
    cfg.fovWidth       = static_cast<UINT>(det.InputSize());
    cfg.fovHeight      = static_cast<UINT>(det.InputSize());
    cfg.frameTimeoutMs = 4;
    cfg.convertToBGR   = false;

    ScreenCapture cap(cfg);
    try { cap.Initialize(); }
    catch (const std::exception& e)
    {
        wchar_t buf[512];
        swprintf(buf, 512, L"Не удалось инициализировать захват:\n%hs", e.what());
        Fatal(buf);
        return 1;
    }

    // 3) Оверлей (Direct2D + DirectComposition)
    RECT desk = cap.DesktopRect();
    Overlay overlay;
    if (!overlay.Create(hInst, desk, L"aim"))
    {
        Fatal(L"Не удалось создать оверлей.");
        return 1;
    }

    // 4) Меню (ImGui + DX11)
    int screenW = desk.right - desk.left;
    int screenH = desk.bottom - desk.top;
    Menu menu;
    if (!menu.Create(hInst, screenW, screenH))
    {
        Fatal(L"Не удалось создать меню.");
        return 1;
    }

    AimSettings s;
    LoadConfig(s);
    SyncParamsFromSettings(s, menu.IsVisible());

    const float fovCx = cap.FovWidth()  * 0.5f;
    const float fovCy = cap.FovHeight() * 0.5f;

    // --- 5) потоковая модель ---
    FrameChannel fc;
    DetChannel   dc;
    std::atomic<bool>  running{ true };
    std::atomic<float> gCapFps{ 0.f };
    std::atomic<float> gDetFps{ 0.f };
    std::atomic<float> gInfMs { 0.f };

    // Поток захвата: крутит DDA и делает O(1) swap кадра.
    std::thread capThread([&]()
    {
        std::vector<uint8_t> stageBuf;
        auto capT = std::chrono::steady_clock::now();
        int capCnt = 0;

        while (running.load(std::memory_order_relaxed))
        {
            if (cap.Capture())
            {
                const auto& f = cap.GetFrame();
                if (stageBuf.size() != f.sizeBytes)
                    stageBuf.resize(f.sizeBytes);
                std::memcpy(stageBuf.data(), f.data, f.sizeBytes);

                {
                    std::lock_guard<std::mutex> lk(fc.mtx);
                    fc.buf.swap(stageBuf);
                    fc.w      = f.width;
                    fc.h      = f.height;
                    fc.stride = f.stride;
                    ++fc.stamp;
                }
                fc.cv.notify_one();

                ++capCnt;
                auto now = std::chrono::steady_clock::now();
                double dt = std::chrono::duration<double>(now - capT).count();
                if (dt >= 1.0)
                {
                    gCapFps.store(static_cast<float>(capCnt / dt), std::memory_order_relaxed);
                    capCnt = 0;
                    capT = now;
                }
            }
        }
        fc.cv.notify_all();
    });

    // Поток инференса: YOLO + трекинг + мгновенный aim (0 мс ожидания UI).
    std::thread infThread([&]()
    {
        std::vector<uint8_t> priv;
        uint64_t seen = 0;
        uint32_t w = 0, h = 0;
        size_t   stride = 0;

        std::vector<Track>     tracks;
        std::vector<Detection> stable;
        std::vector<int>       stableIds;
        std::vector<Detection> players, heads;
        int selectedId = -1;

        auto   detT      = std::chrono::steady_clock::now();
        int    detCnt    = 0;
        double infSumMs  = 0.0;

        while (true)
        {
            {
                std::unique_lock<std::mutex> lk(fc.mtx);
                fc.cv.wait(lk, [&]()
                {
                    return !running.load(std::memory_order_relaxed) || fc.stamp != seen;
                });
                if (!running.load(std::memory_order_relaxed))
                    break;

                seen   = fc.stamp;
                w      = fc.w;
                h      = fc.h;
                stride = fc.stride;
                priv.swap(fc.buf);
            }

            if (priv.empty() || w == 0 || h == 0)
                continue;

            const bool  aimEn       = gParams.aimEnabled.load(std::memory_order_relaxed);
            const float confThresh  = gParams.confThresh.load(std::memory_order_relaxed);
            const float iouThresh   = gParams.iouThresh.load(std::memory_order_relaxed);
            const float headFrac    = gParams.headFrac.load(std::memory_order_relaxed);
            const float headConf    = gParams.headConf.load(std::memory_order_relaxed);
            const float smoothness  = gParams.smoothness.load(std::memory_order_relaxed);
            const float aimStrength = gParams.aimStrength.load(std::memory_order_relaxed);
            const float aimDeadzone = gParams.aimDeadzone.load(std::memory_order_relaxed);
            const float aimMaxStep  = gParams.aimMaxStep.load(std::memory_order_relaxed);
            const int   aimFov      = gParams.aimFov.load(std::memory_order_relaxed);
            const int   key1        = gParams.aimKey1.load(std::memory_order_relaxed);
            const int   key2        = gParams.aimKey2.load(std::memory_order_relaxed);
            const bool  menuOpen    = gParams.menuVisible.load(std::memory_order_relaxed);

            auto t0 = std::chrono::steady_clock::now();
            cv::Mat frame(static_cast<int>(h), static_cast<int>(w), CV_8UC4,
                          priv.data(), stride);
            auto last = det.Detect(frame, confThresh, iouThresh);
            auto t1 = std::chrono::steady_clock::now();
            infSumMs += std::chrono::duration<double, std::milli>(t1 - t0).count();

            // Разделяем: 0 = player, 1 = head.
            players.clear();
            heads.clear();
            for (const auto& d : last)
                (d.classId == 0 ? players : heads).push_back(d);

            ApplyDetections(players, tracks, stable, stableIds, 1.0f - smoothness);

            // --- выбор цели: ближайшая к прицелу, с гистерезисом ---
            auto sqDist = [&](const Detection& d)
            {
                float dx = (d.x + d.w * 0.5f) - fovCx;
                float dy = (d.y + d.h * 0.5f) - fovCy;
                return dx * dx + dy * dy;
            };

            const float aimHalf = aimFov * 0.5f;
            auto inZone = [&](const Detection& d)
            {
                float cx = d.x + d.w * 0.5f;
                float cy = d.y + d.h * 0.5f;
                return std::abs(cx - fovCx) <= aimHalf &&
                       std::abs(cy - fovCy) <= aimHalf;
            };

            int   candIdx  = -1;
            float candDist = 1e9f;
            for (size_t i = 0; i < stable.size(); ++i)
            {
                if (!inZone(stable[i])) continue;
                float d = sqDist(stable[i]);
                if (d < candDist) { candDist = d; candIdx = static_cast<int>(i); }
            }

            int selIdx = -1;
            for (size_t i = 0; i < stable.size(); ++i)
                if (stableIds[i] == selectedId) { selIdx = static_cast<int>(i); break; }

            if (selIdx >= 0 && !inZone(stable[selIdx]))
                selIdx = -1;

            int targetIdx = -1;
            if (selIdx >= 0)
            {
                float selDist = sqDist(stable[selIdx]);
                if (candIdx >= 0 && candDist < selDist * AIM_SWITCH_HYST)
                {
                    targetIdx  = candIdx;
                    selectedId = stableIds[candIdx];
                }
                else
                {
                    targetIdx = selIdx;
                }
            }
            else if (candIdx >= 0)
            {
                targetIdx  = candIdx;
                selectedId = stableIds[candIdx];
            }
            else
            {
                targetIdx  = -1;
                selectedId = -1;
            }

            float aimPx     = fovCx;
            float aimPy     = fovCy;
            bool  aimIsHead = false;

            if (targetIdx >= 0)
            {
                const Detection& t = stable[targetIdx];
                const Detection* hBox = nullptr;
                for (const auto& hd : heads)
                {
                    if (hd.confidence < headConf) continue;
                    float hcx = hd.x + hd.w * 0.5f;
                    float hcy = hd.y + hd.h * 0.5f;
                    if (hcx >= t.x && hcx <= t.x + t.w &&
                        hcy >= t.y && hcy <= t.y + t.h)
                    {
                        if (!hBox || hd.confidence > hBox->confidence)
                            hBox = &hd;
                    }
                }
                aimIsHead = (hBox != nullptr);
                if (hBox)
                {
                    aimPx = hBox->x + hBox->w * 0.5f;
                    aimPy = hBox->y + hBox->h * 0.5f;
                }
                else
                {
                    aimPx = t.x + t.w * 0.5f;
                    aimPy = t.y + t.h * headFrac;
                }

                bool aimHeld = aimEn && !menuOpen &&
                    (((key1 > 0) && (GetAsyncKeyState(key1) & 0x8000) != 0) ||
                     ((key2 > 0) && (GetAsyncKeyState(key2) & 0x8000) != 0));

                if (aimHeld)
                {
                    float dx   = aimPx - fovCx;
                    float dy   = aimPy - fovCy;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > aimDeadzone)
                    {
                        float step = dist * aimStrength;
                        if (step > aimMaxStep) step = aimMaxStep;
                        MoveMouse(static_cast<int>(std::lround(dx / dist * step)),
                                  static_cast<int>(std::lround(dy / dist * step)));
                    }
                }
            }

            // Публикуем снапшот для оверлея
            {
                std::lock_guard<std::mutex> lk(dc.mtx);
                dc.snap.stable    = stable;
                dc.snap.targetIdx = targetIdx;
                dc.snap.aimPx     = aimPx;
                dc.snap.aimPy     = aimPy;
                dc.snap.aimIsHead = aimIsHead;
                dc.snap.maxConf   = det.LastMaxConf();
                ++dc.stamp;
            }

            ++detCnt;
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - detT).count();
            if (dt >= 1.0)
            {
                gDetFps.store(static_cast<float>(detCnt / dt), std::memory_order_relaxed);
                gInfMs.store(static_cast<float>(infSumMs / detCnt), std::memory_order_relaxed);
                detCnt   = 0;
                infSumMs = 0.0;
                detT     = now;
            }
        }
    });

    // Высокоточный таймер Windows 10/11 для точного 144/240 FPS пейсинга без лишнего CPU-спина.
    HANDLE waitTimer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

    RenderSnapshot snap;
    uint64_t lastDetStamp = 0;
    bool     menuNPrev    = false;
    bool     prevMenuVis  = menu.IsVisible();

    double uiFps = 0.0;
    auto   fpsT  = std::chrono::steady_clock::now();
    int    uiCnt = 0;

    auto nextFrame = std::chrono::steady_clock::now();

    while (true)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (GetAsyncKeyState(VK_F8) & 0x8000)
            break;

        bool nDown = ((GetAsyncKeyState('N') & 0x8000) != 0) ||
                     ((GetAsyncKeyState(VK_INSERT) & 0x8000) != 0);
        if (nDown && !menuNPrev && !menu.IsListening())
            menu.Toggle(s.grabFocus);
        menuNPrev = nDown;

        // Если меню только что закрыли (N/Insert или крестик) — автосохранение конфига.
        bool curMenuVis = menu.IsVisible();
        if (prevMenuVis && !curMenuVis)
            SaveConfig(s);
        prevMenuVis = curMenuVis;

        bool aimHeldUi = s.aimEnabled && !curMenuVis &&
            (((s.aimKey1 > 0) && (GetAsyncKeyState(s.aimKey1) & 0x8000) != 0) ||
             ((s.aimKey2 > 0) && (GetAsyncKeyState(s.aimKey2) & 0x8000) != 0));

        // Забираем свежий снапшот от потока инференса (если обновился)
        {
            std::lock_guard<std::mutex> lk(dc.mtx);
            if (dc.stamp != lastDetStamp)
            {
                lastDetStamp = dc.stamp;
                snap = dc.snap;
            }
        }

        const Detection* target =
            (snap.targetIdx >= 0 && snap.targetIdx < static_cast<int>(snap.stable.size()))
                ? &snap.stable[snap.targetIdx] : nullptr;

        // --- отрисовка оверлея (Direct2D + DirectComposition) ---
        overlay.BeginDraw();

        const int crossX = cap.CropRect().left + static_cast<int>(fovCx);
        const int crossY = cap.CropRect().top  + static_cast<int>(fovCy);
        if (s.showCrosshair)
        {
            overlay.DrawFilledRect(crossX - 6, crossY - 1, 13, 3, RGB(255, 255, 255));
            overlay.DrawFilledRect(crossX - 1, crossY - 6, 3, 13, RGB(255, 255, 255));
        }

        if (target && s.showLine)
        {
            int tx = cap.CropRect().left + static_cast<int>(snap.aimPx);
            int ty = cap.CropRect().top  + static_cast<int>(snap.aimPy);
            overlay.DrawLine(crossX, crossY, tx, ty, RGB(0, 255, 255), 2);
            overlay.DrawFilledRect(tx - 3, ty - 3, 7, 7, RGB(0, 255, 255));
        }

        if (s.showBoxes)
        {
            for (size_t i = 0; i < snap.stable.size(); ++i)
            {
                const auto& d = snap.stable[i];
                int x = cap.CropRect().left + static_cast<int>(d.x);
                int y = cap.CropRect().top  + static_cast<int>(d.y);
                int w = static_cast<int>(d.w);
                int h = static_cast<int>(d.h);

                bool isTarget = (static_cast<int>(i) == snap.targetIdx);
                overlay.DrawRect(x, y, w, h,
                                 isTarget ? RGB(0, 220, 255) : RGB(255, 45, 45),
                                 isTarget ? 3 : 2);

                wchar_t lbl[32];
                swprintf(lbl, 32, L"%.2f", d.confidence);
                overlay.DrawText(x, (y > 18) ? y - 18 : y + 2, lbl, 15,
                                 isTarget ? RGB(0, 200, 255) : RGB(255, 80, 80), true);
            }
        }

        const float capFps = gCapFps.load(std::memory_order_relaxed);
        const float detFps = gDetFps.load(std::memory_order_relaxed);
        const float infMs  = gInfMs.load(std::memory_order_relaxed);

        if (s.showLog)
        {
            const int panelW = 236, panelH = 148;
            const int px = overlay.Width() - panelW - 12, py = 12;
            overlay.DrawFilledRect(px, py, panelW, panelH, RGB(20, 22, 26), 215);
            overlay.DrawRect(px, py, panelW, panelH, RGB(60, 65, 75), 1);

            wchar_t line[128];
            swprintf(line, 128, L"TARGETS: %zu", snap.stable.size());
            overlay.DrawText(px + 10, py + 8,  line, 16, RGB(255, 90, 90), true);
            swprintf(line, 128, L"MODE: %s (%.1f ms)", det.UseGpu() ? L"DML" : L"CPU", infMs);
            overlay.DrawText(px + 10, py + 30, line, 15, RGB(255, 200, 90), false);
            swprintf(line, 128, L"AIM: %s %s sm%.2f", aimHeldUi ? L"ON" : L"OFF",
                     (target && snap.aimIsHead) ? L"HEAD" : L"BODY", s.smoothness);
            overlay.DrawText(px + 10, py + 52, line, 15,
                             aimHeldUi ? RGB(0, 220, 120) : RGB(150, 150, 155), false);
            swprintf(line, 128, L"CAP: %.0f  DET: %.0f fps", capFps, detFps);
            overlay.DrawText(px + 10, py + 74, line, 15, RGB(120, 220, 120), false);
            swprintf(line, 128, L"UI: %.0f fps", uiFps);
            overlay.DrawText(px + 10, py + 96, line, 15, RGB(120, 175, 240), false);
            if (target)
                swprintf(line, 128, L"LOCK: %+.0f, %+.0f", snap.aimPx - fovCx, snap.aimPy - fovCy);
            else
                swprintf(line, 128, L"LOCK: none");
            overlay.DrawText(px + 10, py + 118, line, 15,
                             target ? RGB(0, 255, 255) : RGB(140, 140, 145), false);
        }

        if (s.showFov)
        {
            int half = s.aimFov / 2;
            overlay.DrawRect(crossX - half, crossY - half,
                             s.aimFov, s.aimFov, RGB(80, 255, 120), 2, 180);
        }

        overlay.EndDraw();

        // --- рендер меню (ImGui + DX11), если открыто ---
        {
            RuntimeInfo ri;
            ri.targets = static_cast<int>(snap.stable.size());
            ri.aimHeld = aimHeldUi;
            ri.gpu     = det.UseGpu();
            ri.capFps  = capFps;
            ri.detFps  = detFps;
            ri.infMs   = infMs;
            ri.uiFps   = static_cast<float>(uiFps);
            ri.maxConf = snap.maxConf;

            Menu::Action act = menu.Render(s, gParams.confThresh, gParams.iouThresh, ri);
            if (act == Menu::Action::Save)
                SaveConfig(s);
            else if (act == Menu::Action::Load)
                LoadConfig(s);
            SyncParamsFromSettings(s, menu.IsVisible());
            if (act == Menu::Action::Unload)
                break;
        }

        // --- FPS-замеры (окно 1 сек) ---
        ++uiCnt;
        {
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - fpsT).count();
            if (dt >= 1.0)
            {
                uiFps = uiCnt / dt;
                uiCnt = 0;
                fpsT  = now;
            }
        }

        // --- точный frame pacer (не прибавляет sleep поверх времени рендера) ---
        if (s.uiMaxFps < AimSettings::UIFPS_MAX)
        {
            const auto period = std::chrono::microseconds(1'000'000 / std::max(30, s.uiMaxFps));
            nextFrame += period;
            auto now = std::chrono::steady_clock::now();
            if (now - nextFrame > period * 2)
                nextFrame = now;

            auto rem = nextFrame - now;
            if (rem > std::chrono::microseconds(500))
            {
                if (waitTimer)
                {
                    LARGE_INTEGER due{};
                    // 100-нс интервалы со знаком минус = относительное ожидание
                    due.QuadPart = -(std::chrono::duration_cast<std::chrono::nanoseconds>(rem).count() / 100);
                    if (SetWaitableTimer(waitTimer, &due, 0, nullptr, nullptr, FALSE))
                        WaitForSingleObject(waitTimer, INFINITE);
                }
                else
                {
                    std::this_thread::sleep_for(rem);
                }
            }
        }
    }

    if (waitTimer) CloseHandle(waitTimer);

    running.store(false);
    fc.cv.notify_all();
    if (capThread.joinable()) capThread.join();
    if (infThread.joinable()) infThread.join();

    menu.Destroy();
    overlay.Destroy();
    SaveConfig(s);
    return 0;
}
