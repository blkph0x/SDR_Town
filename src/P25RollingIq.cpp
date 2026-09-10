#include "P25RollingIq.h"

#include <algorithm>
#include <cmath>

// Session helpers: declared in P25VoiceDecode.h / still defined in main.cpp until moved.
bool p25Phase2SessionHadBurstEye(const Receiver& rx) noexcept;
bool p25Phase2SessionHadVoiceLock(const Receiver& rx) noexcept;

size_t p25Phase2UndecodedBacklogSamples(const RollingIqWindow& rolling) noexcept
{
    // Capture 20260807_232020: backlog always returned 0 when absKnown=no, so
    // speaker-live catch-up never ran. We only decoded ~60 ms RF every ~350 ms
    // (dutySec≈0.24–0.48) while the ring underran between 80 ms PCM islands.
    if (rolling.samples.empty()) return 0;
    const uint64_t cursor = rolling.effectiveDecodeAbsolute();
    if (rolling.absoluteKnown) {
        if (!rolling.decodeAbsoluteKnown && !rolling.submittedDecodeEndKnown) {
            return rolling.samples.size();
        }
        if (rolling.endAbsolute <= cursor) return 0;
        const uint64_t backlogAbs = rolling.endAbsolute - cursor;
        return static_cast<size_t>(std::min<uint64_t>(
            backlogAbs, static_cast<uint64_t>(rolling.samples.size())));
    }
    // Sample-index mode: cursor is an offset into samples[].
    if (!rolling.decodeAbsoluteKnown && !rolling.submittedDecodeEndKnown) {
        return rolling.samples.size();
    }
    if (cursor >= static_cast<uint64_t>(rolling.samples.size())) return 0;
    return rolling.samples.size() - static_cast<size_t>(cursor);
}

void p25Phase2PrepareRollingIqPull(DeviceManager& mgr,
                                          size_t devIndex,
                                          Receiver& rx,
                                          RollingIqWindow& rolling,
                                          size_t rollingWindow,
                                          size_t& pullWindow,
                                          double sampleRateHz)
{
    if (sampleRateHz <= 0.0) return;
    const size_t recoveryPull = static_cast<size_t>(
        std::clamp(sampleRateHz * 0.200, 4096.0, static_cast<double>(rollingWindow)));
    const size_t depletedThreshold = static_cast<size_t>(sampleRateHz * 0.080);
    if (rolling.samples.empty()) {
        const size_t recoveryPreRoll = std::min(rollingWindow, recoveryPull * 3);
        mgr.setReceiverCursorBeforeLiveEdge(devIndex, rx, recoveryPreRoll);
        pullWindow = std::max(pullWindow, recoveryPull);
    } else if (rolling.samples.size() < depletedThreshold) {
        pullWindow = std::max(pullWindow, recoveryPull);
    }
    if (rolling.absoluteKnown && rolling.endAbsolute > 0) {
        uint64_t syncAbsolute = rolling.endAbsolute;
        if (rolling.decodeAbsoluteKnown && rolling.endAbsolute > rolling.lastDecodeAbsolute) {
            const uint64_t backlog = rolling.endAbsolute - rolling.lastDecodeAbsolute;
            // Capture 20260807_234054: maxBacklog=200 ms + live-edge jump while
            // still "cold" (pre-emit) discarded most speech RF (span 88 s, only
            // ~6 s decoded). Allow ~1.2 s of lag while an eye exists; never jump
            // to live once TDMA framing or emit has started.
            const bool havePhase2Eye =
                p25Phase2SessionHadBurstEye(rx) ||
                p25Phase2SessionHadVoiceLock(rx) ||
                rx.p25SessionState.sustain.hadSuccessfulEmit ||
                rx.p25SessionState.sustain.peakPhase2Bursts >= 1;
            // DEC-0030 / 060036 + DEC-0023: with an eye/emit, allow backlog up
            // to the active rolling window (4.0 s) before any live-edge sync
            // preference. The old 1.2 s / 4194304-sample caps punched RF holes
            // while the worker drained the first cold emit (file duty 0.705).
            const double maxBacklogSeconds = havePhase2Eye
                ? kP25Phase2VoiceDecodeActiveRollingSeconds
                : 0.400;
            const uint64_t maxBacklog = static_cast<uint64_t>(
                std::clamp(sampleRateHz * maxBacklogSeconds, 4096.0,
                           sampleRateHz * kP25Phase2VoiceDecodeRollingHardCapSeconds));
            if (backlog > maxBacklog) {
                // Always keep the device cursor at the rolling live edge so the
                // RTL ring is not left unread. Rewinding to lastDecode (older
                // code) abandoned live samples and punched multi-second RF holes
                // (20260807_235726). Rolling trim drops already-decoded prefix;
                // undecoded backlog is drained by successive decode hops.
                // True-cold still prefers live edge (same syncAbsolute).
                syncAbsolute = rolling.endAbsolute;
                (void)havePhase2Eye;
            }
        }
        mgr.syncReceiverCursorToAbsolute(devIndex, rx, syncAbsolute);
    }
}
