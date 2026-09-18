#include "P25VoiceTiming.h"

#include "P25SdrtrunkTune.h"

#include <algorithm>
#include <cmath>

size_t p25Phase2VoiceRollingMaxSamples(double sampleRateHz, double windowSeconds) noexcept
{
    if (!(sampleRateHz > 0.0) || !std::isfinite(sampleRateHz) ||
        !(windowSeconds > 0.0) || !std::isfinite(windowSeconds)) {
        return 48000;
    }
    const double hardCapSeconds = std::max(windowSeconds, kP25Phase2VoiceDecodeRollingHardCapSeconds);
    return static_cast<size_t>(std::clamp(sampleRateHz * windowSeconds, 48000.0,
                                          sampleRateHz * hardCapSeconds));
}

double p25Phase2LowIfTrafficCenterHz(double voiceHz,
                                            double sampleRateHz,
                                            double companionHz) noexcept
{
    // DEC-0015 wrapper: name kept for verify_p25_one_rtl_traffic_source.py.
    // companionHz is a still-sourced control channel on this tuner, not a
    // "preferred" 250 kHz park.
    return p25SdrtrunkTunerCenterHz(voiceHz, sampleRateHz, companionHz);
}

size_t p25Phase2TrafficPreRollSamples(double sampleRateHz, bool physicalRetune) noexcept
{
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0) {
        return physicalRetune ? 655360u : 860160u;
    }
    const double seconds = physicalRetune
        ? kP25Phase2PhysicalRetunePreRollSeconds
        : kP25Phase2WidebandTrafficPreRollSeconds;
    return static_cast<size_t>(std::clamp(sampleRateHz * seconds, 0.0, 1048576.0));
}

qint64 p25PostArmSettleMs(bool phase2) noexcept
{
    return phase2 ? kP25Phase2PostArmSettleMs : kP25Phase1PostArmSettleMs;
}

int p25PostArmDiscardWindows(bool phase2) noexcept
{
    return phase2 ? kP25Phase2PostArmDiscardWindows : kP25Phase1PostArmDiscardWindows;
}

size_t p25Phase2EffectiveMinFreshSamples(size_t rollingSamples,
                                              size_t decodeOverlap,
                                              size_t minDecodeFreshNominal,
                                              size_t minDecodeFreshFloor,
                                              size_t maxDecodeChunk)
{
    if (rollingSamples == 0) return minDecodeFreshNominal;
    const size_t rollingBudget = rollingSamples > decodeOverlap
        ? rollingSamples - decodeOverlap
        : rollingSamples;
    size_t effective = std::min(minDecodeFreshNominal, rollingBudget);
    if (maxDecodeChunk > 0) effective = std::min(effective, maxDecodeChunk);
    const size_t floorCap = std::min(minDecodeFreshFloor, rollingBudget);
    if (maxDecodeChunk > 0) {
        // Floor must never force a full-window minFresh that cannot be met after
        // the decode cursor advanced (capture 20260801_100006: minFresh==rolling).
        const size_t floorLimit = std::min(floorCap, maxDecodeChunk);
        if (effective < floorLimit) effective = floorLimit;
    } else if (effective < floorCap) {
        effective = floorCap;
    }
    return std::max<size_t>(1, effective);
}

P25Phase2VoiceChunkPlan p25Phase2PlanVoiceDecodeChunk(
    bool streamingDdc,
    bool backlogCatchUp,
    bool activeSpeakerClearPath,
    bool wideReacquireWindow,
    bool maskEpochRepairWindow,
    bool speakerSustainDecode,
    bool phase2SustainDecodeWindow,
    bool firstColdEyeChunk,
    bool unacquiredAcquireWindow,
    bool decodeCursorAdvancedPastStart) noexcept
{
    P25Phase2VoiceChunkPlan plan;
    // After the rolling decode cursor has advanced, never demand a full 720 ms
    // of *fresh* IQ.  The first cold eye may still take a large context window
    // once; every subsequent hop must be a small sustain/acquire advance.
    const bool coldEye = firstColdEyeChunk && !decodeCursorAdvancedPastStart;

    if (streamingDdc) {
        // Contiguous fresh-only (overlap=0): stateful DDC + sticky CQPSK/framer.
        // Never re-feed prior samples — that would desync FIR/NCO state.
        if (coldEye) {
            plan.maxChunkSeconds = kP25Phase2VoiceDecodeFirstColdEyeSeconds;
            plan.overlapSeconds = 0.0;
            plan.minFreshSeconds = kP25Phase2VoiceDecodeFirstColdEyeSeconds;
            plan.treatAsContextFreeFresh = true;
        } else if (speakerSustainDecode ||
                   (activeSpeakerClearPath && !wideReacquireWindow &&
                    !maskEpochRepairWindow && !unacquiredAcquireWindow)) {
            // DEC-0014: 80 ms fresh-only (SDRTrunk 2048@25 kHz). Not 40 ms
            // (20260830 lost the eye). DEC-0022 160 ms sustain rejected
            // (105622 env=1 duty 0.125).
            plan.maxChunkSeconds = kP25Phase2StreamingLiveSliceSeconds;
            plan.overlapSeconds = 0.0;
            plan.minFreshSeconds = kP25Phase2StreamingLiveMinFreshSeconds;
        } else if (backlogCatchUp || wideReacquireWindow || maskEpochRepairWindow ||
                   unacquiredAcquireWindow) {
            if (activeSpeakerClearPath || backlogCatchUp) {
                plan.maxChunkSeconds = kP25Phase2StreamingLiveSliceSeconds * 2.0;
                plan.minFreshSeconds = kP25Phase2StreamingLiveMinFreshSeconds;
            } else {
                plan.maxChunkSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds;
                plan.minFreshSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireMinFreshSeconds;
            }
            plan.overlapSeconds = 0.0;
        } else {
            plan.maxChunkSeconds = kP25Phase2VoiceDecodeSustainChunkSeconds;
            plan.overlapSeconds = 0.0;
            plan.minFreshSeconds = kP25Phase2VoiceDecodeSustainMinFreshSeconds;
        }
        plan.minFreshFloorSamples = 4096.0;
        return plan;
    }

    // Speaker-sustain / active-clear path (DEC-0009 geometry).
    // DEC-0032 forbade backlogCatchUp from *replacing* 80+280 after 081701.
    // DEC-0058 used 160+280; DEC-0060 thinned overlap to 80 ms → 153932 jitter
    // (40 ms WAV islands / bridge top-ups). DEC-0061: 240+280 (see header).
    if (speakerSustainDecode ||
        (activeSpeakerClearPath && !wideReacquireWindow &&
         !maskEpochRepairWindow && !unacquiredAcquireWindow && !coldEye)) {
        if (backlogCatchUp) {
            plan.maxChunkSeconds = kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds;
            plan.overlapSeconds = kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds;
            plan.minFreshSeconds = kP25Phase2VoiceDecodeSpeakerBacklogCatchUpMinFreshSeconds;
            plan.minFreshFloorSamples = 8192.0;
            return plan;
        }
        plan.maxChunkSeconds = kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds;
        plan.overlapSeconds = kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds;
        plan.minFreshSeconds = kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds;
        plan.minFreshFloorSamples = 8192.0;
        return plan;
    }

    if (backlogCatchUp) {
        // Catch-up advances more fresh RF per hop, but must not drop below
        // DEC-0009 sustain overlap (DEC-0024 / 101644 start-middle collapse).
        plan.maxChunkSeconds = kP25Phase2VoiceDecodeBacklogCatchUpChunkSeconds;
        plan.overlapSeconds = kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds;
        plan.minFreshSeconds = kP25Phase2VoiceDecodeBacklogCatchUpMinFreshSeconds;
        plan.minFreshFloorSamples = 16384.0;
        return plan;
    }

    if (wideReacquireWindow || maskEpochRepairWindow) {
        // Large context max, small minFresh so the cursor can stream after submit.
        if (decodeCursorAdvancedPastStart) {
            plan.maxChunkSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds;
            plan.overlapSeconds = kP25Phase2VoiceDecodeAcquireOverlapSeconds;
            plan.minFreshSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireMinFreshSeconds;
        } else {
            plan.maxChunkSeconds = kP25Phase2VoiceDecodeFirstColdEyeSeconds;
            plan.overlapSeconds = kP25Phase2VoiceDecodeAcquireOverlapSeconds;
            plan.minFreshSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireMinFreshSeconds;
        }
        plan.minFreshFloorSamples = 16384.0;
        return plan;
    }

    if (phase2SustainDecodeWindow) {
        plan.maxChunkSeconds = kP25Phase2VoiceDecodeSustainChunkSeconds;
        plan.overlapSeconds = kP25Phase2VoiceDecodeSustainOverlapSeconds;
        plan.minFreshSeconds = kP25Phase2VoiceDecodeSustainMinFreshSeconds;
        plan.minFreshFloorSamples = 8192.0;
        return plan;
    }

    if (coldEye) {
        plan.maxChunkSeconds = kP25Phase2VoiceDecodeFirstColdEyeSeconds;
        plan.overlapSeconds = kP25Phase2VoiceDecodeAcquireOverlapSeconds;
        plan.minFreshSeconds = kP25Phase2VoiceDecodeFirstColdEyeSeconds;
        plan.minFreshFloorSamples = 16384.0;
        plan.treatAsContextFreeFresh = true;
        return plan;
    }

    // firstColdEye requested but cursor already advanced: stream short hops.
    if (firstColdEyeChunk && decodeCursorAdvancedPastStart) {
        plan.maxChunkSeconds = kP25Phase2VoiceDecodeSustainChunkSeconds;
        plan.overlapSeconds = kP25Phase2VoiceDecodeSustainOverlapSeconds;
        plan.minFreshSeconds = kP25Phase2VoiceDecodeSustainMinFreshSeconds;
        plan.minFreshFloorSamples = 8192.0;
        return plan;
    }

    if (unacquiredAcquireWindow) {
        plan.maxChunkSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds;
        plan.overlapSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds;
        plan.minFreshSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireMinFreshSeconds;
        plan.minFreshFloorSamples = 16384.0;
        return plan;
    }

    plan.maxChunkSeconds = kP25Phase2VoiceDecodeAcquireChunkSeconds;
    plan.overlapSeconds = kP25Phase2VoiceDecodeAcquireOverlapSeconds;
    plan.minFreshSeconds = kP25Phase2VoiceDecodeMinFreshSeconds;
    plan.minFreshFloorSamples = 16384.0;
    return plan;
}
