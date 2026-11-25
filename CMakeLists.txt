cmake_minimum_required(VERSION 3.20)
project(screen-capture-yolo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "Desktop Duplication API — только Windows/MSVC")
endif()

# OpenCV нужен только для демо (capture_demo), ядро библиотеки от него не
# зависит. Ищем необязательно, чтобы конфигурация не падала при отсутствии.
find_package(OpenCV QUIET)

# Файлы лежат в корне проекта (не в src/).
add_library(screen_capture STATIC
    ScreenCapture.cpp ScreenCapture.h)
target_compile_options(screen_capture PRIVATE /O2 /arch:SSE2)
target_link_libraries(screen_capture PRIVATE d3d11 dxgi)

if(OpenCV_FOUND)
    # ONNX Runtime (prebuilt DirectML) — ускорение инференса на AMD/NVIDIA GPU.
    # Путь можно передать через -DORT_ROOT=... или переменную окружения ORT_ROOT.
    if(NOT DEFINED ORT_ROOT)
        if(DEFINED ENV{ORT_ROOT})
            set(ORT_ROOT "$ENV{ORT_ROOT}")
        else()
            set(ORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-directml")
        endif()
    endif()

    # ImGui (vendored) + DX11/win32 backends — меню настроек.
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-1.91.9")

    add_executable(capture_demo
        main.cpp
        Overlay.cpp Overlay.h
        YoloDetector.cpp YoloDetector.h
        Menu.cpp Menu.h Settings.h
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
    # только core+imgproc — код использует cv::Mat/cv::resize, остальные модули не нужны
    # и тянут лишние кодеки (jpeg/png/webp/tiff/z/lzma).
    target_link_libraries(capture_demo PRIVATE
        screen_capture opencv_core opencv_imgproc user32 gdi32 onnxruntime
        d3d11 dxgi d3dcompiler)
    target_include_directories(capture_demo PRIVATE
        ${OpenCV_INCLUDE_DIRS} "." "${ORT_ROOT}/build/native/include"
        "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_link_directories(capture_demo PRIVATE "${ORT_ROOT}/runtimes/win-x64/native")
    # windows.h определяет макросы min/max, ломающие std::min/max (onnxruntime/OpenCV).
    target_compile_definitions(capture_demo PRIVATE NOMINMAX)
    set_target_properties(capture_demo PROPERTIES WIN32_EXECUTABLE TRUE)  # без консоли (WinMain)
else()
    message(WARNING
        "OpenCV не найден — собираю только библиотеку screen_capture "
        "(демо capture_demo пропущено).\n"
        "Чтобы собрать демо, установите OpenCV одним из способов:\n"
        "  1) vcpkg:  vcpkg install opencv4:x64-windows\n"
        "     и передайте в CMake:\n"
        "       -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake\n"
        "  2) предсобранный билд: https://github.com/opencv/opencv/releases\n"
        "     распакуйте и укажите путь к конфигу:\n"
        "       -DOpenCV_DIR=C:/opencv/build\n")
endif()
