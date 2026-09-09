#include "P25AppGlobals.h"

#include <limits>

std::atomic<long long> gLastDspMicros{0};
std::atomic<long long> gP25AudioLastSpeakerOutputMs{0};
std::atomic<double> gLastRmsDb{-100.0};   // live RF signal level used by squelch calibration/Auto/indicator
std::atomic<double> gLastNoiseFloorDb{-120.0};
std::atomic<double> gLastSnrDb{0.0};
std::atomic<double> gLastAfcOffsetHz{0.0};
std::atomic<double> gLastAfcPpmDelta{std::numeric_limits<double>::quiet_NaN()};
std::atomic<double> gLastAfcConfidence{0.0};
std::atomic<double> gLastAfcBinHz{0.0};
std::atomic<double> gP25LastTrustedControlFreqHz{0.0};
std::atomic<double> gP25LastTrustedControlOffsetHz{0.0};
std::atomic<long long> gP25LastTrustedControlOffsetMs{0};

P25VoiceDiagMirror gP25VoiceDiagMirror;
P25Phase2CadenceRollup gP25Phase2Cadence;
P25DebugStage gP25DebugStageFilter = P25DebugStage::All;

void publishP25VoiceDiagMirror(const P25VoiceDiagSnapshot& diag, uint8_t tdmaSlot, bool slotKnown)
{
    gP25VoiceDiagMirror.updatedMs.store(diag.updatedMs, std::memory_order_release);
    gP25VoiceDiagMirror.talkgroupId.store(diag.talkgroupId, std::memory_order_release);
    gP25VoiceDiagMirror.diag.store(diag.diag, std::memory_order_release);
    gP25VoiceDiagMirror.phase2Bursts.store(diag.phase2Bursts, std::memory_order_release);
    gP25VoiceDiagMirror.phase2VoiceCodewords.store(diag.phase2VoiceCodewords, std::memory_order_release);
    gP25VoiceDiagMirror.phase2SuperframeBursts.store(diag.phase2SuperframeBursts, std::memory_order_release);
    gP25VoiceDiagMirror.phase2MaskedBursts.store(diag.phase2MaskedBursts, std::memory_order_release);
    gP25VoiceDiagMirror.phase2MacCrcValid.store(diag.phase2MacCrcValid, std::memory_order_release);
    gP25VoiceDiagMirror.phase2MacPdus.store(diag.phase2MacPdus, std::memory_order_release);
    gP25VoiceDiagMirror.decodedFrames.store(diag.decodedFrames, std::memory_order_release);
    gP25VoiceDiagMirror.phase2ExpectedVoiceCodewords.store(diag.phase2ExpectedVoiceCodewords, std::memory_order_release);
    gP25VoiceDiagMirror.phase2FedToMbelib.store(diag.phase2FedToMbelib, std::memory_order_release);
    gP25VoiceDiagMirror.phase2EmittedPcmFrames.store(diag.phase2EmittedPcmFrames, std::memory_order_release);
    gP25VoiceDiagMirror.phase2DuplicateSuppressed.store(diag.phase2DuplicateSuppressedVoiceCodewords, std::memory_order_release);
    gP25VoiceDiagMirror.phase2FeedGaps.store(diag.phase2FeedGaps, std::memory_order_release);
    gP25VoiceDiagMirror.tdmaSlot.store(slotKnown ? static_cast<int>(tdmaSlot & 0x01u) : -1,
        std::memory_order_release);
}

bool loadP25VoiceDiagMirror(P25VoiceDiagSnapshot& out, uint8_t& outSlot, bool& outSlotKnown)
{
    const long long updatedMs = gP25VoiceDiagMirror.updatedMs.load(std::memory_order_acquire);
    if (updatedMs <= 0) return false;
    out.updatedMs = updatedMs;
    out.talkgroupId = gP25VoiceDiagMirror.talkgroupId.load(std::memory_order_acquire);
    out.diag = gP25VoiceDiagMirror.diag.load(std::memory_order_acquire);
    out.phase2Bursts = gP25VoiceDiagMirror.phase2Bursts.load(std::memory_order_acquire);
    out.phase2VoiceCodewords = gP25VoiceDiagMirror.phase2VoiceCodewords.load(std::memory_order_acquire);
    out.phase2SuperframeBursts = gP25VoiceDiagMirror.phase2SuperframeBursts.load(std::memory_order_acquire);
    out.phase2MaskedBursts = gP25VoiceDiagMirror.phase2MaskedBursts.load(std::memory_order_acquire);
    out.phase2MacCrcValid = gP25VoiceDiagMirror.phase2MacCrcValid.load(std::memory_order_acquire);
    out.phase2MacPdus = gP25VoiceDiagMirror.phase2MacPdus.load(std::memory_order_acquire);
    out.decodedFrames = gP25VoiceDiagMirror.decodedFrames.load(std::memory_order_acquire);
    const int slot = gP25VoiceDiagMirror.tdmaSlot.load(std::memory_order_acquire);
    outSlotKnown = slot >= 0;
    outSlot = outSlotKnown ? static_cast<uint8_t>(slot & 0x01u) : 0u;
    return true;
}
