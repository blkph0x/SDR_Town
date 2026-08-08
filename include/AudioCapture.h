#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "miniaudio.h"

// Sprint 2: microphone capture for future P25 TX (no encode/RF here).
// 48 kHz mono float capture → lock-free ring → pull for TX encoder path.

struct AudioCaptureDeviceInfo {
    ma_device_id id{};
    std::string name;
    bool isDefault = false;
};

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    std::vector<AudioCaptureDeviceInfo> enumerateCaptureDevices();

    // Start capture on device index from last enumerate (or default if enum empty / index OOB).
    bool start(int enumIndex = -1, double sampleRateHz = 48000.0);
    void stop();
    bool isCapturing() const noexcept;

    double sampleRateHz() const noexcept { return m_sampleRate.load(std::memory_order_relaxed); }
    int activeDeviceIndex() const noexcept { return m_activeEnumIndex.load(std::memory_order_relaxed); }
    std::string activeDeviceName() const;

    // Linear gain applied after AGC (0..4 typical). Default 1.0.
    void setGain(float gain);
    float gain() const noexcept { return m_userGain.load(std::memory_order_relaxed); }

    void setAgcEnabled(bool on) noexcept { m_agcEnabled.store(on, std::memory_order_relaxed); }
    bool agcEnabled() const noexcept { return m_agcEnabled.load(std::memory_order_relaxed); }

    // Soft peak limiter on/off (default on).
    void setLimiterEnabled(bool on) noexcept { m_limiterEnabled.store(on, std::memory_order_relaxed); }

    // Pull mono float frames at capture rate. Returns frames written.
    size_t pull(float* out, size_t maxFrames);

    // Pull mono float frames decimated/resampled to 8 kHz (for AMBE later).
    // Simple box-decimate + linear interp; good enough for level tests / future encoder feed.
    size_t pullAtRate(float* out, size_t maxFrames, double targetRateHz);

    // Instantaneous meters (post-gain/AGC), updated from capture callback.
    float levelRms() const noexcept { return m_levelRms.load(std::memory_order_relaxed); }
    float levelPeak() const noexcept { return m_levelPeak.load(std::memory_order_relaxed); }
    // 0..1 UI-friendly meter with slow decay.
    float levelMeter() const noexcept { return m_levelMeter.load(std::memory_order_relaxed); }

    size_t underrunCount() const noexcept { return m_pullStarved.load(std::memory_order_relaxed); }
    size_t overrunCount() const noexcept { return m_ringOverruns.load(std::memory_order_relaxed); }
    uint64_t framesCaptured() const noexcept { return m_framesCaptured.load(std::memory_order_relaxed); }

    // Capture for durationSec into mono f32 WAV (PCM float). Blocking helper for tests/CLI.
    bool captureToWav(const std::string& path, double durationSec, int enumIndex = -1);

    // Power-of-2 ring (~2.7 s at 48 kHz).
    static constexpr size_t kRingCapacity = 1u << 17;

private:
    friend void audioCaptureDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    void processCapturedFrames(const float* input, ma_uint32 frameCount, ma_uint32 channels);
    void pushRing(const float* samples, size_t count);
    size_t popRing(float* out, size_t maxFrames);

    ma_context m_context{};
    bool m_contextValid = false;
    ma_device m_device{};
    bool m_deviceActive = false;

    std::vector<AudioCaptureDeviceInfo> m_devices;
    std::mutex m_enumMutex;

    std::vector<float> m_ring;
    std::atomic<size_t> m_writePos{0};
    std::atomic<size_t> m_readPos{0};

    std::atomic<double> m_sampleRate{48000.0};
    std::atomic<int> m_activeEnumIndex{-1};
    std::string m_activeName;
    mutable std::mutex m_nameMutex;

    std::atomic<float> m_userGain{1.0f};
    std::atomic<bool> m_agcEnabled{true};
    std::atomic<bool> m_limiterEnabled{true};
    std::atomic<float> m_agcGain{1.0f};

    std::atomic<float> m_levelRms{0.0f};
    std::atomic<float> m_levelPeak{0.0f};
    std::atomic<float> m_levelMeter{0.0f};

    std::atomic<size_t> m_ringOverruns{0};
    std::atomic<size_t> m_pullStarved{0};
    std::atomic<uint64_t> m_framesCaptured{0};

    // Resampler state for pullAtRate
    double m_resampPhase = 0.0;
    float m_resampHold = 0.0f;
};
