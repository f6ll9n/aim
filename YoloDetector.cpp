#include "YoloDetector.h"

#include <dml_provider_factory.h>
#include <opencv2/imgproc.hpp>
#include <tmmintrin.h>

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
        m_opts->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        bool dml = false;
        if (m_enableGpu)
        {
            try
            {
                // Рекомендуемые настройки DirectML EP для стабильного низкого latency.
                m_opts->DisableMemPattern();
                m_opts->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
                m_opts->SetIntraOpNumThreads(1);
                OrtSessionOptionsAppendExecutionProvider_DML(*m_opts, 0);
                dml = true;
            }
            catch (...) { dml = false; }
        }
        else
        {
            m_opts->SetIntraOpNumThreads(0);
        }

        try
        {
            m_session = std::make_unique<Ort::Session>(*m_env, onnxPath.c_str(), *m_opts);
            m_useDml = dml;
        }
        catch (...)
        {
            if (dml)
            {
                m_opts = std::make_unique<Ort::SessionOptions>();
                m_opts->SetIntraOpNumThreads(0);
                m_opts->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
                m_session = std::make_unique<Ort::Session>(*m_env, onnxPath.c_str(), *m_opts);
                m_useDml = false;
            }
            else
            {
                throw;
            }
        }

        Ort::AllocatorWithDefaultOptions alloc;
        const size_t nIn  = m_session->GetInputCount();
        const size_t nOut = m_session->GetOutputCount();
        m_inNames.clear();
        m_outNames.clear();
        m_inNamePtrs.clear();
        m_outNamePtrs.clear();
        for (size_t i = 0; i < nIn; ++i)
            m_inNames.emplace_back(m_session->GetInputNameAllocated(i, alloc).get());
        for (size_t i = 0; i < nOut; ++i)
            m_outNames.emplace_back(m_session->GetOutputNameAllocated(i, alloc).get());
        for (const auto& s : m_inNames)  m_inNamePtrs.push_back(s.c_str());
        for (const auto& s : m_outNames) m_outNamePtrs.push_back(s.c_str());

        if (nIn > 0)
        {
            auto inInfo = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
            auto inDims = inInfo.GetShape();
            if (inDims.size() >= 4 && inDims[2] > 0 && inDims[3] > 0)
                m_imgsz = static_cast<int>(inDims[2]);
        }

        if (nOut > 0)
        {
            auto outInfo = m_session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
            auto dims = outInfo.GetShape();
            if (dims.size() >= 3)
            {
                m_outChannels = static_cast<int>(dims[1]);
                m_anchors     = static_cast<int>(dims[2]);
            }
            m_nc = std::max(1, m_outChannels - 4);
        }

        // Предвыделяем буферы и переиспользуемые тензоры + IoBinding ОДИН раз.
        const int HW = m_imgsz * m_imgsz;
        m_inputData.assign(static_cast<size_t>(HW) * 3, 0.f);
        m_outData.assign(static_cast<size_t>(m_anchors) * m_outChannels, 0.f);

        m_cpuMem = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        const int64_t inShape[4]  = { 1, 3, m_imgsz, m_imgsz };
        const int64_t outShape[3] = { 1, m_outChannels, m_anchors };

        m_inTensor = Ort::Value::CreateTensor<float>(
            *m_cpuMem, m_inputData.data(), m_inputData.size(), inShape, 4);
        m_outTensor = Ort::Value::CreateTensor<float>(
            *m_cpuMem, m_outData.data(), m_outData.size(), outShape, 3);

        m_runOpts = Ort::RunOptions{ nullptr };
        return true;
    }
    catch (...)
    {
        m_session.reset();
        return false;
    }
}

void YoloDetector::PreprocessBGRA(const cv::Mat& srcMat)
{
    const int W = m_imgsz, H = m_imgsz;
    const int HW = W * H;

    float* dR = m_inputData.data();
    float* dG = dR + static_cast<size_t>(HW);
    float* dB = dR + static_cast<size_t>(HW) * 2;

    const __m128i shuf = _mm_setr_epi8(
        0, 4, 8, 12,   1, 5, 9, 13,   2, 6, 10, 14,   -1, -1, -1, -1);
    const __m128i zero = _mm_setzero_si128();
    const __m128  vInv = _mm_set1_ps(1.0f / 255.0f);
    const float   inv  = 1.0f / 255.0f;

    const uint8_t* src = srcMat.data;
    const size_t step  = srcMat.step;

    int idx = 0;
    for (int y = 0; y < H; ++y)
    {
        const uint8_t* s = src + static_cast<size_t>(y) * step;
        int x = 0;
        for (; x + 4 <= W; x += 4, idx += 4, s += 16)
        {
            __m128i px      = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
            __m128i grouped = _mm_shuffle_epi8(px, shuf);
            __m128i lo16    = _mm_unpacklo_epi8(grouped, zero);
            __m128i hi16    = _mm_unpackhi_epi8(grouped, zero);

            __m128i b32 = _mm_unpacklo_epi16(lo16, zero);
            __m128i g32 = _mm_unpackhi_epi16(lo16, zero);
            __m128i r32 = _mm_unpacklo_epi16(hi16, zero);

            _mm_storeu_ps(dB + idx, _mm_mul_ps(_mm_cvtepi32_ps(b32), vInv));
            _mm_storeu_ps(dG + idx, _mm_mul_ps(_mm_cvtepi32_ps(g32), vInv));
            _mm_storeu_ps(dR + idx, _mm_mul_ps(_mm_cvtepi32_ps(r32), vInv));
        }
        for (; x < W; ++x, ++idx, s += 4)
        {
            dB[idx] = s[0] * inv;
            dG[idx] = s[1] * inv;
            dR[idx] = s[2] * inv;
        }
    }
}

void YoloDetector::PreprocessBGR(const cv::Mat& srcMat)
{
    const int W = m_imgsz, H = m_imgsz;
    const int HW = W * H;

    float* dR = m_inputData.data();
    float* dG = dR + static_cast<size_t>(HW);
    float* dB = dR + static_cast<size_t>(HW) * 2;

    const __m128i shuf = _mm_setr_epi8(
        0, 3, 6, 9,   1, 4, 7, 10,   2, 5, 8, 11,   -1, -1, -1, -1);
    const __m128i zero = _mm_setzero_si128();
    const __m128  vInv = _mm_set1_ps(1.0f / 255.0f);
    const float   inv  = 1.0f / 255.0f;

    const uint8_t* src = srcMat.data;
    const size_t step  = srcMat.step;

    int idx = 0;
    for (int y = 0; y < H; ++y)
    {
        const uint8_t* s = src + static_cast<size_t>(y) * step;
        int x = 0;
        // Читаем 16 байт, поэтому останавливаемся за >= 6 пикселей (18 байт) до конца строки.
        for (; x + 6 <= W; x += 4, idx += 4, s += 12)
        {
            __m128i px      = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
            __m128i grouped = _mm_shuffle_epi8(px, shuf);
            __m128i lo16    = _mm_unpacklo_epi8(grouped, zero);
            __m128i hi16    = _mm_unpackhi_epi8(grouped, zero);

            __m128i b32 = _mm_unpacklo_epi16(lo16, zero);
            __m128i g32 = _mm_unpackhi_epi16(lo16, zero);
            __m128i r32 = _mm_unpacklo_epi16(hi16, zero);

            _mm_storeu_ps(dB + idx, _mm_mul_ps(_mm_cvtepi32_ps(b32), vInv));
            _mm_storeu_ps(dG + idx, _mm_mul_ps(_mm_cvtepi32_ps(g32), vInv));
            _mm_storeu_ps(dR + idx, _mm_mul_ps(_mm_cvtepi32_ps(r32), vInv));
        }
        for (; x < W; ++x, ++idx, s += 3)
        {
            dB[idx] = s[0] * inv;
            dG[idx] = s[1] * inv;
            dR[idx] = s[2] * inv;
        }
    }
}

std::vector<Detection> YoloDetector::Detect(const cv::Mat& frame,
                                            float confThresh, float iouThresh)
{
    if (!m_session || m_inNamePtrs.empty() || m_outNamePtrs.empty() || frame.empty())
        return {};

    const cv::Mat* srcMat = &frame;
    if (frame.cols != m_imgsz || frame.rows != m_imgsz)
    {
        cv::resize(frame, m_resized, cv::Size(m_imgsz, m_imgsz), 0, 0, cv::INTER_LINEAR);
        srcMat = &m_resized;
    }

    if (srcMat->channels() == 4)
        PreprocessBGRA(*srcMat);
    else
        PreprocessBGR(*srcMat);

    m_session->Run(m_runOpts,
                   m_inNamePtrs.data(),  &m_inTensor,  1,
                   m_outNamePtrs.data(), &m_outTensor, 1);

    // --- парсинг + NMS (по классам) ---
    const float* p   = m_outData.data();
    const int    anc = m_anchors;
    const float* pX  = p;
    const float* pY  = p + anc;
    const float* pW  = p + anc * 2;
    const float* pH  = p + anc * 3;
    const float* pC  = p + anc * 4;

    struct Cand { float x1, y1, x2, y2, conf; int cls; };
    std::vector<Cand> cands;
    cands.reserve(32);

    float maxConf = 0.f;
    for (int i = 0; i < anc; ++i)
    {
        int   bestCls  = 0;
        float bestConf = pC[i];
        for (int k = 1; k < m_nc; ++k)
        {
            float c = pC[k * anc + i];
            if (c > bestConf) { bestConf = c; bestCls = k; }
        }
        if (bestConf > maxConf) maxConf = bestConf;
        if (bestConf < confThresh)
            continue;

        float cx = pX[i];
        float cy = pY[i];
        float hw = pW[i] * 0.5f;
        float hh = pH[i] * 0.5f;
        cands.push_back({ cx - hw, cy - hh, cx + hw, cy + hh, bestConf, bestCls });
    }
    m_lastMaxConf = maxConf;

    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.conf > b.conf; });

    std::vector<Detection> dets;
    dets.reserve(cands.size());
    for (const Cand& c : cands)
    {
        bool keep = true;
        for (const Detection& d : dets)
        {
            if (d.classId != c.cls) continue;
            float ix1   = std::max(c.x1, d.x);
            float iy1   = std::max(c.y1, d.y);
            float ix2   = std::min(c.x2, d.x + d.w);
            float iy2   = std::min(c.y2, d.y + d.h);
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
