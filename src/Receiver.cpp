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
}

void p25Phase2RefreshGrantEpoch(Receiver& rx, int64_t grantEpochMs) noexcept
{
    rx.p25VoiceGrantEpochMs = grantEpochMs;
}
