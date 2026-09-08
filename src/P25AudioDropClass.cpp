#include "P25AudioDropClass.h"

#include <algorithm>

P25AudioDropBucket classifyP25AudioDrop(const P25AudioDropSample& sample) noexcept
{
    if (sample.followReturned) {
        return P25AudioDropBucket::Follow;
    }

    const double windowSeconds = (sample.windowSeconds > 0.0) ? sample.windowSeconds : 1.0;
    const long long uniqueVcw = std::max(0LL, sample.targetVcw - sample.dups);
    const double uniquePerSecond = static_cast<double>(uniqueVcw) / windowSeconds;
    const double duty =
        (static_cast<double>(std::max(0LL, sample.emittedPcm)) * kP25AudioDropAmbeFrameSeconds)
        / windowSeconds;
    const double minUnique = kP25AudioDropMinUniqueVcwPerSecond;

    // A — unique selected-slot Voice2/4 not advancing (overlap/dedupe or no lock).
    if (uniquePerSecond < minUnique && duty < kP25AudioDropContinuousDuty) {
        return P25AudioDropBucket::Extract;
    }

    // B — unique VCWs present, mbelib not fed.
    if (sample.fed <= 0) {
        return P25AudioDropBucket::Feed;
    }

    // C — fed, speaker PCM not emitted.
    if (sample.emittedPcm <= 0) {
        return P25AudioDropBucket::Emit;
    }

    // D — emitting islands; ring/bridge/cadence not continuous.
    if (duty < kP25AudioDropContinuousDuty) {
        return P25AudioDropBucket::Playout;
    }

    return P25AudioDropBucket::Ok;
}

const char* p25AudioDropBucketLabel(P25AudioDropBucket bucket) noexcept
{
    switch (bucket) {
    case P25AudioDropBucket::Extract:
        return "A";
    case P25AudioDropBucket::Feed:
        return "B";
    case P25AudioDropBucket::Emit:
        return "C";
    case P25AudioDropBucket::Playout:
        return "D";
    case P25AudioDropBucket::Follow:
        return "E";
    case P25AudioDropBucket::Ok:
    default:
        return "ok";
    }
}
