// ============================================================================
//  capture_demo — DXGI-захват + YOLO (ONNX Runtime / DirectML) + оверлей + aim.
//
//  Потоковая модель (3 потока, минимум блокировок):
//    [захват]  Desktop Duplication -> последний кадр в FrameChannel
//    [инференс] берёт САМЫЙ свежий кадр -> YOLO -> детекции в DetChannel
//    [main]    трекинг + наведение + рендер оверлея (не блокируется инференсом)
//
//  Оверлей: прозрачное топмост-окно поверх игры + боксы врагов + лог справа.
//
//  Запуск:  capture_demo.exe [путь_к_best.onnx]
//  Клавиши: ЛКМ (зажать) или Alt (зажать) — наведение к цели
//           - / = — слабее / сильнее наведение
//           [ / ] — точка прицеливания вверх/вниз (голова)
//           F8 — выход
// ============================================================================

#include "ScreenCapture.h"
#include "YoloDetector.h"
#include "Overlay.h"
#include "Menu.h"
#include "Settings.h"

#include <opencv2/opencv.hpp>

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

// --- настройки aim (фиксированные; тонкая подстройка — ползунки в меню) ---
constexpr float AIM_STRENGTH    = 0.15f;  // сила наведения (доля дистанции за шаг)
constexpr float AIM_DEADZONE    = 8.0f;   // px: радиус «мёртвой зоны»
constexpr float AIM_MAX_STEP    = 25.0f;  // px: кап перемещения мыши за кадр
constexpr float AIM_SWITCH_HYST = 0.70f;  // гистерезис переключения цели
constexpr bool  FORCE_CPU       = false;  // false = пробуем DirectML; фолбэк на CPU

// Пороги детекции — разделяются с потоком инференса (атомарные), правятся в меню.
static std::atomic<float> gConfThresh{ 0.30f };
static std::atomic<float> gIouThresh  { 0.45f };

// --- сохранение/загрузка настроек в f6.cfg (лежит рядом с exe) ---
static std::string ConfigPath()
{
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s;
    if (n > 0 && n < MAX_PATH) s.assign(buf, n);
    size_t pos = s.find_last_of("\\/");
    std::string dir = (pos == std::string::npos) ? std::string() : s.substr(0, pos + 1);
    return dir + "f6.cfg";
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
        if      (!strcmp(key, "headFrac"))      s.headFrac      = (float)atof(val);
        else if (!strcmp(key, "headConf"))      s.headConf      = (float)atof(val);
        else if (!strcmp(key, "smoothness"))    s.smoothness    = (float)atof(val);
        else if (!strcmp(key, "aimFov"))        s.aimFov        = atoi(val);
        else if (!strcmp(key, "showCrosshair")) s.showCrosshair = atoi(val) != 0;
        else if (!strcmp(key, "showBoxes"))     s.showBoxes     = atoi(val) != 0;
        else if (!strcmp(key, "showLine"))      s.showLine      = atoi(val) != 0;
        else if (!strcmp(key, "showLog"))       s.showLog       = atoi(val) != 0;
        else if (!strcmp(key, "showFov"))       s.showFov       = atoi(val) != 0;
        else if (!strcmp(key, "aimKey1"))       s.aimKey1       = atoi(val);
        else if (!strcmp(key, "aimKey2"))       s.aimKey2       = atoi(val);
        else if (!strcmp(key, "conf"))          gConfThresh.store((float)atof(val));
    }
    fclose(f);
}

static void SaveConfig(const AimSettings& s)
{
    std::string path = ConfigPath();
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "w") != 0 || !f) return;
    fprintf(f, "headFrac=%.3f\n",      s.headFrac);
    fprintf(f, "headConf=%.3f\n",      s.headConf);
    fprintf(f, "smoothness=%.3f\n",    s.smoothness);
    fprintf(f, "aimFov=%d\n",          s.aimFov);
    fprintf(f, "showCrosshair=%d\n",   s.showCrosshair ? 1 : 0);
    fprintf(f, "showBoxes=%d\n",       s.showBoxes ? 1 : 0);
    fprintf(f, "showLine=%d\n",        s.showLine ? 1 : 0);
    fprintf(f, "showLog=%d\n",         s.showLog ? 1 : 0);
    fprintf(f, "showFov=%d\n",         s.showFov ? 1 : 0);
    fprintf(f, "aimKey1=%d\n",         s.aimKey1);
    fprintf(f, "aimKey2=%d\n",         s.aimKey2);
    fprintf(f, "conf=%.3f\n",          gConfThresh.load());
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

// Относительное перемещение мыши (пиксели).
static void MoveMouse(int dx, int dy)
{
    INPUT in[1]{};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dx = dx;
    in[0].mi.dy = dy;
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, in, sizeof(INPUT));
}

// --- трекер: подавление ложных срабатываний временной стабилизацией ---
// Цель считается реальной, только если держится MIN_AGE кадров подряд.
constexpr float TRACK_IOU = 0.30f;  // IoU-порог «та же цель»
constexpr int   MIN_AGE   = 2;      // кадров подряд для подтверждения
constexpr int   MAX_GAP   = 3;      // кадров пропуска до удаления трека

struct Track
{
    float x = 0, y = 0, w = 0, h = 0, conf = 0;
    int age = 0;   // сколько кадров подряд подтверждалась
    int gap = 0;   // кадров с последнего попадания
    int id = 0;    // стабильный id (для гистерезиса выбора цели)
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

// Применить новую пачку детекций к трекам (вызывается в main на каждой пачке).
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
            stable.push_back({ t.x, t.y, t.w, t.h, t.conf });
            stableIds.push_back(t.id);
        }
}

// Канал «кадр»: из потока захвата в поток инференса.
struct FrameChannel
{
    std::mutex              mtx;
    std::condition_variable cv;
    std::vector<uint8_t>    buf;
    uint32_t                w = 0, h = 0;
    size_t                  stride = 0;
    uint64_t                stamp = 0;
};

// Канал «детекции»: из потока инференса в main.
struct DetChannel
{
    std::mutex             mtx;
    std::vector<Detection> dets;
    float                  maxConf = 0.f;
    uint64_t               stamp = 0;
};

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 1) Захват
    ScreenCapture::Config cfg;
    cfg.fovWidth       = 640;
    cfg.fovHeight      = 640;
    cfg.frameTimeoutMs = 8;
    cfg.convertToBGR   = true;

    ScreenCapture cap(cfg);
    try { cap.Initialize(); }
    catch (const std::exception& e)
    {
        wchar_t buf[512];
        swprintf(buf, 512, L"Не удалось инициализировать захват:\n%hs", e.what());
        Fatal(buf);
        return 1;
    }

    // 2) Детектор YOLO (ONNX Runtime, DirectML/CPU)
    std::wstring onnxPath = GetOnnxPath();
    YoloDetector det;
    if (!det.Initialize(onnxPath, !FORCE_CPU))
    {
        Fatal((L"Не удалось загрузить ONNX: " + onnxPath).c_str());
        return 1;
    }

    // 3) Оверлей
    RECT desk = cap.DesktopRect();
    Overlay overlay;
    if (!overlay.Create(hInst, desk, L"aim"))
    {
        Fatal(L"Не удалось создать оверлей.");
        return 1;
    }

    // 4) Меню (ImGui + DX11), центрируем по экрану
    int screenW = desk.right - desk.left;
    int screenH = desk.bottom - desk.top;
    Menu menu;
    if (!menu.Create(hInst, screenW, screenH))
    {
        Fatal(L"Не удалось создать меню.");
        return 1;
    }

    // --- 5) потоковая модель ---
    FrameChannel fc;
    DetChannel   dc;
    std::atomic<bool> running{ true };

    // Поток захвата: крутит DDA и публикует последний кадр.
    std::thread capThread([&]()
    {
        while (running.load(std::memory_order_relaxed))
        {
            if (cap.Capture())
            {
                const auto& f = cap.GetFrame();
                {
                    std::lock_guard<std::mutex> lk(fc.mtx);
                    if (fc.buf.size() != f.sizeBytes)
                        fc.buf.resize(f.sizeBytes);
                    std::memcpy(fc.buf.data(), f.data, f.sizeBytes);
                    fc.w = f.width;
                    fc.h = f.height;
                    fc.stride = f.stride;
                    ++fc.stamp;
                }
                fc.cv.notify_one();
            }
        }
        fc.cv.notify_all();
    });

    // Поток инференса: всегда считает на САМОМ свежем кадре (стейл дропается).
    std::thread infThread([&]()
    {
        std::vector<uint8_t> priv;   // приватная копия, чтобы захват не перезаписал
        uint64_t seen = 0;
        uint32_t w = 0, h = 0; size_t stride = 0;

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

                seen = fc.stamp;
                w = fc.w; h = fc.h; stride = fc.stride;
                if (priv.size() != fc.buf.size())
                    priv.resize(fc.buf.size());
                std::memcpy(priv.data(), fc.buf.data(), fc.buf.size());
            }

            cv::Mat frame(static_cast<int>(h), static_cast<int>(w), CV_8UC3,
                          priv.data(), stride);
            auto dets = det.Detect(frame, gConfThresh.load(std::memory_order_relaxed),
                                    gIouThresh.load(std::memory_order_relaxed));

            {
                std::lock_guard<std::mutex> lk(dc.mtx);
                dc.dets = std::move(dets);
                dc.maxConf = det.LastMaxConf();
                ++dc.stamp;
            }
        }
    });

    // --- 5) главный цикл: наведение + отрисовка ---
    std::vector<Detection> stable;    // подтверждённые трекером цели
    std::vector<int>       stableIds; // id тех же целей (для гистерезиса)
    std::vector<Track> tracks;        // активные треки
    uint64_t lastDetStamp = 0;
    int      targetIdx = -1;          // индекс выбранной цели (для рендера)
    int      selectedId = -1;         // id залоченной цели (гистерезис)
    float    aimPx = 320.f, aimPy = 320.f;   // точка прицеливания (обновится по кадру)
    bool     aimIsHead = false;               // true = целимся в head-бокс (не в тело)

    AimSettings s;                           // настройки (правятся в меню)
    LoadConfig(s);                           // подтягиваем f6.cfg, если сохранён

    bool  menuNPrev = false;                 // edge-triggered (тумблер меню)

    float  fovCx = 320.f, fovCy = 320.f;     // центр FOV (обновляется по кадру)

    double uiFps = 0.0, detFps = 0.0;
    auto fpsT = std::chrono::steady_clock::now();
    int uiCnt = 0, detCnt = 0;
    int dumpTick = 0;   // диагностический дамп оверлея

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

        // Тумблер меню: только N.
        bool nDown = (GetAsyncKeyState('N') & 0x8000) != 0;
        if (nDown && !menuNPrev)
            menu.Toggle();
        menuNPrev = nDown;

        // Центр FOV (меняется при изменении размера FOV в меню).
        fovCx = cap.FovWidth()  * 0.5f;
        fovCy = cap.FovHeight() * 0.5f;

        // Aim активен, пока зажата одна из привязанных клавиш (hold-to-aim).
        // Пока открыто меню — наведение выключено, чтобы мышь не «улетала».
        bool aimHeld = !menu.IsVisible() &&
            (((GetAsyncKeyState(s.aimKey1) & 0x8000) != 0) ||
             ((GetAsyncKeyState(s.aimKey2) & 0x8000) != 0));

        // Если пришла свежая пачка детекций — применить к трекам (вне лока).
        {
            std::vector<Detection> last;
            bool fresh = false;
            {
                std::lock_guard<std::mutex> lk(dc.mtx);
                if (dc.stamp != lastDetStamp)
                {
                    fresh = true;
                    lastDetStamp = dc.stamp;
                    last = dc.dets;
                }
            }
            if (fresh)
            {
                // Разделяем: classId 0 = player (сущности), 1 = head (точка аима).
                std::vector<Detection> players, heads;
                players.reserve(last.size());
                for (const auto& d : last)
                    (d.classId == 0 ? players : heads).push_back(d);

                // smoothness: 0 = резко (ema=1), выше = плавнее (ema=1-smoothness).
                ApplyDetections(players, tracks, stable, stableIds, 1.0f - s.smoothness);
                ++detCnt;

                // --- выбор цели: ближайшая к прицелу, с гистерезисом ---
                // Гистерезис: чтобы прицел не метался между двумя целями на равном расстоянии —
                // переключаемся только если новый кандидат заметно ближе.
                auto sqDist = [&](const Detection& d)
                {
                    float dx = (d.x + d.w * 0.5f) - fovCx;
                    float dy = (d.y + d.h * 0.5f) - fovCy;
                    return dx * dx + dy * dy;
                };

                // Цель учитывается, только если её центр в зоне наводки.
                const float aimHalf = s.aimFov * 0.5f;
                auto inZone = [&](const Detection& d)
                {
                    float cx = d.x + d.w * 0.5f;
                    float cy = d.y + d.h * 0.5f;
                    return std::abs(cx - fovCx) <= aimHalf &&
                           std::abs(cy - fovCy) <= aimHalf;
                };

                int candIdx = -1;
                float candDist = 1e9f;
                for (size_t i = 0; i < stable.size(); ++i)
                {
                    if (!inZone(stable[i])) continue;
                    float d = sqDist(stable[i]);
                    if (d < candDist) { candDist = d; candIdx = static_cast<int>(i); }
                }

                // индекс текущей выбранной цели (если ещё жива)
                int selIdx = -1;
                for (size_t i = 0; i < stable.size(); ++i)
                    if (stableIds[i] == selectedId) { selIdx = static_cast<int>(i); break; }

                // если выбранная цель вышла из зоны наводки — сбрасываем выбор
                if (selIdx >= 0 && !inZone(stable[selIdx]))
                    selIdx = -1;

                if (selIdx >= 0)
                {
                    float selDist = sqDist(stable[selIdx]);
                    if (candIdx >= 0 && candDist < selDist * AIM_SWITCH_HYST)
                    {
                        targetIdx = candIdx;
                        selectedId = stableIds[candIdx];
                    }
                    else
                    {
                        targetIdx = selIdx;   // держим выбранную
                    }
                }
                else
                {
                    if (candIdx >= 0) { targetIdx = candIdx; selectedId = stableIds[candIdx]; }
                    else             { targetIdx = -1;    selectedId = -1; }
                }

                // --- точка прицеливания: голова ТОЛЬКО при уверенности, иначе тело ---
                if (targetIdx >= 0)
                {
                    const Detection& t = stable[targetIdx];

                    // Ищем голову этой цели: центр внутри бокса + уверенность >= headConf.
                    // Берём самую уверенную из подходящих (а не первую попавшуюся).
                    const Detection* h = nullptr;
                    for (const auto& hd : heads)
                    {
                        // ниже порога голова не учитывается — переключаемся на тело.
                        if (hd.confidence < s.headConf) continue;
                        float hcx = hd.x + hd.w * 0.5f;
                        float hcy = hd.y + hd.h * 0.5f;
                        if (hcx >= t.x && hcx <= t.x + t.w &&
                            hcy >= t.y && hcy <= t.y + t.h)
                        {
                            if (!h || hd.confidence > h->confidence)
                                h = &hd;
                        }
                    }
                    aimIsHead = (h != nullptr);
                    if (h)    { aimPx = h->x + h->w * 0.5f; aimPy = h->y + h->h * 0.5f; }
                    else      { aimPx = t.x + t.w * 0.5f;     aimPy = t.y + t.h * s.headFrac; }

                    // --- наведение: НА СКОРОСТИ ДЕТЕКТА, не на UI-фпс ---
                    if (aimHeld)
                    {
                        float dx = aimPx - fovCx;
                        float dy = aimPy - fovCy;
                        float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist > AIM_DEADZONE)
                        {
                            float step = dist * AIM_STRENGTH;
                            if (step > AIM_MAX_STEP) step = AIM_MAX_STEP;
                            MoveMouse(static_cast<int>(std::lround(dx / dist * step)),
                                      static_cast<int>(std::lround(dy / dist * step)));
                        }
                    }
                }
            }
        }

        const Detection* target =
            (targetIdx >= 0 && targetIdx < static_cast<int>(stable.size()))
                ? &stable[targetIdx] : nullptr;

        // --- отрисовка ---
        overlay.BeginDraw();

        int crossX = cap.CropRect().left + static_cast<int>(fovCx);
        int crossY = cap.CropRect().top  + static_cast<int>(fovCy);
        if (s.showCrosshair)
        {
            overlay.DrawFilledRect(crossX - 6, crossY - 1, 13, 3, RGB(255, 255, 255));
            overlay.DrawFilledRect(crossX - 1, crossY - 6, 3, 13, RGB(255, 255, 255));
        }

        if (target && s.showLine)
        {
            // Линия ведёт к точке прицеливания (голове/доле), а не к центру бокса.
            int tx = cap.CropRect().left + static_cast<int>(aimPx);
            int ty = cap.CropRect().top  + static_cast<int>(aimPy);
            overlay.DrawLine(crossX, crossY, tx, ty, RGB(0, 255, 255), 2);
            // Маркер точки прицеливания.
            overlay.DrawFilledRect(tx - 3, ty - 3, 7, 7, RGB(0, 255, 255));
        }

        if (s.showBoxes)
        {
            for (size_t i = 0; i < stable.size(); ++i)
            {
                const auto& d = stable[i];
                int x = cap.CropRect().left + static_cast<int>(d.x);
                int y = cap.CropRect().top  + static_cast<int>(d.y);
                int w = static_cast<int>(d.w);
                int h = static_cast<int>(d.h);

                bool isTarget = (static_cast<int>(i) == targetIdx);
                overlay.DrawRect(x, y, w, h,
                                 isTarget ? RGB(0, 220, 255) : RGB(255, 45, 45),
                                 isTarget ? 3 : 2);

                wchar_t lbl[64];
                swprintf(lbl, 64, L"%.2f", d.confidence);
                overlay.DrawText(x, (y > 18) ? y - 18 : y + 2, lbl, 15,
                                 isTarget ? RGB(0, 200, 255) : RGB(255, 80, 80), true);
            }
        }

        // лог (справа сверху)
        if (s.showLog)
        {
        int panelW = 230, panelH = 140;
        int px = overlay.Width() - panelW - 12, py = 12;
        overlay.DrawFilledRect(px, py, panelW, panelH, RGB(24, 26, 30));
        overlay.DrawRect(px, py, panelW, panelH, RGB(60, 65, 70), 1);

        wchar_t line[128];
        swprintf(line, 128, L"TARGETS: %zu", stable.size());
        overlay.DrawText(px + 10, py + 8,  line, 17, RGB(255, 90, 90), true);
        swprintf(line, 128, L"MODE: %s", det.UseGpu() ? L"DML(GPU)" : L"CPU");
        overlay.DrawText(px + 10, py + 30, line, 15, RGB(255, 200, 90), false);
        swprintf(line, 128, L"AIM: %s %s sm%.2f", aimHeld ? L"ON" : L"OFF",
                 (target && aimIsHead) ? L"HEAD" : L"BODY", s.smoothness);
        overlay.DrawText(px + 10, py + 52, line, 15,
                         aimHeld ? RGB(0, 220, 120) : RGB(140, 140, 140), false);
        swprintf(line, 128, L"DET: %.0f fps", detFps);
        overlay.DrawText(px + 10, py + 74, line, 15, RGB(120, 220, 120), false);
        swprintf(line, 128, L"UI: %.0f fps", uiFps);
        overlay.DrawText(px + 10, py + 96, line, 15, RGB(120, 160, 220), false);
        if (target)
        {
            swprintf(line, 128, L"LOCK: %+.0f, %+.0f",
                     aimPx - fovCx, aimPy - fovCy);
        }
        else
        {
            swprintf(line, 128, L"LOCK: none");
        }
        overlay.DrawText(px + 10, py + 118, line, 15,
                         target ? RGB(0, 255, 255) : RGB(140, 140, 140), false);
        }

        // Зона наводки — рисуем поверх всего (меньше зоны захвата 640).
        if (s.showFov)
        {
            int half = s.aimFov / 2;
            overlay.DrawRect(crossX - half, crossY - half,
                             s.aimFov, s.aimFov, RGB(80, 255, 120), 2);
        }

        overlay.EndDraw();

        // --- FPS-замеры (окно 1 сек) ---
        ++uiCnt;
        {
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - fpsT).count();
            if (dt >= 1.0)
            {
                uiFps = uiCnt / dt;
                detFps = detCnt / dt;
                uiCnt = 0;
                detCnt = 0;
                fpsT = now;
            }
        }

        // --- диагностика: раз в N кадров ---
        if (++dumpTick >= 120)
        {
            dumpTick = 0;
            std::string dbgPath = ConfigPath();
            size_t slash = dbgPath.find_last_of("\\/");
            dbgPath = (slash == std::string::npos ? std::string() : dbgPath.substr(0, slash + 1)) + "debug.log";
            FILE* dbg = nullptr;
            if (fopen_s(&dbg, dbgPath.c_str(), "ab") == 0 && dbg)
            {
                RECT cr = cap.CropRect();
                fprintf(dbg,
                        "stable=%zu mode=%s detfps=%.0f uifps=%.0f maxconf=%.3f crop=(%ld,%ld %ldx%ld)\n",
                        stable.size(), det.UseGpu() ? "DML" : "CPU",
                        detFps, uiFps, det.LastMaxConf(),
                        (long)cr.left, (long)cr.top,
                        (long)(cr.right - cr.left), (long)(cr.bottom - cr.top));
                fclose(dbg);
            }
        }

        // --- рендер меню (ImGui + DX11), если открыто ---
        {
            RuntimeInfo ri;
            ri.targets = static_cast<int>(stable.size());
            ri.aimHeld = aimHeld;
            ri.gpu     = det.UseGpu();
            ri.detFps  = static_cast<float>(detFps);
            ri.uiFps   = static_cast<float>(uiFps);
            ri.maxConf = det.LastMaxConf();
            menu.Render(s, gConfThresh, ri);
        }

        // --- ограничение UI-цикла (~144 кадров/с): меньше холостых
        //     UpdateLayeredWindow -> ниже нагрузка на GPU/CPU в игре. ---
        std::this_thread::sleep_until(
            std::chrono::steady_clock::now() + std::chrono::microseconds(6944));
    }

    // --- останов ---
    running.store(false);
    fc.cv.notify_all();
    if (capThread.joinable())  capThread.join();
    if (infThread.joinable())  infThread.join();

    menu.Destroy();
    overlay.Destroy();
    SaveConfig(s);        // пишем настройки в f6.cfg
    return 0;
}
