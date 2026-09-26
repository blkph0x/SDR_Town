#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

// DEC-0142: bounded, numeric-only process aggregates, one bucket per FM mode.
// Relaxed atomic snapshots are approximate while receivers are active; they
// are diagnostics, never a clock, security gate or decoder control input.
namespace fmDiagnostics {
enum Field : size_t { Blocks, InputSamples, DiscSamples, AudioSamples, Resets,
    EmptyAudio, LookaheadReads, PhaseRepairs, ProcessingUs, InputUs,
    OverBudgetBlocks, ChannelizerUs, DiscriminatorUs, ResamplerUs, PostAudioUs,
    RequestedAudioSamples, MaxBlockUs, MaxFirDelayUs, Count };
using Snapshot = std::array<uint64_t, Count>;
inline std::array<std::array<std::atomic<uint64_t>, Count>, 2> counters{};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
inline Snapshot snapshot(bool wfm) {
    Snapshot result{};
    for (size_t i=0; i<Count; ++i) result[i]=counters[wfm][i].load(std::memory_order_relaxed);
    return result;
}
inline void maximum(std::atomic<uint64_t>& value, uint64_t candidate) {
    auto old=value.load(std::memory_order_relaxed);
    while (old<candidate && !value.compare_exchange_weak(old,candidate,std::memory_order_relaxed)) {}
}
struct Block {
    bool wfm;
    Snapshot values{};
    std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now();
    Block(bool wide, size_t samples, double rate) : wfm(wide) {
        values[Blocks]=1;values[InputSamples]=samples;
        values[InputUs]=static_cast<uint64_t>(samples*1e6/rate);
    }
    ~Block() {
        values[ProcessingUs]=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-begin).count());
        values[OverBudgetBlocks]=values[ProcessingUs]>values[InputUs];
        values[EmptyAudio]=values[AudioSamples]==0;
        for(size_t i=0;i<MaxBlockUs;++i)counters[wfm][i].fetch_add(values[i],std::memory_order_relaxed);
        maximum(counters[wfm][MaxBlockUs],values[ProcessingUs]);
        maximum(counters[wfm][MaxFirDelayUs],values[MaxFirDelayUs]);
    }
};
}
