#include "P25VoiceSession.h"

#include <QByteArray>
#include <QDateTime>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

qint64 p25Phase2EffectiveAudioTailGraceMs() noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) {
        return kP25Phase2SpeakerAudioTailGraceMs;
    }
    return kP25Phase2AudioTailGraceMs;
}


bool p25Phase2SessionHasHardTargetAcquire(const Receiver& rx) noexcept
{
    const auto& sustain = rx.p25SessionState.sustain;
    const bool acquired =
        sustain.hadSuccessfulEmit ||
        sustain.peakDecodedFrames > 0 ||
        sustain.cumulativeAudioSamples > 0 ||
        p25DiagTargetHardClear(rx.p25VoiceDiagnostics);
    if (rx.p25Phase2WideReacquireHoldWindows > 0 && !acquired) return false;
    return acquired;
}

bool p25Phase2SessionHadVoiceLock(const Receiver& rx) noexcept
{
    return p25Phase2SessionHasHardTargetAcquire(rx);
}

// Soft telemetry only proves that a Phase-2 eye may be present. It must not move
// a windowed decoder into sustain mode before target-slot MAC/ESS/audio proves
// the call; SDRTrunk can stream dibits continuously, while this path still needs
// full acquisition context until hard call state is known.
bool p25Phase2SessionHadBurstEye(const Receiver& rx) noexcept
{
    const auto& sustain = rx.p25SessionState.sustain;
    const bool voiceLock = p25Phase2SessionHadVoiceLock(rx);
    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !voiceLock &&
        !sustain.hadSuccessfulEmit) {
        return false;
    }
    if (voiceLock) return true;
    if (sustain.peakPhase2Bursts >= 1) return true;
    if (rx.p25VoiceDiagnostics.phase2Bursts > 0) return true;
    return rx.p25VoiceLiveDecoder.cqpskLockValid();
}

bool p25Phase2SessionSpeakerSustainActive(const Receiver& rx) noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto& sustain = rx.p25SessionState.sustain;
    if (!sustain.hadSuccessfulEmit) return false;
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) return true;
    // Keep sustain streaming after the last PCM push so inter-slot silence and
    // CQPSK re-lock do not drop us back to cold/unacquired hops (032428 droughts).
    return sustain.lastEmitMs > 0 && (nowMs - sustain.lastEmitMs) <= 20000;
}

bool p25Phase2EstablishedClearVoiceStreamingLocked(const Receiver& rx) noexcept
{
    if (!rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2 || rx.p25VoiceEncrypted) {
        return false;
    }
    // After successful emit, ignore wide-reacquire hold flags so short sustain
    // hops stay selected (see NeedsWideReacquireWindowLocked).
    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit) {
        return false;
    }

    const auto& sustain = rx.p25SessionState.sustain;
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const bool decodedAudio =
        sustain.hadSuccessfulEmit ||
        sustain.cumulativeAudioSamples > 0 ||
        sustain.peakDecodedFrames > 0 ||
        diag.decodedFrames > 0 ||
        diag.phase2AmbeAcceptedFrames > 0 ||
        diag.audioSamples > 0;
    const bool selectedSlotEvidence =
        sustain.peakPhase2TargetVoiceCodewords > 0 ||
        diag.phase2TargetVoiceCodewords > 0 ||
        p25DiagTargetHardClear(diag);
    const bool targetClearEvidence =
        p25DiagTargetHardClear(diag);
    const bool maskOrStickyLock =
        sustain.hadBootstrapMaskLock ||
        sustain.peakPhase2MaskedBursts >= 1 ||
        diag.phase2MaskedBursts > 0 ||
        rx.p25VoiceLiveDecoder.cqpskLockValid();
    const bool selectedClearTrafficStreaming =
        rx.p25VoiceClearKnown &&
        rx.p25VoiceMaskParamsKnown &&
        rx.p25VoiceTdmaSlotKnown &&
        selectedSlotEvidence &&
        (maskOrStickyLock || targetClearEvidence) &&
        (targetClearEvidence ||
         sustain.hadSuccessfulEmit);

    return (decodedAudio && selectedSlotEvidence && maskOrStickyLock &&
            (targetClearEvidence || sustain.hadSuccessfulEmit)) ||
        selectedClearTrafficStreaming;
}

double p25Phase2EffectiveRollingWindowSeconds(const Receiver& rx) noexcept
{
    if (p25Phase2SessionSpeakerSustainActive(rx) ||
        p25Phase2EstablishedClearVoiceStreamingLocked(rx) ||
        p25Phase2SessionHadVoiceLock(rx)) {
        return kP25Phase2VoiceDecodeActiveRollingSeconds;
    }
    return kP25Phase2VoiceDecodeWindowSeconds;
}

bool p25TrustedControlOffsetForPhase2Traffic(double controlFreqHz,
                                                    qint64 nowMs,
                                                    double* outOffsetHz) noexcept
{
    if (!outOffsetHz) return false;
    *outOffsetHz = 0.0;
    if (!std::isfinite(controlFreqHz) || controlFreqHz <= 0.0) return false;
    const double trustedControlFreqHz = gP25LastTrustedControlFreqHz.load(std::memory_order_acquire);
    const double trustedControlOffsetHz = gP25LastTrustedControlOffsetHz.load(std::memory_order_acquire);
    const long long trustedControlOffsetMs = gP25LastTrustedControlOffsetMs.load(std::memory_order_acquire);
    if (trustedControlOffsetMs <= 0) return false;
    if (nowMs <= 0) nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - static_cast<qint64>(trustedControlOffsetMs) > kP25Phase2ControlCarryFreshMs) return false;
    if (!std::isfinite(trustedControlFreqHz) || std::abs(trustedControlFreqHz - controlFreqHz) > 50.0) return false;
    if (!std::isfinite(trustedControlOffsetHz)) return false;
    const double absOffsetHz = std::abs(trustedControlOffsetHz);
    if (absOffsetHz < kP25Phase2ControlCarryOffsetMinHz ||
        absOffsetHz > kP25Phase2ControlCarryOffsetMaxHz) {
        return false;
    }
    *outOffsetHz = std::clamp(trustedControlOffsetHz,
                              -kP25Phase2TrafficTargetOffsetMaxHz,
                              kP25Phase2TrafficTargetOffsetMaxHz);
    return true;
}

void p25SeedPhase2TrafficOffsetFromControl(Receiver& rx,
                                                  double offsetHz,
                                                  int trust) noexcept
{
    if (!std::isfinite(offsetHz)) return;
    const double absOffsetHz = std::abs(offsetHz);
    if (absOffsetHz < kP25Phase2ControlCarryOffsetMinHz ||
        absOffsetHz > kP25Phase2TrafficTargetOffsetMaxHz) {
        return;
    }
    const double clampedOffsetHz = std::clamp(offsetHz,
                                             -kP25Phase2TrafficTargetOffsetMaxHz,
                                             kP25Phase2TrafficTargetOffsetMaxHz);
    rx.p25Phase2TrafficTargetOffsetKnown = true;
    rx.p25Phase2TrafficTargetOffsetHz = clampedOffsetHz;
    rx.p25Phase2TrafficTargetOffsetTrust = std::clamp(trust, 0, kP25Phase2TrafficTargetOffsetVerifiedTrust);
    rx.p25Phase2TrafficTargetOffsetMisses = 0;
    if (rx.p25TrafficRetunesPrimary) {
        rx.p25AfcFrozen = false;
        rx.p25FrozenAfcOffsetHz = clampedOffsetHz;
    }
}

int p25Phase2AdaptiveVoiceDecodeCadenceMs() noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    // During active speaker sustain, keep a fixed near-live cadence.  Scaling
    // scheduler sleep to the last DSP pass duration created positive feedback:
    // slow 1–4 s acquire windows stretched the gap between sustain chunks and
    // produced blocky one-burst-then-silence audio.
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) {
        return kP25Phase2VoiceDecodeSpeakerCadenceMs;
    }
    return kP25Phase2VoiceDecodeCadenceMs;
}

int p25Phase2AdaptiveVoiceDecodeCadenceMs(const Receiver& rx) noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    // Keep near-live hop rate for the whole call after first emit — not only
    // while the 2.5 s speaker-hold flag is warm (capture 20260807_231232).
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs) ||
        (rx.p25VoicePhase2 && rx.p25SessionState.sustain.hadSuccessfulEmit)) {
        return kP25Phase2VoiceDecodeSpeakerCadenceMs;
    }
    const bool coldAcquire =
        rx.p25IndependentTrafficSource &&
        rx.p25VoicePhase2 &&
        rx.p25VoiceDecodeEnabled &&
        !p25Phase2SessionHadBurstEye(rx) &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit;
    if (coldAcquire) return kP25Phase2VoiceDecodeColdCadenceMs;
    return kP25Phase2VoiceDecodeCadenceMs;
}

bool p25Phase2SpeakerSustainDecodeActive() noexcept
{
    // Global hint used for pending-job depth; prefer speaker-hold, but do not
    // starve the pipeline the moment hold expires mid-call.
    return p25RecentSpeakerOutputActive(QDateTime::currentMSecsSinceEpoch(),
                                        kP25Phase2SpeakerFollowHoldMs);
}

size_t p25VoiceDecodeMaxPendingJobsNow(bool speakerSustainHint) noexcept
{
    // Capture 20260807_231232: worker-busy starved unique VCW feed (97 busy
    // logs, feedRatio≈0.25). Keep a short pipeline so 30–60 ms hops are not
    // dropped while a 25–30 ms sticky decode is finishing.
    return (speakerSustainHint || p25Phase2SpeakerSustainDecodeActive())
        ? kP25VoiceDecodeMaxPendingJobsSpeaker
        : kP25VoiceDecodeMaxPendingJobs;
}

size_t p25VoiceDecodeMaxPendingJobsNow() noexcept
{
    return p25VoiceDecodeMaxPendingJobsNow(false);
}

bool p25Phase2HasStableSuperframeLockLocked(const Receiver& rx) noexcept
{
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const auto& sustain = rx.p25SessionState.sustain;
    const long long sfBursts = std::max(diag.phase2SuperframeBursts, sustain.peakPhase2SuperframeBursts);
    const long long maskBursts = std::max(diag.phase2MaskedBursts, sustain.peakPhase2MaskedBursts);
    return (sfBursts >= 6 && maskBursts >= 3) ||
           (rx.p25Phase2RecentSuperframeMaskLock && sfBursts >= 3) ||
           (sustain.hadBootstrapMaskLock && sustain.hadSuccessfulEmit);
}

bool p25Phase2NeedsWideReacquireWindowLocked(const Receiver& rx) noexcept
{
    if (!rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) return false;
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const auto& sustain = rx.p25SessionState.sustain;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentSpeaker =
        p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs);
    const bool recentTraffic =
        rx.p25Phase2RecentTrafficEvidenceMs > 0 &&
        (nowMs - rx.p25Phase2RecentTrafficEvidenceMs) <= 15000;
    const bool currentStreamingEye =
        diag.phase2Bursts > 0 ||
        diag.phase2VoiceCodewords > 0 ||
        diag.phase2MaskedBursts > 0;

    // Capture 20260807_230551: after gate=emit, many windows have p2bursts=0
    // (opposite TDMA slot / brief silence). The old logic treated that as
    // "lost eye" and forced wide-reacquire with minFresh=120-160ms, producing
    // 80ms PCM islands every 300ms+ (blocky speech) while slot isolation stayed
    // correct. Once the speaker has opened, stay on short sustain hops.
    if (sustain.hadSuccessfulEmit) {
        if (recentSpeaker) return false;
        if (currentStreamingEye) return false;
        if (recentTraffic) return false;
        const qint64 silenceMs = (sustain.lastEmitMs > 0) ? (nowMs - sustain.lastEmitMs) : 0;
        // Only cold wide-reacquire after a real hang (not inter-slot silence).
        if (silenceMs > 0 && silenceMs < 8000) return false;
    }

    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !p25DiagTargetHardClear(diag) &&
        diag.decodedFrames == 0 &&
        !sustain.hadSuccessfulEmit) {
        return true;
    }
    // Mask-epoch repair must not steal the speaker-live path after emit.
    if (rx.p25Phase2MaskEpochRepairHoldWindows > 0 && !sustain.hadSuccessfulEmit) {
        return true;
    }
    if (currentStreamingEye) {
        return false;
    }
    const bool hadAnySync =
        sustain.peakPhase2Bursts > 0 ||
        sustain.peakDecodedFrames > 0 ||
        sustain.hadSuccessfulEmit ||
        diag.decodedFrames > 0 ||
        diag.audioSamples > 0 ||
        recentTraffic;
    // Do not reacquire solely because we once emitted — that is the chop path.
    return hadAnySync &&
        !sustain.hadSuccessfulEmit &&
        (diag.decodedFrames > 0 ||
         diag.audioSamples > 0 ||
         sustain.peakDecodedFrames > 0);
}

bool p25Phase2UseSustainDecodeWindowLocked(const Receiver& rx) noexcept
{
    if (!rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) return false;
    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit) {
        return false;
    }
    if (rx.p25Phase2MaskEpochRepairHoldWindows > 0 &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit) {
        return false;
    }
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const auto& sustain = rx.p25SessionState.sustain;
    const bool hasDecodedAudio =
        diag.decodedFrames > 0 ||
        diag.phase2AmbeAcceptedFrames > 0 ||
        diag.audioSamples > 0 ||
        sustain.peakDecodedFrames > 0 ||
        sustain.cumulativeAudioSamples > 0;
    const bool hasStableSuperframeMask =
        std::max(diag.phase2SuperframeBursts, sustain.peakPhase2SuperframeBursts) >= 8 &&
        std::max(diag.phase2MaskedBursts, sustain.peakPhase2MaskedBursts) >= 8 &&
        (diag.phase2TargetVoiceCodewords > 0 ||
         diag.phase2VoiceCodewords > 0 ||
         sustain.peakPhase2TargetVoiceCodewords > 0);
    const bool hasBootstrappedMaskLock =
        std::max(diag.phase2MaskedBursts, sustain.peakPhase2MaskedBursts) >= 1 &&
        std::max(diag.phase2SuperframeBursts, sustain.peakPhase2SuperframeBursts) >= 1 &&
        (diag.phase2TargetVoiceCodewords > 0 ||
         diag.phase2VoiceCodewords > 0 ||
         diag.decodedFrames > 0 ||
         sustain.peakDecodedFrames > 0 ||
         sustain.hadBootstrapMaskLock);
    const bool hasTrustedCallState =
        !rx.p25VoiceEncrypted &&
        p25DiagTargetHardClear(diag);
    const bool trustedAndFramed =
        hasTrustedCallState && hasStableSuperframeMask;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentTrafficEvidence =
        rx.p25Phase2RecentTrafficEvidenceMs > 0 &&
        (nowMs - rx.p25Phase2RecentTrafficEvidenceMs) <= 8000;
    const bool recentSuperframeMaskLock =
        rx.p25Phase2RecentSuperframeMaskLock &&
        recentTrafficEvidence;
    const bool recentSpeaker =
        gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed) > 0 &&
        nowMs - gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed) <= 12000;
    return hasDecodedAudio ||
        sustain.hadSuccessfulEmit ||
        p25Phase2SessionSpeakerSustainActive(rx) ||
        trustedAndFramed ||
        (hasTrustedCallState && (hasStableSuperframeMask ||
                                 hasBootstrappedMaskLock ||
                                 recentTrafficEvidence ||
                                 recentSuperframeMaskLock)) ||
        (recentSpeaker && rx.p25VoiceMaskParamsKnown && hasTrustedCallState);
}


int p25Phase2StreamingDdcEnvOverride() noexcept
{
    // 1 = force on, -1 = force off, 0 = default (traffic-source only).
    static const int override = [] {
        const QByteArray value = qgetenv("SDR_TOWN_P25_STREAMING_DDC").trimmed().toLower();
        if (value == "1" || value == "true" || value == "yes" || value == "on") return 1;
        if (value == "0" || value == "false" || value == "no" || value == "off") return -1;
        return 0;
    }();
    return override;
}

bool p25Phase2StreamingDdcExperimentEnabled()
{
    return p25Phase2StreamingDdcEnvOverride() > 0;
}
