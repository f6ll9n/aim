#pragma once
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

#include <memory>
#include <vector>
#include <string>

// Результат детекции: бокс в координатах FOV (пиксели 0..inputSize).
struct Detection
{
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    float confidence = 0.f;
    int   classId = 0;   // 0 = player, 1 = head
};

// ============================================================================
//  YoloDetector — YOLOv8 ONNX через ONNX Runtime.
//  GPU: DirectML (AMD/NVIDIA, работает через DirectX 12); фолбэк на CPU.
//  Вход: BGRA (CV_8UC4) или BGR (CV_8UC3). Выход [1, 4+nc, anchors].
// ============================================================================
class YoloDetector
{
public:
    ~YoloDetector();

    // onnxPath — путь к best.onnx (wide, как требует onnxruntime на Windows).
    // enableGpu — пробовать DirectML (false = принудительно CPU).
    bool Initialize(const std::wstring& onnxPath, bool enableGpu = true);

    std::vector<Detection> Detect(const cv::Mat& frame,
                                  float confThresh = 0.5f,
                                  float iouThresh  = 0.45f);

    bool Loaded() const noexcept { return m_session != nullptr; }
    bool UseGpu() const noexcept { return m_useDml; }
    int  InputSize() const noexcept { return m_imgsz; }

    // Максимальная уверенность среди всех якорей последнего кадра (диагностика).
    float LastMaxConf() const noexcept { return m_lastMaxConf; }

private:
    void PreprocessBGRA(const cv::Mat& src);
    void PreprocessBGR(const cv::Mat& src);

    std::unique_ptr<Ort::Env>            m_env;
    std::unique_ptr<Ort::SessionOptions> m_opts;
    std::unique_ptr<Ort::Session>        m_session;
    std::unique_ptr<Ort::MemoryInfo>     m_cpuMem;
    Ort::Value                           m_inTensor{ nullptr };
    Ort::Value                           m_outTensor{ nullptr };
    Ort::RunOptions                      m_runOpts{ nullptr };

    std::vector<std::string> m_inNames;
    std::vector<std::string> m_outNames;
    std::vector<const char*> m_inNamePtrs;
    std::vector<const char*> m_outNamePtrs;
    bool  m_useDml      = false;
    bool  m_enableGpu   = true;
    int   m_imgsz       = 640;
    int   m_anchors     = 8400;   // заполняется из shape модели
    int   m_outChannels = 5;      // 4 бокса + nc классов (из shape)
    int   m_nc          = 1;      // число классов
    float m_lastMaxConf = 0.f;

    // Предвыделенные буферы, чтобы в hot-пути не было ни одной аллокации.
    std::vector<float> m_inputData;   // 3*imgsz*imgsz (NCHW, RGB, x/255)
    std::vector<float> m_outData;     // (4+nc)*anchors
    cv::Mat            m_resized;     // для редкого случая неверного размера
};
