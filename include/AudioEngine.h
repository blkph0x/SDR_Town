#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <new>
#include <cstdint>
#include <thread>

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

    // Enumeration (call once or on demand). Remaps active outputs' enum indices
    // by device name so a dialog re-enumerate cannot orphan the live device list.
    std::vector<AudioDeviceInfo> enumeratePlaybackDevices();

    // Active outputs: vector of device indices from the last enumerate, or by name substring match.
    // setActiveOutputs is incremental: devices that stay selected are not torn down.
    void setActiveOutputs(const std::vector<size_t>& indicesFromLastEnum);
    void setActiveOutputsByName(const std::vector<std::string>& nameSubstrings); // convenient for "CABLE", "Speakers"
    std::vector<std::string> getActiveDeviceNameList() const;

    // Volumes: master + per active output (0..1)
    void setMasterVolume(float vol);
    void setOutputVolume(size_t activeIndex, float vol);

    // Sprint 2: mute all playback (PTT anti-feedback). Independent of master volume.
    void setOutputMuted(bool muted);
    bool isOutputMuted() const noexcept { return m_outputMuted.load(std::memory_order_relaxed); }

    // Push a block of mono float samples (will be duplicated + gained to all active devices)
    // Call from your demod/resample thread at ~10-50ms blocks for low latency.
    void pushAudio(const float* samples, size_t count);
    void pushAudioToActiveOutputs(const float* samples, size_t count, const std::vector<size_t>& activeOutputIndices);
    void pushBridgeAudioToActiveOutputs(const float* samples, size_t count, const std::vector<size_t>& activeOutputIndices);
    size_t dropQueuedBridgeAudio(size_t maxSamples, const std::vector<size_t>& activeOutputIndices = {});
    void clearBuffers();
    // Drop oldest queued PCM so live analog monitoring stays near realtime.
    // Returns samples discarded. Never touches the realtime callback read path
    // beyond advancing readPos under audioMutex (same ownership as push).
    size_t trimQueuedAudio(size_t maxQueuedSamples,
                           const std::vector<size_t>& activeOutputIndices = {});

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
    size_t getRingQueuedSamples() const;
    size_t getJitterQueueCapFrames() const;
    int    getUnderrunCount() const;

    // Exposed for data callback (test tone state)
    std::atomic<int>   m_testTarget{-1};
    std::atomic<float> m_testFreq{1000.0f};

    // Guards active output rings and device lifecycle.
    mutable std::mutex audioMutex;

    float getMasterVolume() const { return m_masterVolume.load(std::memory_order_relaxed); }

    // Diagnostics counters (incremented from RT callback; read lock-free from stats)
    std::atomic<int> underrunCount{0};
    // Cumulative per-output callback observations, not inferred speech loss.
    std::atomic<uint64_t> consumedFrames{0};
    std::atomic<uint64_t> zeroFillFrames{0};
    std::atomic<uint64_t> emptyCallbacks{0};
    std::atomic<uint64_t> partialCallbacks{0};
    std::atomic<uint64_t> controlSilenceFrames{0};
    std::atomic<uint64_t> producerDroppedFrames{0};

    struct ActiveOutput;

private:
    static constexpr uint8_t kRingSampleReal = 0;
    static constexpr uint8_t kRingSampleBridge = 1;

    void startDevice(size_t enumIndex);
    void stopDevice(size_t enumIndex);
    void pushAudioToActiveOutputLocked(ActiveOutput& output, const float* samples, size_t count, uint8_t sampleKind);

    ma_context m_context;
    bool m_contextValid = false;
    std::vector<AudioDeviceInfo> m_devices;

public:
    // Power-of-2 SPSC ring with nonblocking callback and guarded control discards.
    // Capacity must be power of 2.
    struct RingBuffer {
        // DEC-0074: only exceptional cursor mutations exclude the consumer.
        // Normal producer writes retain the SPSC path. Callback acquisition
        // is a single nonblocking attempt; only control threads may wait.
        std::atomic_flag consumerBusy = ATOMIC_FLAG_INIT;
        struct ConsumerLease {
            RingBuffer& ring;
            bool acquired;
            ConsumerLease(RingBuffer& rb, bool wait) noexcept : ring(rb), acquired(false) {
                do {
                    acquired = !ring.consumerBusy.test_and_set(std::memory_order_acquire);
                    if (acquired || !wait) break;
                    std::this_thread::yield();
                } while (true);
            }
            ~ConsumerLease() {
                if (acquired) ring.consumerBusy.clear(std::memory_order_release);
            }
            ConsumerLease(const ConsumerLease&) = delete;
            ConsumerLease& operator=(const ConsumerLease&) = delete;
        };
        std::vector<float> data;
        std::vector<uint8_t> sampleKind;
        alignas(std::hardware_destructive_interference_size) std::atomic<size_t> writePos{0};
        alignas(std::hardware_destructive_interference_size) std::atomic<size_t> readPos{0};
        size_t capacity = 0; // power of 2

        RingBuffer() = default;
        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        RingBuffer(RingBuffer&& other) noexcept
            : data(std::move(other.data)),
              sampleKind(std::move(other.sampleKind)),
              writePos(other.writePos.load(std::memory_order_relaxed)),
              readPos(other.readPos.load(std::memory_order_relaxed)),
              capacity(other.capacity) {}

        RingBuffer& operator=(RingBuffer&& other) noexcept {
            if (this != &other) {
                data = std::move(other.data);
                sampleKind = std::move(other.sampleKind);
                writePos.store(other.writePos.load(std::memory_order_relaxed));
                readPos.store(other.readPos.load(std::memory_order_relaxed));
                capacity = other.capacity;
            }
            return *this;
        }

        void discardAll() noexcept {
            ConsumerLease control(*this, true);
            readPos.store(writePos.load(std::memory_order_acquire), std::memory_order_release);
        }

        void init(size_t cap) {
            if (cap == 0 || (cap & (cap - 1)) != 0) {
                throw std::invalid_argument("AudioEngine RingBuffer capacity must be a power of two");
            }
            capacity = 1;
            while (capacity < cap) capacity <<= 1;
            data.assign(capacity, 0.0f);
            sampleKind.assign(capacity, kRingSampleReal);
            writePos.store(0);
            readPos.store(0);
        }
    };

    struct ActiveOutput {
        size_t enumIndex = 0;
        std::string deviceName;             // stable across re-enumerate (enum index is not)
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
              deviceName(std::move(other.deviceName)),
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
                deviceName = std::move(other.deviceName);
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
    std::atomic<bool> m_outputMuted{false};

    std::atomic<float> m_sampleRate{48000.0f};  // actual output rate; used for exact audio block sizing and bitrate reporting
    float getSampleRate() const { return m_sampleRate.load(std::memory_order_relaxed); }

    // For test tone generation (phase kept here)
    std::atomic<float> m_testPhase{0.0f};
};
