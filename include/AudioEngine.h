#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

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
    void clearBuffers();

    // Test tone on a specific active output (or all)
    void playTestTone(size_t activeIndex = size_t(-1), float freq = 1000.0f, float durationSec = 0.6f);
    void playTestToneForDevice(size_t enumIndex, float freq = 1000.0f, float durationSec = 0.6f);

    // State
    size_t activeOutputCount() const;
    bool isDeviceActive(size_t enumIndex) const;

    // For status UI
    std::string getActiveDeviceNames() const;

    // Live diagnostics (P1 audit): ring buffer fill and underrun count for CLI/GUI stats
    double getRingFillPercent() const;
    int    getUnderrunCount() const;

    // Exposed for data callback (test tone state)
    std::atomic<int>   m_testTarget{-1};
    std::atomic<float> m_testFreq{1000.0f};

    // Real audio buffer (accessible from static callback)
    mutable std::mutex audioMutex;
    std::vector<float> audioBuffer;

    float getMasterVolume() const { return m_masterVolume.load(std::memory_order_relaxed); }

    // Diagnostics counters (incremented from RT callback; read lock-free from stats)
    std::atomic<int> underrunCount{0};

private:
    void startDevice(size_t enumIndex);
    void stopDevice(size_t enumIndex);

    ma_context m_context;
    bool m_contextValid = false;
    std::vector<AudioDeviceInfo> m_devices;

public:
    // Simple power-of-2 lock-free ring buffer for realtime audio (no mutex, no erase/alloc in callback).
    // Capacity must be power of 2.
    struct RingBuffer {
        std::vector<float> data;
        std::atomic<size_t> writePos{0};
        std::atomic<size_t> readPos{0};
        size_t capacity = 0; // power of 2

        RingBuffer() = default;
        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        RingBuffer(RingBuffer&& other) noexcept
            : data(std::move(other.data)),
              writePos(other.writePos.load(std::memory_order_relaxed)),
              readPos(other.readPos.load(std::memory_order_relaxed)),
              capacity(other.capacity) {}

        RingBuffer& operator=(RingBuffer&& other) noexcept {
            if (this != &other) {
                data = std::move(other.data);
                writePos.store(other.writePos.load(std::memory_order_relaxed));
                readPos.store(other.readPos.load(std::memory_order_relaxed));
                capacity = other.capacity;
            }
            return *this;
        }

        void init(size_t cap) {
            capacity = 1;
            while (capacity < cap) capacity <<= 1;
            data.assign(capacity, 0.0f);
            writePos.store(0);
            readPos.store(0);
        }
    };

    struct ActiveOutput {
        size_t enumIndex = 0;
        std::unique_ptr<ma_device> device;
        std::atomic<float> volume{1.0f};
        RingBuffer ring;                    // replaces liveBuffer for RT-safe audio delivery
        std::vector<float> pending;         // for test tones / small smoothing

        // per-output test tone state
        std::atomic<bool> isTestTone{false};
        std::atomic<float> testFreq{1000.0f};
        std::atomic<float> testPhase{0.0f};
        std::atomic<int>  testToneFramesLeft{0};

        std::atomic<bool> valid{false};  // set after full init, cleared before destroy to protect callback (P0 lifetime)

        // Back-pointer so data_callback can get the engine without walking m_active (P1 callback ownership).
        AudioEngine* owningEngine = nullptr;

        ActiveOutput() = default;
        ActiveOutput(ActiveOutput&& other) noexcept
            : enumIndex(other.enumIndex),
              device(std::move(other.device)),
              volume(other.volume.load()),
              ring(std::move(other.ring)),
              pending(std::move(other.pending)),
              isTestTone(other.isTestTone.load()),
              testFreq(other.testFreq.load()),
              testPhase(other.testPhase.load()),
              testToneFramesLeft(other.testToneFramesLeft.load()) {}

        ActiveOutput& operator=(ActiveOutput&& other) noexcept {
            if (this != &other) {
                enumIndex = other.enumIndex;
                device = std::move(other.device);
                volume.store(other.volume.load());
                ring = std::move(other.ring);
                pending = std::move(other.pending);
                isTestTone.store(other.isTestTone.load());
                testFreq.store(other.testFreq.load());
                testPhase.store(other.testPhase.load());
                testToneFramesLeft.store(other.testToneFramesLeft.load());
            }
            return *this;
        }

        // Delete copy to satisfy atomic
        ActiveOutput(const ActiveOutput&) = delete;
        ActiveOutput& operator=(const ActiveOutput&) = delete;
    };
    std::vector<std::shared_ptr<ActiveOutput>> m_active;

    // Helper for the free data_callback (nested ActiveOutput type is now visible)
    std::shared_ptr<ActiveOutput> findActiveOutput(ma_device* pDev);

    std::atomic<float> m_masterVolume{0.85f};

    std::atomic<float> m_sampleRate{48000.0f};  // actual output rate; used for exact audio block sizing and bitrate reporting
    float getSampleRate() const { return m_sampleRate.load(std::memory_order_relaxed); }

    // For test tone generation (phase kept here)
    std::atomic<float> m_testPhase{0.0f};
};
