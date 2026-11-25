#pragma once
#include <Windows.h>

// ============================================================================
//  Settings — настройки айма/оверлея, крутятся через меню.
//
//  Что читается только в главном потоке (точка, плавность, FOV, клавиши, флаги
//  отрисовки) — обычные поля. Пороги детекции (conf/iou) читает ещё поток
//  инференса, поэтому они std::atomic<float> в main.cpp, сюда НЕ пихаем. Размер
//  FOV читает поток захвата через std::atomic<int> — тоже мимо.
// ============================================================================

struct AimSettings
{
    // --- наведение ---
    float headFrac   = 0.30f;  // высота точки (доля высоты бокса)
    float headConf   = 0.55f;  // мин. увер. головы, чтоб целиться в башку
                               // (ниже — падаем на тело: headFrac)
    float smoothness = 0.15f;  // плавность (0 = резко, больше = плавнее)

    // --- зона наводки (квадрат вокруг прицела, меньше зоны захвата 640) ---
    int aimFov = 400;

    // --- отрисовка ---
    bool showCrosshair = true;  // крестик по центру
    bool showBoxes     = true;  // боксы целей
    bool showLine      = true;  // линия до цели
    bool showLog       = true;  // лог-панель справа сверху
    bool showFov       = true;  // квадрат зоны наводки

    // --- клавиши ---
    int aimKey1 = VK_LBUTTON;   // основная (зажать)
    int aimKey2 = VK_MENU;      // доп (Alt)

    // границы ползунков
    static constexpr float HD_MIN = 0.00f, HD_MAX = 0.50f;
    static constexpr float HC_MIN = 0.05f, HC_MAX = 0.95f;
    static constexpr float SM_MIN = 0.00f, SM_MAX = 0.90f;
    static constexpr int   AIMFOV_MIN = 40, AIMFOV_MAX = 640;
};

// снапшот для инфо-вкладки (заполняется в главном цикле).
struct RuntimeInfo
{
    int   targets  = 0;
    bool  aimHeld  = false;
    bool  gpu      = false;
    float detFps   = 0.f;
    float uiFps    = 0.f;
    float maxConf  = 0.f;
};
