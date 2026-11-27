# aim

Детектор целей для **Pixel Gun 3D** в реальном времени: захват центральной зоны экрана (`640×640`) через Windows Desktop Duplication API (`DXGI 1.2`), инференс YOLOv8 через ONNX Runtime (`DirectML` на GPU с фолбэком на CPU), аппаратный прозрачный оверлей на `Direct2D + DirectComposition` и меню настроек в стиле Skeet/GameSense на `DX11 + Dear ImGui`.

Готовая сборка под Windows x64 с обученной моделью `best.onnx` и runtime DLL лежит в **Releases**.

## Как устроено

Работает в три потока с zero-copy передачей кадров (`swap` буферов):
1. `ScreenCapture` — снимает центральный кроп в нативном `BGRA` на GPU (`CopySubresourceRegion`) и отдаёт свежий кадр.
2. `YoloDetector` + трекер — забирает самый свежий кадр, делает прямой SSSE3-препроцессинг `BGRA -> RGB NCHW`, гоняет ONNX-модель (классы `player` и `head`), стабилизирует боксы IoU-трекером + EMA и сразу выполняет наведение через `SendInput` (без задержки UI-цикла).
3. Главный цикл — рисует кликабельно-сквозной оверлей (`Direct2D 1.1 + DirectComposition`, `Present(0, 0)` с точным high-res таймером под лимит UI FPS) и крутит ImGui-меню.

Настройки сохраняются в `f6.cfg` рядом с `.exe` (автоматически при закрытии меню или выходе, а также кнопкой `Save`).

## Клавиши

- `N` или `Insert` — открыть/скрыть меню (по умолчанию с `WS_EX_NOACTIVATE`, поверх игры в режиме **«Без рамок» / Borderless** без сворачивания)
- `ЛКМ` / `Alt` (по умолчанию) — наведение при удержании, переназначается в меню (`ПКМ` по доп. клавише отключает её)
- `F8` (или кнопка `Unload` в меню) — выход

## Сборка

Нужны Windows 10/11, MSVC (C++17), `CMake >= 3.20`, `OpenCV` (`core` + `imgproc`) и `ONNX Runtime DirectML`.

```powershell
cmake -S . -B build -A x64 -DORT_ROOT="C:/path/to/onnxruntime-directml" -DOpenCV_DIR="C:/path/to/opencv/build"
cmake --build build --config Release
```

Рядом с `capture_demo.exe` должны лежать `best.onnx`, `fa-solid-900.ttf` (или папка `fonts/`), `onnxruntime.dll`, `onnxruntime_providers_shared.dll`, `opencv_core4.dll`, `opencv_imgproc4.dll` и `z.dll`. Путь к модели можно передать аргументом: `capture_demo.exe path\to\best.onnx`.

## Обучение

Скрипты сбора кадров, разметки и обучения YOLO (локально или в Colab) лежат в `training/`, инструкция — в [training/README.md](training/README.md).
