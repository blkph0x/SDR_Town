#pragma once

#include "Demod.h"
#include "P25LiveDecoder.h"
#include "P25ReceiverSession.h"
#include "P25TrafficChannelProcessor.h"
#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>

// Basic per-receiver state holder.
// Goal: each SDR/monitor path gets its own demod, audio targets, recorder stub, etc.
// This eliminates global monitor* state and enables true multi-device independence.

struct P25VoiceDiagSnapshot {
    int diag = 0;
    long long updatedMs = 0;
    uint32_t talkgroupId = 0;
    long long syncs = 0;
    long long nids = 0;
    long long imbeFrames = 0;
    long long decodedFrames = 0;
    long long audioSamples = 0;
    long long phase2Bursts = 0;
    long long phase2VoiceCodewords = 0;
    long long phase2TargetVoiceCodewords = 0;
    long long phase2OppositeVoiceCodewords = 0;
    long long phase2ExpectedVoiceCodewords = 0;
    long long phase2FedToMbelib = 0;
    long long phase2EmittedPcmFrames = 0;
    long long phase2FeedGaps = 0;
    long long phase2AmbeDecodeAttempts = 0;
    long long phase2AmbeAcceptedFrames = 0;
    long long phase2DiagnosticAmbeProbeAttempts = 0;
    long long phase2DiagnosticAmbeProbeAccepted = 0;
    long long phase2DuplicateSuppressedVoiceCodewords = 0;
    long long phase2SuperframeBursts = 0;
    long long phase2MaskedBursts = 0;
    long long phase2MacPdus = 0;
    long long phase2MacCrcValid = 0;
    long long phase2MacFecDecoded = 0;
    long long phase2MacDirectCrcValid = 0;
    long long phase2MacDirectCrcRejected = 0;
    long long phase2MacRsDecoded = 0;
    long long phase2MacNominalCrcValid = 0;
    long long phase2MacAltKindCrcValid = 0;
    long long phase2MacBitSwapCrcValid = 0;
    long long phase2MacSlipCrcValid = 0;
    long long phase2MacInvertCrcValid = 0;
    bool phase2EssKnown = false;
    bool phase2EssEncrypted = false;
    bool phase2TargetEssKnown = false;
    bool phase2TargetEssEncrypted = false;
    bool phase2TargetMacCrcValid = false;
    bool phase2TargetSessionAudioRelease = false;
    bool backendAvailable = false;
    bool nidLock = false;
    double phase2CenterFreqHz = 0.0;
    double phase2EffectiveTargetFreqHz = 0.0;
};

inline P25LiveDecoderConfig p25RealtimeVoiceDecoderConfig()
{
    P25LiveDecoderConfig cfg;
    cfg.enableC4fmFixedPhaseSearch = false;
    cfg.maxFrameSyncs = 6;
    cfg.maxRawTsbkBlocksPerFrame = 4;
    return cfg;
}

struct Receiver {
    mutable std::mutex stateMutex;
    // Protects mutable DSP internals (demodulator filters/squelch, P25 live
    // decoder state, and IMBE/AMBE decoder history). Keep UI/control state under
    // stateMutex and avoid holding stateMutex while running heavy DSP blocks.
    mutable std::recursive_mutex dspMutex;

    size_t deviceIndex = 0;           // which DeviceManager device this receiver uses
    Demodulator demod;                // own demod instance (already per-state)

    double freqHz = 100e6;
    DemodMode mode = DemodMode::NFM;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;

    // P1 audit: explicit separation (RF is hardware sensitivity on the SDR; audio is post-demod gain; display for viz/spectrum scaling).
    // setLiveGain on DeviceManager applies RF immediately to running Soapy device when possible.
    double rfGainDb = 20.0;
    double audioGain = 1.0;
    double displayGain = 1.0;

    double gain = 1.0; // legacy alias (maps to audioGain during transition)
    double wfmDeTauUs = 75.0;
    double wfmPilotNotchR = 0.96;

    // P25 clear voice follow state. When enabled, the DSP worker bypasses the
    // analog NFM demodulator and routes valid IMBE/AMBE voice frames through mbelib.
    bool p25VoiceDecodeEnabled = false;
    bool p25VoiceClearKnown = false;
    bool p25VoiceEncrypted = false;
    uint32_t p25VoiceTalkgroupId = 0;
    uint32_t p25VoiceSourceId = 0;
    int64_t p25VoiceGrantEpochMs = 0;
    // Binds pending audio, ESS security, and emit gates to one call instance.
    // Generated at grant arm from talkgroup + grant epoch (SDRTrunk current-call session).
    uint64_t p25CurrentCallSessionId = 0;
    bool p25VoicePhase2 = false;
    bool p25VoiceTdmaSlotKnown = false;
    uint8_t p25VoiceTdmaSlot = 0;
    // Sticky Phase-2 slot-label invert for the current call.  When the superframe
    // epoch is one burst early/late, grant slot labels flip and all VCWs land on
    // the opposite slot.  Once a clear-grant window proves that pattern, keep
    // the invert active for the rest of the call (SDRTrunk never flips mid-call;
    // it binds the traffic channel timeslot once and stays continuous).
    bool p25Phase2StickySlotLabelInvert = false;
    int p25Phase2OppositeOnlyWindows = 0;
    bool p25VoiceSlotProbePending = false;
    uint8_t p25VoiceSlotProbeRequested = 0;
    bool p25VoiceMaskParamsKnown = false;
    uint16_t p25VoiceNac = 0;
    uint32_t p25VoiceWacn = 0;
    uint16_t p25VoiceSystemId = 0;
    int64_t p25VoiceSettleUntilMs = 0;
    int p25VoiceDiscardWindows = 0;
    bool p25ControlChannelMute = false;
    // True when this receiver is an sdrtrunk-style traffic-channel source
    // created from a P25 trunking grant rather than the user's primary monitor
    // receiver.  These receivers are safe to remove automatically when the call
    // ends/returns to control.  The same pattern can be reused by other demods
    // that need an independent virtual channel source from a wideband SDR ring.
    bool p25IndependentTrafficSource = false;
    // True when this traffic source was created by retuning the primary
    // physical tuner away from the control channel.  The application must
    // retune RF back to p25TrafficControlFreqHz before reporting CC monitor.
    bool p25TrafficRetunesPrimary = false;
    // Monotonic generation assigned by the GUI/control side when creating a
    // P25 traffic-channel source.  DSP workers check this before processing so
    // a stale shared_ptr captured in a previous receiver snapshot cannot keep
    // demodulating an old grant after a newer grant/source has replaced it.
    uint64_t p25TrafficGeneration = 0;
    double p25TrafficControlFreqHz = 0.0;
    double p25TrafficVoiceFreqHz = 0.0;
    uint8_t p25TrafficSlot = 0;
    int64_t p25TrafficLastGrantMs = 0;
    bool p25Phase2TrafficTargetOffsetKnown = false;
    double p25Phase2TrafficTargetOffsetHz = 0.0;
    int p25Phase2TrafficTargetOffsetTrust = 0;
    int p25Phase2TrafficTargetOffsetMisses = 0;
    int64_t p25Phase2RecentTrafficEvidenceMs = 0;
    uint64_t p25Phase2LastEmittedAbsDibit = 0;
    // sdrtrunk-style Phase 2 audio security gate: for unknown grants, follow
    // the traffic channel but hold decoded PCM until current-call MAC/ESS
    // establishes clear audio.  Drop the queue if trusted
    // encrypted metadata arrives or the call identity changes.
    std::vector<float> p25Phase2PendingAudio;
    uint32_t p25Phase2PendingTalkgroupId = 0;
    bool p25Phase2PendingAudioArmed = false;
    // Short-lived, active-call Phase-2 security evidence.  MAC/ESS and voice
    // often arrive in different DSP windows, so the audio gate must remember
    // current-call MAC/ESS context without promoting it across calls.
    uint32_t p25Phase2RecentSecurityTalkgroupId = 0;
    uint8_t p25Phase2RecentSecuritySlot = 0xffu;
    int64_t p25Phase2RecentSecurityFrequencyHz = 0;
    int64_t p25Phase2RecentSecurityEvidenceMs = 0;
    bool p25Phase2RecentTargetMacCrcValid = false;
    bool p25Phase2RecentAnyMacCrcValid = false;
    bool p25Phase2RecentTargetEssKnown = false;
    bool p25Phase2RecentTargetEssEncrypted = false;
    bool p25Phase2RecentTargetSessionAudioRelease = false;
    bool p25Phase2RecentSuperframeMaskLock = false;
    // Guarded Phase-2 field recovery. Explicit encrypted grants/ESS still mute
    // immediately, while late-entry unknown calls queue audio until target-slot
    // PTT/ESS or strong target MAC/mask/voice evidence
    // plus sane decoded PCM proves the current traffic slot is clear.
    bool p25Phase2AllowLateEntryAudioProbe = false;
    bool p25VoiceResetPending = false;
    P25VoiceDiagSnapshot p25VoiceDiagnostics;
    P25LiveDecoder p25VoiceLiveDecoder{p25RealtimeVoiceDecoderConfig()};
    std::unique_ptr<P25TrafficChannelProcessor> p25TrafficProcessor;
    P25ImbeVoiceDecoder p25ImbeVoiceDecoder;
    P25AmbeVoiceDecoder p25AmbeVoiceDecoder;
    // Per-receiver P25 audio queue, resampler, and AMBE emit de-dupe (no global maps).
    P25ReceiverSessionState p25SessionState;
    int p25Phase2PreferredAmbeVariant = -1;
    int p25Phase2PreferredAmbeVariantHits = 0;
    int p25Phase2PreferredAmbeVariantMisses = 0;
    std::array<int, 4> p25Phase2PreferredAmbeVariantByVoiceIndex = {-1, -1, -1, -1};
    std::array<int, 4> p25Phase2PreferredAmbeVariantHitsByVoiceIndex = {};
    std::array<int, 4> p25Phase2PreferredAmbeVariantMissesByVoiceIndex = {};
    // Last good resampled AMBE block for packet-loss concealment between frames.
    std::vector<float> p25Phase2LastGoodPcm;

    // Narrow-FM AFC: keeps the user tuned to the nominal channel while DSP
    // recenters the demodulator when SDR PPM/tuning error moves the carrier.
    bool afcEnabled = true;
    bool afcLocked = false;
    double afcOffsetHz = 0.0;
    bool p25AfcFrozen = false;
    double p25FrozenAfcOffsetHz = 0.0;

    // Per-receiver audio routing. Empty means mirror to all active outputs.
    std::vector<size_t> audioOutputIndices;  // active AudioEngine output indices

    // Stubs for future features (recording, waterfall data, scheduler)
    bool recordingAudio = false;
    bool recordingIQ = false;
    // TODO: std::unique_ptr<Recorder> recorder;
    // TODO: per-receiver spectrum/waterfall buffer
    // TODO: schedule / hunter / sat tracking state

    bool active = false;              // is this receiver currently processing?

    // S0 / audit-followup-2: per-receiver read cursor (absolute samples) for the device's IQ ring.
    // Allows each rx to consume its own new chronological data without fighting other rxs on the same device.
    std::atomic<uint64_t> lastConsumedAbsolute{0};
    std::atomic<uint64_t> lastSeenStreamEpoch{0};
    std::atomic<uint64_t> p25TrafficSessionGeneration{1};

    // Simple reset helper (calls into Demodulator)
    void resetDemodState()
    {
        std::lock_guard<std::recursive_mutex> dspLock(dspMutex);
        demod.resetState();
        lastConsumedAbsolute.store(0, std::memory_order_release);
        lastSeenStreamEpoch.store(0, std::memory_order_release);
        afcLocked = false;
        afcOffsetHz = 0.0;
        p25AfcFrozen = false;
        p25FrozenAfcOffsetHz = 0.0;
        p25Phase2TrafficTargetOffsetKnown = false;
        p25Phase2TrafficTargetOffsetHz = 0.0;
        p25Phase2TrafficTargetOffsetTrust = 0;
        p25Phase2TrafficTargetOffsetMisses = 0;
        p25Phase2RecentTrafficEvidenceMs = 0;
    }
    void resetP25TrafficSession(const char* /*reason*/ = nullptr, bool fullClear = true)
    {
        std::lock_guard<std::recursive_mutex> dspLock(dspMutex);
        p25TrafficSessionGeneration.fetch_add(1, std::memory_order_acq_rel);
        p25VoiceLiveDecoder.reset();
        if (fullClear) {
            p25TrafficProcessor.reset();
            p25ImbeVoiceDecoder = P25ImbeVoiceDecoder();
            p25AmbeVoiceDecoder = P25AmbeVoiceDecoder();
        }
        p25Phase2PreferredAmbeVariant = -1;
        p25Phase2PreferredAmbeVariantHits = 0;
        p25Phase2PreferredAmbeVariantMisses = 0;
        p25Phase2PreferredAmbeVariantByVoiceIndex.fill(-1);
        p25Phase2PreferredAmbeVariantHitsByVoiceIndex.fill(0);
        p25Phase2PreferredAmbeVariantMissesByVoiceIndex.fill(0);
        p25Phase2LastGoodPcm.clear();
        p25Phase2TrafficTargetOffsetKnown = false;
        p25Phase2TrafficTargetOffsetHz = 0.0;
        p25Phase2TrafficTargetOffsetTrust = 0;
        p25Phase2TrafficTargetOffsetMisses = 0;
        p25Phase2RecentTrafficEvidenceMs = 0;
        p25Phase2LastEmittedAbsDibit = 0;
        p25Phase2StickySlotLabelInvert = false;
        p25Phase2OppositeOnlyWindows = 0;
        p25Phase2PendingAudio.clear();
        p25Phase2PendingTalkgroupId = 0;
        p25Phase2PendingAudioArmed = false;
        p25Phase2RecentSecurityTalkgroupId = 0;
        p25Phase2RecentSecuritySlot = 0xffu;
        p25Phase2RecentSecurityFrequencyHz = 0;
        p25Phase2RecentSecurityEvidenceMs = 0;
        p25Phase2RecentTargetMacCrcValid = false;
        p25Phase2RecentAnyMacCrcValid = false;
        p25Phase2RecentTargetEssKnown = false;
        p25Phase2RecentTargetEssEncrypted = false;
        p25Phase2RecentTargetSessionAudioRelease = false;
        p25Phase2RecentSuperframeMaskLock = false;
        p25SessionState.clearAll();
        p25VoiceSourceId = 0;
        p25VoiceGrantEpochMs = 0;
        p25CurrentCallSessionId = 0;
    }

    void resetP25VoiceState()
    {
        resetP25TrafficSession("legacy-reset", true);
    }

    // Force immediate squelch gate close (bypass hang) when user raises threshold via main controls or CLI.
    // Makes "set sq higher" live without requiring a freq retune/click (which previously forced a full reset).
    void resetSquelchGate()
    {
        std::lock_guard<std::recursive_mutex> dspLock(dspMutex);
        demod.resetSquelchGate();
    }
};

// Placeholder for future per-receiver audio push (currently falls back to global engine)
// void pushReceiverAudio(Receiver& rx, const float* samples, size_t count);
