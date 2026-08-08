#include "AudioCapture.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

namespace {

constexpr size_t kRingMask = AudioCapture::kRingCapacity - 1u;
static_assert((AudioCapture::kRingCapacity & (AudioCapture::kRingCapacity - 1u)) == 0,
              "AudioCapture ring must be power of two");

void writeWavFloat32Mono(const std::string& path, double sampleRate, const std::vector<float>& pcm)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot open wav path");

    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(float));
    const uint32_t fmtChunkSize = 16;
    const uint16_t audioFormat = 3; // IEEE float
    const uint16_t channels = 1;
    const uint32_t sr = static_cast<uint32_t>(std::lround(sampleRate));
    const uint16_t bitsPerSample = 32;
    const uint32_t byteRate = sr * channels * (bitsPerSample / 8);
    const uint16_t blockAlign = channels * (bitsPerSample / 8);
    const uint32_t riffSize = 4 + (8 + fmtChunkSize) + (8 + dataBytes);

    auto w4 = [&](const char* s) { f.write(s, 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };

    w4("RIFF");
    u32(riffSize);
    w4("WAVE");
    w4("fmt ");
    u32(fmtChunkSize);
    u16(audioFormat);
    u16(channels);
    u32(sr);
    u32(byteRate);
    u16(blockAlign);
    u16(bitsPerSample);
    w4("data");
    u32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(dataBytes));
}

} // namespace

void audioCaptureDataCallback(ma_device* pDevice, void* /*pOutput*/, const void* pInput, ma_uint32 frameCount)
{
    auto* self = reinterpret_cast<AudioCapture*>(pDevice ? pDevice->pUserData : nullptr);
    if (!self || !pInput || frameCount == 0) return;
    const float* in = reinterpret_cast<const float*>(pInput);
    const ma_uint32 ch = pDevice->capture.channels ? pDevice->capture.channels : 1;
    self->processCapturedFrames(in, frameCount, ch);
}

AudioCapture::AudioCapture()
{
    m_ring.assign(kRingCapacity, 0.0f);
    if (ma_context_init(nullptr, 0, nullptr, &m_context) != MA_SUCCESS) {
        spdlog::error("AudioCapture: ma_context_init failed");
        m_contextValid = false;
        return;
    }
    m_contextValid = true;
}

AudioCapture::~AudioCapture()
{
    stop();
    if (m_contextValid) {
        ma_context_uninit(&m_context);
        m_contextValid = false;
    }
}

std::vector<AudioCaptureDeviceInfo> AudioCapture::enumerateCaptureDevices()
{
    std::lock_guard<std::mutex> lk(m_enumMutex);
    m_devices.clear();
    if (!m_contextValid) return m_devices;

    ma_device_info* pCaptureInfos = nullptr;
    ma_uint32 captureCount = 0;
    ma_device_info* pPlaybackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(&m_context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS) {
        spdlog::warn("AudioCapture: ma_context_get_devices failed");
        return m_devices;
    }

    ma_device_info defaultInfo{};
    const bool haveDefault =
        ma_context_get_device_info(&m_context, ma_device_type_capture, nullptr, &defaultInfo) == MA_SUCCESS;

    for (ma_uint32 i = 0; i < captureCount; ++i) {
        AudioCaptureDeviceInfo info;
        info.id = pCaptureInfos[i].id;
        info.name = pCaptureInfos[i].name ? pCaptureInfos[i].name : "Capture";
        if (haveDefault) {
            info.isDefault = (std::memcmp(&pCaptureInfos[i].id, &defaultInfo.id, sizeof(ma_device_id)) == 0);
        }
        m_devices.push_back(info);
    }
    return m_devices;
}

bool AudioCapture::start(int enumIndex, double sampleRateHz)
{
    stop();
    if (!m_contextValid) return false;

    auto devices = enumerateCaptureDevices();
    if (devices.empty()) {
        spdlog::warn("AudioCapture: no capture devices");
        return false;
    }

    int idx = enumIndex;
    if (idx < 0 || static_cast<size_t>(idx) >= devices.size()) {
        idx = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].isDefault) {
                idx = static_cast<int>(i);
                break;
            }
        }
    }

    if (!std::isfinite(sampleRateHz) || sampleRateHz < 8000.0) sampleRateHz = 48000.0;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = static_cast<ma_uint32>(std::lround(sampleRateHz));
    cfg.dataCallback = audioCaptureDataCallback;
    cfg.pUserData = this;
    cfg.capture.pDeviceID = &devices[static_cast<size_t>(idx)].id;

    std::memset(&m_device, 0, sizeof(m_device));
    if (ma_device_init(&m_context, &cfg, &m_device) != MA_SUCCESS) {
        // Retry default device
        cfg.capture.pDeviceID = nullptr;
        if (ma_device_init(&m_context, &cfg, &m_device) != MA_SUCCESS) {
            spdlog::error("AudioCapture: ma_device_init failed");
            return false;
        }
        idx = -1;
    }

    if (ma_device_start(&m_device) != MA_SUCCESS) {
        spdlog::error("AudioCapture: ma_device_start failed");
        ma_device_uninit(&m_device);
        return false;
    }

    m_deviceActive = true;
    m_sampleRate.store(static_cast<double>(m_device.sampleRate ? m_device.sampleRate : cfg.sampleRate),
                       std::memory_order_relaxed);
    m_activeEnumIndex.store(idx, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(m_nameMutex);
        if (idx >= 0 && static_cast<size_t>(idx) < devices.size()) {
            m_activeName = devices[static_cast<size_t>(idx)].name;
        } else {
            m_activeName = "Default capture";
        }
    }
    m_writePos.store(0, std::memory_order_relaxed);
    m_readPos.store(0, std::memory_order_relaxed);
    m_framesCaptured.store(0, std::memory_order_relaxed);
    m_resampPhase = 0.0;
    m_agcGain.store(1.0f, std::memory_order_relaxed);

    {
        const std::string name = activeDeviceName();
        const double sr = m_sampleRate.load(std::memory_order_relaxed);
        spdlog::info("AudioCapture started: device='{}' sr={:.0f} Hz", name, sr);
    }
    return true;
}

void AudioCapture::stop()
{
    if (!m_deviceActive) return;
    ma_device_stop(&m_device);
    ma_device_uninit(&m_device);
    m_deviceActive = false;
    m_activeEnumIndex.store(-1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(m_nameMutex);
        m_activeName.clear();
    }
    spdlog::info("AudioCapture stopped");
}

bool AudioCapture::isCapturing() const noexcept
{
    return m_deviceActive;
}

std::string AudioCapture::activeDeviceName() const
{
    std::lock_guard<std::mutex> lk(m_nameMutex);
    return m_activeName;
}

void AudioCapture::setGain(float gain)
{
    if (!std::isfinite(gain)) gain = 1.0f;
    gain = std::max(0.0f, std::min(4.0f, gain));
    m_userGain.store(gain, std::memory_order_relaxed);
}

void AudioCapture::processCapturedFrames(const float* input, ma_uint32 frameCount, ma_uint32 channels)
{
    if (!input || frameCount == 0) return;

    // Downmix to mono if needed
    thread_local std::vector<float> mono;
    mono.resize(frameCount);
    if (channels <= 1) {
        std::memcpy(mono.data(), input, frameCount * sizeof(float));
    } else {
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float sum = 0.0f;
            for (ma_uint32 c = 0; c < channels; ++c) {
                sum += input[i * channels + c];
            }
            mono[static_cast<size_t>(i)] = sum / static_cast<float>(channels);
        }
    }

    float userGain = m_userGain.load(std::memory_order_relaxed);
    float agc = m_agcGain.load(std::memory_order_relaxed);
    const bool agcOn = m_agcEnabled.load(std::memory_order_relaxed);
    const bool limOn = m_limiterEnabled.load(std::memory_order_relaxed);

    double sumSq = 0.0;
    float peak = 0.0f;
    for (ma_uint32 i = 0; i < frameCount; ++i) {
        float s = mono[static_cast<size_t>(i)] * userGain * agc;
        if (limOn) {
            // Soft clip
            if (s > 0.95f) s = 0.95f + 0.05f * std::tanh((s - 0.95f) * 10.0f);
            else if (s < -0.95f) s = -0.95f + 0.05f * std::tanh((s + 0.95f) * 10.0f);
        }
        mono[static_cast<size_t>(i)] = s;
        sumSq += static_cast<double>(s) * static_cast<double>(s);
        peak = std::max(peak, std::abs(s));
    }

    const float rms = frameCount > 0
        ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(frameCount)))
        : 0.0f;
    m_levelRms.store(rms, std::memory_order_relaxed);
    m_levelPeak.store(peak, std::memory_order_relaxed);
    float meter = m_levelMeter.load(std::memory_order_relaxed);
    const float instant = std::min(1.0f, std::max(rms * 2.5f, peak));
    meter = instant > meter ? instant : (meter * 0.92f + instant * 0.08f);
    m_levelMeter.store(meter, std::memory_order_relaxed);

    if (agcOn) {
        // Slow AGC toward ~-18 dBFS RMS (~0.125 linear)
        const float target = 0.12f;
        if (rms > 1.0e-4f) {
            float desired = target / rms;
            desired = std::max(0.25f, std::min(8.0f, desired));
            agc = agc * 0.98f + desired * 0.02f;
            m_agcGain.store(agc, std::memory_order_relaxed);
        }
    }

    pushRing(mono.data(), frameCount);
    m_framesCaptured.fetch_add(frameCount, std::memory_order_relaxed);
}

void AudioCapture::pushRing(const float* samples, size_t count)
{
    if (!samples || count == 0) return;
    size_t w = m_writePos.load(std::memory_order_relaxed);
    size_t r = m_readPos.load(std::memory_order_acquire);
    size_t used = (w - r) & kRingMask;
    size_t free = kRingCapacity - 1 - used;
    if (count > free) {
        // Drop oldest to make room
        const size_t drop = count - free;
        r = (r + drop) & kRingMask;
        m_readPos.store(r, std::memory_order_release);
        m_ringOverruns.fetch_add(1, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < count; ++i) {
        m_ring[(w + i) & kRingMask] = samples[i];
    }
    m_writePos.store((w + count) & kRingMask, std::memory_order_release);
}

size_t AudioCapture::popRing(float* out, size_t maxFrames)
{
    if (!out || maxFrames == 0) return 0;
    size_t w = m_writePos.load(std::memory_order_acquire);
    size_t r = m_readPos.load(std::memory_order_relaxed);
    size_t avail = (w - r) & kRingMask;
    size_t n = std::min(maxFrames, avail);
    if (n == 0) {
        m_pullStarved.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = m_ring[(r + i) & kRingMask];
    }
    m_readPos.store((r + n) & kRingMask, std::memory_order_release);
    return n;
}

size_t AudioCapture::pull(float* out, size_t maxFrames)
{
    return popRing(out, maxFrames);
}

size_t AudioCapture::pullAtRate(float* out, size_t maxFrames, double targetRateHz)
{
    if (!out || maxFrames == 0) return 0;
    const double srcRate = sampleRateHz();
    if (!std::isfinite(targetRateHz) || targetRateHz < 1000.0) targetRateHz = 8000.0;
    if (srcRate <= 0.0) return 0;

    const double step = srcRate / targetRateHz;
    size_t produced = 0;

    // Pull a working buffer from the ring as needed
    thread_local std::vector<float> src;
    src.resize(4096);

    while (produced < maxFrames) {
        // Need next sample at m_resampPhase
        while (m_resampPhase >= 1.0) {
            float s = 0.0f;
            if (popRing(&s, 1) == 0) {
                // No more input
                return produced;
            }
            m_resampHold = s;
            m_resampPhase -= 1.0;
        }
        out[produced++] = m_resampHold;
        m_resampPhase += step;
    }
    return produced;
}

bool AudioCapture::captureToWav(const std::string& path, double durationSec, int enumIndex)
{
    if (durationSec <= 0.0) durationSec = 1.0;
    const bool wasRunning = isCapturing();
    if (!wasRunning) {
        if (!start(enumIndex, 48000.0)) return false;
    }

    const double sr = sampleRateHz();
    const size_t need = static_cast<size_t>(std::llround(sr * durationSec));
    std::vector<float> pcm;
    pcm.reserve(need);
    std::vector<float> chunk(2048);

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(durationSec * 1000.0) + 500);
    while (pcm.size() < need && std::chrono::steady_clock::now() < deadline) {
        const size_t got = pull(chunk.data(), chunk.size());
        if (got == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }

    if (!wasRunning) stop();

    if (pcm.size() < need / 4) {
        spdlog::warn("AudioCapture: captureToWav got only {} frames", pcm.size());
        return false;
    }
    if (pcm.size() > need) pcm.resize(need);

    try {
        writeWavFloat32Mono(path, sr, pcm);
    } catch (const std::exception& ex) {
        spdlog::error("AudioCapture WAV write failed: {}", ex.what());
        return false;
    }
    spdlog::info("AudioCapture wrote {} frames to {}", pcm.size(), path);
    return true;
}
