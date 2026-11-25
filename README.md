# aim

Детекция целей на экране в реальном времени через Windows Desktop Duplication API (`DXGI 1.2`) и YOLO (`ONNX Runtime` + `DirectML`), с прозрачным оверлеем и отдельным меню настроек на `DX11 + ImGui`.

> Готовую сборку под Windows x64 (с runtime DLL и обученной моделью `best.onnx`) можно скачать в разделе **Releases**.

## Как это работает

Конвейер разделён на три потока с минимальными блокировками:

```
[Захват]     DXGI Desktop Duplication -> кроп 640x640 по центру на GPU -> SSSE3 BGRA->BGR
   │
[Инференс]   забирает самый свежий кадр -> YOLOv8/v11 (DirectML GPU или CPU) -> детекции
   │
[Main]       IoU-трекер + EMA-сглаживание + гистерезис цели -> SendInput + GDI-оверлей + ImGui-меню
```

- **Захват (`ScreenCapture`)** — копирует только центральный FOV (`CopySubresourceRegion`), не выгружая весь экран в RAM; обрабатывает `DXGI_ERROR_ACCESS_LOST`.
- **Детектор (`YoloDetector`)** — гоняет ONNX-модель через DirectML (работает и на NVIDIA, и на AMD) с автофолбэком на CPU и предвыделенными буферами без аллокаций в hot path.
- **Оверлей (`Overlay`)** — layered-окно поверх всех окон (`WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`), не перехватывает клики и клавиатуру.
- **Меню (`Menu`)** — отдельное окно на `DirectX 11` + `Dear ImGui 1.91.9`, открывается по `N`. Настройки сохраняются в `f6.cfg` рядом с `.exe`.

## Управление

| Клавиша | Действие |
|---|---|
| `N` | Показать / скрыть меню настроек |
| `ЛКМ` / `Alt` (по умолчанию) | Наведение при удержании (переназначается в меню) |
| `F8` | Выход из программы |

## Сборка из исходников

Требования: Windows 10/11, Visual Studio (MSVC, C++17), `CMake >= 3.20`, `OpenCV` (`core` + `imgproc`) и `ONNX Runtime DirectML`.

```powershell
cmake -S . -B build -A x64 -DORT_ROOT="C:/path/to/onnxruntime-directml" -DOpenCV_DIR="C:/path/to/opencv/build"
cmake --build build --config Release
```

Рядом с `capture_demo.exe` должны лежать модель `best.onnx`, шрифт `fonts/fa-solid-900.ttf` и runtime DLL (`onnxruntime*.dll`, `opencv_core*.dll`, `opencv_imgproc*.dll`).

Запуск с явным путём к модели:

```powershell
capture_demo.exe path\to\best.onnx
```

## Обучение модели

В папке [`training/`](training/) лежит полный пайплайн под YOLO: сбор кадров (`collect.py`, `collect_data.py`, `collect_background.py`), сортировка/разметка (`sort_frames.py`, `label.py`, `autolabel.py`), упаковка архива для Google Colab (`make_colab_zip.py`, `colab_train.ipynb`) и локальный скрипт обучения (`train.py`). Подробнее — в [`training/README.md`](training/README.md).

## Структура проекта

```
ScreenCapture.cpp/.h    DXGI Desktop Duplication + GPU-кроп + SSSE3 BGRA->BGR
YoloDetector.cpp/.h     ONNX Runtime (DirectML / CPU) + постпроцессинг + NMS
Overlay.cpp/.h          прозрачный кликабельно-сквозной GDI-оверлей
Menu.cpp/.h             отдельное окно настроек на DX11 + Dear ImGui
Settings.h              параметры наведения, FOV, отрисовки и горячих клавиш
main.cpp                3-поточный цикл, IoU-трекер, наведение и HUD
training/               скрипты сбора датасета и обучения YOLO
third_party/            vendored Dear ImGui 1.91.9
```

## Лицензия

[MIT](LICENSE)
