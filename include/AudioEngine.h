#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <atomic>

#include "miniaudio.h"

// Simple multi-device audio output engine (PR4)
// Supports duplicating the same PCM stream to N selected playback devices (e.g. speakers + VB-Audio Cable)
// with independent per-device volume. Designed for 48kHz mono float.

struct AudioDeviceInfo {
    ma_device_id id;          // miniaudio id
    std::string name;
    bool isDefault = false;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Enumeration (call once or on demand)
    std::vector<AudioDeviceInfo> enumeratePlaybackDevices();

    // Active outputs: vector of device indices from the last enumerate, or by name substring match
    void setActiveOutputs(const std::vector<size_t>& indicesFromLastEnum);
    void setActiveOutputsByName(const std::vector<std::string>& nameSubstrings); // convenient for "CABLE", "Speakers"

    // Volumes: master + per active output (0..1)
    void setMasterVolume(float vol);
    void setOutputVolume(size_t activeIndex, float vol);

    // Push a block of mono float samples (will be duplicated + gained to all active devices)
    // Call from your demod/resample thread at ~10-50ms blocks for low latency.
    void pushAudio(const float* samples, size_t count);

    // Test tone on a specific active output (or all)
    void playTestTone(size_t activeIndex = size_t(-1), float freq = 1000.0f, float durationSec = 0.6f);

    // State
    size_t activeOutputCount() const;
    bool isDeviceActive(size_t enumIndex) const;

    // For status UI
    std::string getActiveDeviceNames() const;

private:
    void startDevice(size_t enumIndex);
    void stopDevice(size_t enumIndex);

    ma_context m_context;
    std::vector<AudioDeviceInfo> m_devices;

    struct ActiveOutput {
        size_t enumIndex = 0;
        ma_device device{};
        std::atomic<float> volume{1.0f};
        std::vector<float> pending; // small ring for test tones etc.
    };
    std::vector<ActiveOutput> m_active;

    float m_masterVolume = 0.85f;

    // For test tone generation
    std::atomic<float> m_testPhase{0.0f};
    std::atomic<int> m_testTarget{-1};
    std::atomic<float> m_testFreq{1000.0f};
};