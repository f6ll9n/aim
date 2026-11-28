#pragma once
#include <Windows.h>

// ============================================================================
//  Settings — настройки айма/оверлея, крутятся через меню и сохраняются в f6.cfg.
// ============================================================================

struct AimSettings
{
    // --- наведение ---
    bool  aimEnabled  = true;   // общий тумблер наведения
    bool  stickyLock  = true;   // держит цель пока жива в FOV; при смерти автосвап на следующую
    int   headMode    = 0;      // 0 = голова -> тело, 1 = только голова, 2 = только тело
    float headFrac    = 0.30f;  // высота точки (доля высоты бокса, если нет head)
    float headConf    = 0.55f;  // мин. увер. головы, чтоб целиться в башку
    float smoothness  = 0.15f;  // сглаживание трекера (0 = резко, выше = плавнее)
    float aimStrength = 0.15f;  // сила доводки (доля дистанции за тик)
    float aimDeadzone = 8.0f;   // радиус мёртвой зоны (px)
    float aimMaxStep  = 25.0f;  // макс. шаг мыши за один тик (px)

    // --- зона наводки (квадрат вокруг прицела, <= зоны захвата 640) ---
    int aimFov = 400;

    // --- отрисовка ---
    bool showCrosshair = true;  // крестик по центру
    bool showBoxes     = true;  // боксы целей
    bool showHeadBoxes = true;  // боксы голов (класс 1)
    bool showLine      = true;  // линия до цели
    bool showLog       = true;  // лог-панель справа сверху
    bool showFov       = true;  // квадрат зоны наводки

    // --- производительность и поведение меню ---
    int  detMaxFps = 120;       // лимит YOLO FPS (40..240, 240 = без лимита; бережёт GPU для игры)
    int  uiMaxFps  = 144;       // лимит UI FPS (60..240, 240 = без лимита)
    bool grabFocus = false;     // false = меню открывается поверх игры без сворачивания (NoActivate)

    // --- клавиши (0 = отключена) ---
    int aimKey1 = VK_LBUTTON;   // основная (зажать)
    int aimKey2 = VK_MENU;      // доп (Alt)

    // границы ползунков
    static constexpr float HD_MIN = 0.00f, HD_MAX = 0.50f;
    static constexpr float HC_MIN = 0.05f, HC_MAX = 0.95f;
    static constexpr float SM_MIN = 0.00f, SM_MAX = 0.90f;
    static constexpr float ST_MIN = 0.05f, ST_MAX = 0.60f;
    static constexpr float DZ_MIN = 0.0f,  DZ_MAX = 30.0f;
    static constexpr float MS_MIN = 5.0f,  MS_MAX = 80.0f;
    static constexpr int   AIMFOV_MIN = 40, AIMFOV_MAX = 640;
    static constexpr int   DETFPS_MIN = 40, DETFPS_MAX = 240;
    static constexpr int   UIFPS_MIN  = 60, UIFPS_MAX  = 240;
};

// снапшот для инфо-вкладки (заполняется в главном цикле).
struct RuntimeInfo
{
    int   targets  = 0;
    bool  aimHeld  = false;
    bool  gpu      = false;
    float capFps   = 0.f;
    float detFps   = 0.f;
    float infMs    = 0.f;
    float uiFps    = 0.f;
    float maxConf  = 0.f;
};
