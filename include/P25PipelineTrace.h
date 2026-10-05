#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <memory>
#include <vector>

// DEC-0194: observational only. No payload, allocation, wait or file I/O on producers.
struct P25PipelineEvent {
    char stage[32]{};
    char reason[96]{};
    uint64_t traceSequence = 0, monotonicUs = 0;
    uint64_t job = 0, session = 0, generation = 0, flush = 0;
    uint64_t iqStart = 0, iqEnd = 0, submittedUs = 0, startedUs = 0, completedUs = 0;
    uint64_t pcmSamples = 0, pushedSamples = 0, pendingSamples = 0;
    uint64_t selectedVcw = 0, companionVcw = 0, acceptedFrames = 0;
    uint64_t fedFrames = 0, rejectedVcw = 0, duplicateVcw = 0, contextVcw = 0;
    uint32_t tg = 0, rid = 0;
    int slot = -1, device = -1;
    int decisionLine = 0, controlOpcode = -1, encryptionState = -1;
    double centerHz = 0, targetHz = 0, sampleRate = 0, ringPercent = 0;

    static uint64_t nowUs() noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    template<size_t N> static void text(char (&dest)[N], const char* source) noexcept {
        size_t i = 0;
        if (source) for (; i + 1 < N && source[i]; ++i) dest[i] = source[i];
        dest[i] = '\0';
    }
};

template<size_t Capacity = 4096> class P25PipelineTrace {
    static_assert(Capacity > 0);
public:
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = count_ = 0; sequence_ = 0;
        dropped_.store(0); enabled_.store(true, std::memory_order_release);
    }
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_.store(false, std::memory_order_release);
    }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    bool push(P25PipelineEvent event) {
        if (!enabled()) return false;
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) { dropped_.fetch_add(1); return false; }
        if (!enabled()) return false;
        if (count_ == Capacity) { dropped_.fetch_add(1); return false; }
        event.traceSequence = ++sequence_;
        event.monotonicUs = P25PipelineEvent::nowUs();
        (*events_)[(head_ + count_) % Capacity] = event;
        ++count_;
        return true;
    }
    std::vector<P25PipelineEvent> drain() {
        // Allocation is writer-only and outside admission's critical section.
        std::vector<P25PipelineEvent> result(Capacity);
        size_t size;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size = count_;
            for (size_t i = 0; i < size; ++i) result[i] = (*events_)[(head_ + i) % Capacity];
            head_ = (head_ + size) % Capacity; count_ = 0;
        }
        result.resize(size);
        return result;
    }
    uint64_t dropped() const noexcept { return dropped_.load(); }
private:
    std::mutex mutex_;
    std::unique_ptr<std::array<P25PipelineEvent, Capacity>> events_ =
        std::make_unique<std::array<P25PipelineEvent, Capacity>>();
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> dropped_{0};
    size_t head_ = 0, count_ = 0;
    uint64_t sequence_ = 0;
};

struct P25ControlContext {
    size_t device = 0;
    uint64_t streamEpoch = 0, resetGeneration = 0;
    double centerHz = 0, sampleRate = 0, targetHz = 0;
    bool matches(const P25ControlContext& current) const noexcept {
        return device == current.device && streamEpoch == current.streamEpoch &&
            resetGeneration == current.resetGeneration &&
            std::isfinite(centerHz) && std::isfinite(targetHz) &&
            std::isfinite(sampleRate) && sampleRate > 0 && centerHz > 0 && targetHz > 0 &&
            centerHz == current.centerHz && sampleRate == current.sampleRate &&
            targetHz == current.targetHz;
    }
};
