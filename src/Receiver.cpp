#include "Receiver.h"

uint64_t p25MakeCurrentCallSessionId(uint32_t talkgroupId, uint64_t pttGeneration) noexcept
{
    return (static_cast<uint64_t>(talkgroupId) << 32) |
        (pttGeneration & 0xffffffffull);
}

void p25Phase2BeginNewPtt(Receiver& rx, int64_t grantEpochMs) noexcept
{
    ++rx.p25PttGeneration;
    if (rx.p25PttGeneration == 0) {
        rx.p25PttGeneration = 1;
    }
    rx.p25VoiceGrantEpochMs = grantEpochMs;
    rx.p25CurrentCallSessionId =
        p25MakeCurrentCallSessionId(rx.p25VoiceTalkgroupId, rx.p25PttGeneration);

    // A new PTT is a new Phase 2 audio/security session.  Do not let a Clear
    // latch, queued AMBE, vocoder predictor history, frame ordering state, or
    // playout tail from the previous call authorize or color the next one.
    rx.p25SessionState.pendingAudio = {};
    rx.p25SessionState.ambeDedupe = {};
    rx.p25SessionState.frameSequencer = {};
    rx.p25SessionState.audioTail = {};
    rx.p25SessionState.sustain = {};
    rx.p25SessionState.callSecurityLatch = P25CallSecurityLatch::Unknown;
    rx.p25SessionState.lastPttStartPendingClearCallSessionId = 0;
    rx.p25Phase2CallHadSpeakerAudio = false;
}

void p25Phase2RefreshGrantEpoch(Receiver& rx, int64_t grantEpochMs) noexcept
{
    // This value keys the current PTT/session audio state, so keep it stable
    // across repeated grants. Moving activity timestamps live in
    // p25TrafficLastGrantMs.
    if (rx.p25CurrentCallSessionId == 0 || rx.p25VoiceGrantEpochMs <= 0) {
        rx.p25VoiceGrantEpochMs = grantEpochMs;
    }
}
