#pragma once
#include "Demod.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

enum class SstvInputGap : uint32_t {
    Start=1, Stop=2, Source=4, Position=8, Invalid=16, Overflow=32, Contention=64
};
enum class SstvPushResult { Accepted, Inactive, Invalid, Contended };

struct SstvInputEvent {
    static constexpr size_t maxSamples=8192;
    std::array<float,maxSamples> samples{};
    size_t count=0;
    uint64_t generation=0,sourceId=0,epoch=0,firstSample=0;
    double sampleRate=0,targetHz=0;
    uint32_t gapReasons=0; // Gap events have count=0; always precede new audio.
};
struct SstvInputStats {
    uint64_t acceptedSamples=0,consumedSamples=0,discardedSamples=0,gaps=0,generation=0;
    size_t queuedSamples=0,queuedBlocks=0;
    uint64_t invalidBlocks=0,contendedBlocks=0;
    bool active=false;
};

// DEC-0095: one producer; fixed storage allocated at construction, no producer
// allocations or blocking locks. pop/start/stop/stats belong off the RF thread.
// The owner must detach and quiesce the producer before start/stop/destruction.
// This prevents an old in-flight block from entering a restarted session.
class SstvLiveInput final {
public:
    SstvLiveInput();
    ~SstvLiveInput();
    void start();
    void stop();
    SstvPushResult tryPush(const FmMultiplexBlock& block,uint64_t sourceId,DemodMode mode);
    std::optional<SstvInputEvent> pop();
    SstvInputStats stats() const;
private:
    static constexpr size_t slots=16;
    struct Storage {std::array<SstvInputEvent,slots> blocks;};
    void resetLocked(uint32_t reasons);
    void pendingLocked();
    std::unique_ptr<Storage> storage_;
    mutable std::mutex mutex_;
    std::atomic<bool> active_{false};
    std::atomic<uint32_t> pending_{0};
    std::atomic<uint64_t> invalid_{0},contended_{0},rejectedSamples_{0};
    size_t head_=0,size_=0,queued_=0;
    uint32_t gap_=0;
    uint64_t generation_=0,accepted_=0,consumed_=0,discarded_=0,gaps_=0;
    bool identity_=false;
    uint64_t source_=0,epoch_=0,next_=0;
    double rate_=0,target_=0;
};
