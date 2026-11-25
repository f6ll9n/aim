# aim

Детектор целей для **Pixel Gun 3D** в реальном времени: захват центральной зоны экрана (640×640) через Windows Desktop Duplication API (`DXGI 1.2`), инференс YOLO через ONNX Runtime (`DirectML` на GPU с фолбэком на CPU), прозрачный GDI-оверлей и отдельное меню настроек на `DX11 + Dear ImGui`.

Готовая сборка под Windows x64 с обученной под Pixel Gun 3D моделью `best.onnx` и runtime DLL лежит в **Releases**.

## Как устроено

Работает в три потока:
1. `ScreenCapture` — снимает только центральный кроп на GPU (`CopySubresourceRegion`), конвертирует `BGRA -> BGR` через SSSE3 и отдаёт свежий кадр.
2. `YoloDetector` — забирает последний кадр (пропуская старые, если инференс не успевает), гоняет ONNX-модель (классы `player` и `head`) и делает NMS.
3. Главный цикл — стабилизирует детекции простым IoU-трекером + EMA, ведёт мышь через `SendInput` (в голову при уверенности выше порога, иначе по доле бокса), рисует боксы на кликабельно-сквозном оверлее и крутит ImGui-меню.

Настройки сохраняются в `f6.cfg` рядом с `.exe`.

## Клавиши

- `N` — открыть/скрыть меню
- `ЛКМ` / `Alt` (по умолчанию) — наведение при удержании, переназначается в меню
- `F8` — выход

## Сборка

Нужны Windows 10/11, MSVC (C++17), `CMake >= 3.20`, `OpenCV` (`core` + `imgproc`) и `ONNX Runtime DirectML`.

```powershell
cmake -S . -B build -A x64 -DORT_ROOT="C:/path/to/onnxruntime-directml" -DOpenCV_DIR="C:/path/to/opencv/build"
cmake --build build --config Release
```

Рядом с `capture_demo.exe` должны лежать `best.onnx`, `fonts/fa-solid-900.ttf` и DLL из OpenCV / ONNX Runtime. Путь к модели можно передать аргументом: `capture_demo.exe path\to\best.onnx`.

## Обучение

Скрипты сбора кадров, разметки и обучения YOLO (локально или в Colab) лежат в `training/`, инструкция там же в `training/README.md`.
