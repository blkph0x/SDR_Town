#include "P25FollowStateMachine.h"

#include <algorithm>
#include <cmath>

namespace {

bool diagIs(int diag, P25FollowDiagCode code)
{
    return diag == static_cast<int>(code);
}

bool diagIsPhase2Specific(int diag)
{
    return diagIs(diag, P25FollowDiagCode::Phase2Unsupported) ||
        diagIs(diag, P25FollowDiagCode::Phase2AudioLockMissing) ||
        diagIs(diag, P25FollowDiagCode::Phase2MetadataMissing) ||
        diagIs(diag, P25FollowDiagCode::Phase2MaskMissing) ||
        diagIs(diag, P25FollowDiagCode::Phase2MaskAppliedNoMacCrc) ||
        diagIs(diag, P25FollowDiagCode::Phase2EssMissing) ||
        diagIs(diag, P25FollowDiagCode::Phase2WrongSlot) ||
        diagIs(diag, P25FollowDiagCode::Phase2AmbeRejected) ||
        diagIs(diag, P25FollowDiagCode::Phase2LateEntryWaiting);
}

} // namespace

P25FollowDecision evaluateP25Follow(const P25FollowSnapshot& snapshot)
{
    P25FollowDecision decision;
    decision.effectiveTalkgroupId = snapshot.talkgroupId != 0
        ? snapshot.talkgroupId
        : snapshot.fallbackTalkgroupId;

    const bool hasPublishedVoiceDiagnostic =
        snapshot.diagUpdatedMs > 0 ||
        snapshot.diag != static_cast<int>(P25FollowDiagCode::Idle) ||
        snapshot.syncs > 0 ||
        snapshot.nids > 0 ||
        snapshot.imbeFrames > 0 ||
        snapshot.decodedFrames > 0 ||
        snapshot.phase2Bursts > 0 ||
        snapshot.phase2VoiceCodewords > 0 ||
        snapshot.phase2SuperframeBursts > 0 ||
        snapshot.phase2MaskedBursts > 0 ||
        snapshot.phase2MacPdus > 0 ||
        snapshot.phase2MacCrcValid > 0 ||
        snapshot.phase2EssKnown ||
        snapshot.phase2TrafficProcessorActive;

    const bool diagnosticFresh =
        hasPublishedVoiceDiagnostic &&
        (snapshot.diagUpdatedMs <= 0 ||
         snapshot.nowMs <= 0 ||
         snapshot.nowMs - snapshot.diagUpdatedMs <= 2500);

    const bool phase2Follow = snapshot.phase2Voice ||
        snapshot.phase2Bursts > 0 ||
        snapshot.phase2VoiceCodewords > 0 ||
        snapshot.phase2SuperframeBursts > 0 ||
        snapshot.phase2MaskedBursts > 0 ||
        snapshot.phase2MacPdus > 0 ||
        snapshot.phase2MacCrcValid > 0 ||
        snapshot.phase2EssKnown ||
        snapshot.phase2TrafficProcessorActive ||
        diagIsPhase2Specific(snapshot.diag);

    const bool phase2TrustedActivity =
        snapshot.decodedFrames > 0 ||
        snapshot.phase2MacCrcValid > 0 ||
        snapshot.phase2EssKnown ||
        snapshot.phase2TrafficAudioOpen ||
        (snapshot.phase2SuperframeBursts >= 10 && snapshot.phase2MaskedBursts >= 10);

    // Voice still active requires recent *voice* evidence (VCW or decoded or traffic audio open with VCW),
    // not just any bursts or sf/mask framing (which can linger on a dead freq or inactive TG).
    // This fixes "stuck on inactive talk groups".
    // Partial sf/mask without ongoing VCW is explicitly not sufficient (see SDRTrunk comments in prior code).
    const bool hasRecentVoiceVcws = snapshot.phase2VoiceCodewords > 0 || snapshot.decodedFrames > 0 || snapshot.imbeFrames > 0;
    bool hasCarrier = true;
    if (snapshot.rfMetricsPopulated) {
        hasCarrier = snapshot.recentSnrDb > 3.0 ||
                     (snapshot.recentSignalLevelDb > snapshot.recentNoiseFloorDb + 5.0);
    }
    decision.voiceStillLooksActive = diagnosticFresh &&
        (hasRecentVoiceVcws || snapshot.phase2TrafficCallActive) &&
        hasCarrier &&  // use actual RF energy (peak in BW vs noise floor) to ignore background noise as "real data"
        (snapshot.phase2TrafficAudioOpen ||
         snapshot.phase2TrafficCallActive ||
         // Only fall back to pure sync/nid for very brief P1-style or initial acquisition; not for sustained follow.
         (snapshot.phase2VoiceCodewords == 0 && snapshot.phase2Bursts == 0 &&
          (snapshot.syncs > 0 || snapshot.nids > 0) && (snapshot.nowMs - snapshot.tunedAtMs) < 3000));

    const bool trustedEncryptedEss =
        snapshot.phase2EssKnown &&
        snapshot.phase2EssEncrypted &&
        snapshot.phase2MacCrcValid > 0;
    decision.encryptedOnVoice =
        diagIs(snapshot.diag, P25FollowDiagCode::SkippedEncrypted) ||
        snapshot.phase2TrafficEncrypted ||
        trustedEncryptedEss;
    if (decision.encryptedOnVoice) {
        decision.action = P25FollowAction::ReturnEncrypted;
        return decision;
    }

    if (!snapshot.autoActive) {
        return decision;
    }

    const bool initialHoldExpired =
        snapshot.tunedAtMs > 0 && snapshot.nowMs - snapshot.tunedAtMs > 2500;
    const int64_t lastSignalMs = std::max(snapshot.lastActiveMs, snapshot.tunedAtMs);
    const int64_t silenceSinceSignalMs = snapshot.nowMs - lastSignalMs;
    decision.activityGone =
        initialHoldExpired &&
        silenceSinceSignalMs > 3500 &&
        (diagIs(snapshot.diag, P25FollowDiagCode::NoSync) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NoLduVoice));

    const bool noTrustedPhase2MacEssYet =
        phase2Follow &&
        snapshot.tunedAtMs > 0 &&
        snapshot.phase2MacCrcValid == 0 &&
        (!snapshot.phase2EssKnown || snapshot.phase2EssEncrypted) &&
        snapshot.decodedFrames == 0 &&
        !snapshot.phase2TrafficAudioOpen;

    decision.tdmaEpochLockedNoMacEss =
        noTrustedPhase2MacEssYet &&
        snapshot.phase2SuperframeBursts >= 4 &&
        snapshot.phase2MaskedBursts >= 4;

    const bool phase2RecentContinuation =
        phase2Follow &&
        snapshot.lastActiveMs > snapshot.tunedAtMs &&
        snapshot.nowMs > snapshot.lastActiveMs &&
        !snapshot.phase2EssEncrypted;
    const int64_t tdmaVcwNoSuperframeTunedMs = phase2RecentContinuation ? 22000 : 15000;
    const int64_t tdmaVcwNoSuperframeSilenceMs = phase2RecentContinuation ? 10000 : 3500;
    const int64_t tdmaNoVcwTunedMs = phase2RecentContinuation ? 18000 : 9000;
    const int64_t tdmaNoVcwSilenceMs = phase2RecentContinuation ? 10000 : 3500;
    const int64_t tdmaNoMacEssTunedMs = phase2RecentContinuation ? 30000 : 15000;
    const int64_t tdmaNoMacEssSilenceMs = phase2RecentContinuation ? 10000 : 3500;

    const bool firstDiagnosticGraceExpired =
        phase2Follow &&
        !hasPublishedVoiceDiagnostic &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > 30000;

    decision.hardTimeout =
        snapshot.tunedAtMs > 0 &&
        (hasPublishedVoiceDiagnostic || firstDiagnosticGraceExpired) &&
        snapshot.nowMs - snapshot.tunedAtMs > 12000 &&
        silenceSinceSignalMs > 3000 &&
        !snapshot.phase2TrafficCallActive &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0;

    bool hasCarrierForDrop = true;
    if (snapshot.rfMetricsPopulated) {
        hasCarrierForDrop = snapshot.recentSnrDb > 3.0 ||
                            (snapshot.recentSignalLevelDb > snapshot.recentNoiseFloorDb + 5.0);
    }
    decision.carrierDropped = initialHoldExpired &&
        !hasCarrierForDrop &&
        silenceSinceSignalMs > 800;  // quick return once carrier drops after voice ends

    decision.tdmaVcwNoSuperframeTimeout =
        noTrustedPhase2MacEssYet &&
        snapshot.nowMs - snapshot.tunedAtMs > tdmaVcwNoSuperframeTunedMs &&
        silenceSinceSignalMs > tdmaVcwNoSuperframeSilenceMs &&
        snapshot.phase2VoiceCodewords >= 4 &&
        snapshot.phase2SuperframeBursts == 0 &&
        snapshot.phase2MaskedBursts == 0 &&
        (diagIs(snapshot.diag, P25FollowDiagCode::Phase2AudioLockMissing) ||
         diagIs(snapshot.diag, P25FollowDiagCode::Phase2LateEntryWaiting) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NidUnlocked) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NoDecodedAudio) ||
         diagIs(snapshot.diag, P25FollowDiagCode::WaitingForClearGrant));

    const bool tdmaPartialEpochNoProgress =
        noTrustedPhase2MacEssYet &&
        snapshot.nowMs - snapshot.tunedAtMs > tdmaNoMacEssTunedMs &&
        silenceSinceSignalMs > tdmaNoMacEssSilenceMs &&
        snapshot.phase2VoiceCodewords >= 4 &&
        snapshot.phase2SuperframeBursts > 0 &&
        snapshot.phase2MaskedBursts > 0 &&
        (snapshot.phase2SuperframeBursts < 10 || snapshot.phase2MaskedBursts < 10);

    decision.tdmaNoProgressTimeout =
        (decision.tdmaEpochLockedNoMacEss &&
         snapshot.nowMs - snapshot.tunedAtMs > tdmaNoMacEssTunedMs &&
         silenceSinceSignalMs > tdmaNoMacEssSilenceMs) ||
        decision.tdmaVcwNoSuperframeTimeout ||
        tdmaPartialEpochNoProgress;


    const bool tdmaNoAcquireIdleTimeout =
        phase2Follow &&
        hasPublishedVoiceDiagnostic &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > 4500 &&
        silenceSinceSignalMs > 1200 &&
        !snapshot.phase2TrafficCallActive &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        diagIs(snapshot.diag, P25FollowDiagCode::Idle);

    decision.tdmaNoVcwTimeout =
        phase2Follow &&
        hasPublishedVoiceDiagnostic &&
        snapshot.tunedAtMs > 0 &&
        (snapshot.nowMs - snapshot.tunedAtMs > tdmaNoVcwTunedMs ||
         tdmaNoAcquireIdleTimeout) &&
        silenceSinceSignalMs > tdmaNoVcwSilenceMs &&
        !snapshot.phase2TrafficCallActive &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        // Do not require bursts==0; noise can still produce "bursts" counts. VCW==0 + no decoded is enough.
        (diagIs(snapshot.diag, P25FollowDiagCode::Idle) ||
         diagIs(snapshot.diag, P25FollowDiagCode::WaitingForClearGrant) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NoSync) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NoLduVoice) ||
         diagIs(snapshot.diag, P25FollowDiagCode::Phase2AudioLockMissing));

    if (decision.tdmaNoProgressTimeout) {
        decision.action = P25FollowAction::ReturnNoMacEss;
    } else if (decision.tdmaNoVcwTimeout) {
        decision.action = P25FollowAction::ReturnNoVoiceCodewords;
    } else if (decision.activityGone) {
        decision.action = P25FollowAction::ReturnActivityGone;
    } else if (decision.hardTimeout) {
        decision.action = P25FollowAction::ReturnHardTimeout;
    } else if (decision.carrierDropped) {
        decision.action = P25FollowAction::ReturnActivityGone;
    }

    return decision;
}

P25SlotProbeDecision evaluateP25SlotProbe(const P25SlotProbeSnapshot& snapshot)
{
    P25SlotProbeDecision decision;
    decision.resetTracking =
        snapshot.trackedTalkgroupId != snapshot.talkgroupId ||
        std::abs(snapshot.trackedVoiceHz - snapshot.voiceHz) > snapshot.voiceHzResetThreshold ||
        snapshot.trackedArmMs != snapshot.tunedAtMs;

    const int baseWrongSlotChecks = decision.resetTracking
        ? 0
        : std::max(0, snapshot.wrongSlotChecks);
    const int baseFlipCount = decision.resetTracking
        ? 0
        : std::max(0, snapshot.flipCount);
    const int64_t baseLastFlipMs = decision.resetTracking ? 0 : snapshot.lastFlipMs;

    decision.tdmaEpochLocked =
        snapshot.phase2SuperframeBursts >= 6 &&
        snapshot.phase2MaskedBursts >= 6;
    decision.noMacEssYet =
        snapshot.phase2MacCrcValid == 0 &&
        !snapshot.phase2EssKnown;

    decision.wrongSlotEligible =
        // Main/CLI callers deliberately downgrade the diagnostic to Decoding
        // whenever the selected slot produced PCM in the current window.  Only
        // a pure wrong-slot window with no decoded audio reaches this point.
        diagIs(snapshot.diag, P25FollowDiagCode::Phase2WrongSlot) &&
        snapshot.inPassband &&
        snapshot.phase2VoiceCodewords >= snapshot.minVoiceCodewords &&
        decision.noMacEssYet;

    decision.resetWrongSlot =
        !diagIs(snapshot.diag, P25FollowDiagCode::Phase2WrongSlot) ||
        snapshot.phase2MacPdus > 0 ||
        snapshot.phase2EssKnown;

    decision.wrongSlotChecksAfterObservation = baseWrongSlotChecks;
    if (decision.wrongSlotEligible) {
        ++decision.wrongSlotChecksAfterObservation;
    } else if (decision.resetWrongSlot) {
        decision.wrongSlotChecksAfterObservation = 0;
    }

    decision.flipCountAfterObservation = baseFlipCount;
    decision.probeRateOk =
        baseLastFlipMs == 0 ||
        snapshot.nowMs - baseLastFlipMs > snapshot.minFlipIntervalMs;
    decision.shouldFlip =
        decision.wrongSlotChecksAfterObservation >= snapshot.wrongSlotThreshold &&
        decision.flipCountAfterObservation < snapshot.maxFlips &&
        decision.probeRateOk;

    return decision;
}
