#include "YoloDetector.h"

#include <dml_provider_factory.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstring>

YoloDetector::~YoloDetector() = default;

bool YoloDetector::Initialize(const std::wstring& onnxPath, bool enableGpu)
{
    try
    {
        m_enableGpu = enableGpu;

        m_env  = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "aim");
        m_opts = std::make_unique<Ort::SessionOptions>();
        // GPU: 1 поток для CPU-частей (чтобы не отбирать ядра у игры).
        // CPU: все ядра для максимальной скорости инференса.
        m_opts->SetIntraOpNumThreads(m_enableGpu ? 1 : 0);
        m_opts->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Пытаемся включить DirectML (GPU через DirectX 12; RX 5600 XT и пр.).
        bool dml = false;
        if (m_enableGpu)
        {
            try
            {
                OrtSessionOptionsAppendExecutionProvider_DML(*m_opts, 0);
                dml = true;
            }
            catch (...) { dml = false; }
        }

        try
        {
            m_session = std::make_unique<Ort::Session>(*m_env, onnxPath.c_str(), *m_opts);
            m_useDml = dml;
        }
        catch (...)
        {
            // DirectML не потянул (нет GPU/несовместимый op) -> CPU.
            if (dml)
            {
                m_opts = std::make_unique<Ort::SessionOptions>();
                m_opts->SetIntraOpNumThreads(0);      // все ядра CPU
                m_opts->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
                m_session = std::make_unique<Ort::Session>(*m_env, onnxPath.c_str(), *m_opts);
                m_useDml = false;
            }
            else
            {
                throw;
            }
        }

        // Имена входов/выходов + форма выхода (для anchors).
        Ort::AllocatorWithDefaultOptions alloc;
        size_t nIn = m_session->GetInputCount();
        size_t nOut = m_session->GetOutputCount();
        for (size_t i = 0; i < nIn; ++i)
            m_inNames.emplace_back(m_session->GetInputNameAllocated(i, alloc).get());
        for (size_t i = 0; i < nOut; ++i)
            m_outNames.emplace_back(m_session->GetOutputNameAllocated(i, alloc).get());

        if (nOut > 0)
        {
            auto info = m_session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
            auto dims = info.GetShape();
            if (dims.size() >= 3)
            {
                m_outChannels = static_cast<int>(dims[1]);  // 4 + nc
                m_anchors     = static_cast<int>(dims[2]);
            }
            m_nc = std::max(1, m_outChannels - 4);
        }

        // Предвыделяем буферы ОДИН раз — в горячем цикле 0 аллокаций.
        const int HW = m_imgsz * m_imgsz;
        m_inputData.assign(static_cast<size_t>(HW) * 3, 0.f);
        m_outData.assign(static_cast<size_t>(m_anchors) * m_outChannels, 0.f);
        return true;
    }
    catch (...)
    {
        m_session.reset();
        return false;
    }
}

std::vector<Detection> YoloDetector::Detect(const cv::Mat& bgr,
                                            float confThresh, float iouThresh)
{
    if (!m_session || bgr.empty())
        return {};

    const int HW = m_imgsz * m_imgsz;
    const int W = m_imgsz, H = m_imgsz;
    const float inv = 1.0f / 255.0f;

    // Препроцессинг: BGR -> RGB + /255 -> NCHW, ОДНИМ проходом, без alloc.
    const cv::Mat* srcMat = &bgr;
    if (bgr.cols != W || bgr.rows != H)
    {
        cv::resize(bgr, m_resizedBgr, cv::Size(W, H));  // редкий случай
        srcMat = &m_resizedBgr;
    }

    float* dR = m_inputData.data();
    float* dG = m_inputData.data() + static_cast<size_t>(HW);
    float* dB = m_inputData.data() + static_cast<size_t>(HW) * 2;

    const uint8_t* src = srcMat->data;
    const size_t step = srcMat->step;
    int i = 0;
    for (int y = 0; y < H; ++y)
    {
        const uint8_t* s = src + y * step;
        for (int x = 0; x < W; ++x, ++i)
        {
            dB[i] = s[0] * inv;
            dG[i] = s[1] * inv;
            dR[i] = s[2] * inv;
            s += 3;
        }
    }

    Ort::MemoryInfo cpuMem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,
                                                        OrtMemTypeDefault);

    std::vector<int64_t> inShape{ 1, 3, m_imgsz, m_imgsz };
    Ort::Value inT = Ort::Value::CreateTensor<float>(
        cpuMem, m_inputData.data(), m_inputData.size(), inShape.data(), inShape.size());

    // Выходной буфер на CPU: [1, 4+nc, anchors] (IoBinding сам копирует GPU->CPU).
    std::vector<int64_t> outShape{ 1, m_outChannels, m_anchors };
    Ort::Value outT = Ort::Value::CreateTensor<float>(
        cpuMem, m_outData.data(), m_outData.size(), outShape.data(), outShape.size());

    Ort::IoBinding binding{ *m_session };
    binding.BindInput(m_inNames[0].c_str(), inT);
    binding.BindOutput(m_outNames[0].c_str(), outT);
    m_session->Run(Ort::RunOptions{ nullptr }, binding);

    // --- парсинг + NMS (по классам) ---
    const float* p = m_outData.data();
    const int anc = m_anchors;

    struct Cand { float x1, y1, x2, y2, conf; int cls; };
    std::vector<Cand> cands;
    cands.reserve(64);

    float maxConf = 0.f;
    for (int i = 0; i < anc; ++i)
    {
        float cx = p[0 * anc + i];
        float cy = p[1 * anc + i];
        float w  = p[2 * anc + i];
        float h  = p[3 * anc + i];

        // лучший класс + его уверенность
        int   bestCls = 0;
        float bestConf = -1e9f;
        for (int k = 0; k < m_nc; ++k)
        {
            float c = p[(4 + k) * anc + i];
            if (c > bestConf) { bestConf = c; bestCls = k; }
        }
        if (bestConf > maxConf) maxConf = bestConf;
        if (bestConf < confThresh)
            continue;

        // ВАЖНО: ultralytics уже отдаёт боксы в ПИКСЕЛЯХ входного разрешения
        // (0..m_imgsz), а НЕ в нормализованных 0..1. Поэтому никакого
        // умножения на m_imgsz — иначе бокс улетает за экран.
        float hw = w * 0.5f, hh = h * 0.5f;
        cands.push_back({ cx - hw, cy - hh, cx + hw, cy + hh, bestConf, bestCls });
    }
    m_lastMaxConf = maxConf;

    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.conf > b.conf; });

    std::vector<Detection> dets;
    for (const Cand& c : cands)
    {
        bool keep = true;
        for (const Detection& d : dets)
        {
            if (d.classId != c.cls) continue;   // NMS только внутри класса
            float ix1 = std::max(c.x1, d.x);
            float iy1 = std::max(c.y1, d.y);
            float ix2 = std::min(c.x2, d.x + d.w);
            float iy2 = std::min(c.y2, d.y + d.h);
            float inter = std::max(0.f, ix2 - ix1) * std::max(0.f, iy2 - iy1);
            float areaC = (c.x2 - c.x1) * (c.y2 - c.y1);
            float areaD = d.w * d.h;
            float uni   = areaC + areaD - inter;
            if (uni > 0.f && inter / uni > iouThresh) { keep = false; break; }
        }
        if (keep)
            dets.push_back({ c.x1, c.y1, c.x2 - c.x1, c.y2 - c.y1, c.conf, c.cls });
    }

    return dets;
}
