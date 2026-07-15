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
    constexpr int64_t kSpeakerFollowGraceMs = 2500;
    const bool recentSpeakerOutput =
        snapshot.recentSpeakerOutputMs > 0 &&
        snapshot.nowMs > 0 &&
        snapshot.nowMs - snapshot.recentSpeakerOutputMs <= kSpeakerFollowGraceMs;
    bool hasCarrier = true;
    if (snapshot.rfMetricsPopulated) {
        hasCarrier = snapshot.recentSnrDb > 3.0 ||
                     (snapshot.recentSignalLevelDb > snapshot.recentNoiseFloorDb + 5.0);
    }
    decision.voiceStillLooksActive = diagnosticFresh &&
        (hasRecentVoiceVcws || snapshot.phase2TrafficCallActive ||
         (snapshot.phase2TrafficAudioOpen && snapshot.phase2VoiceCodewords > 0)) &&
        hasCarrier &&  // use actual RF energy (peak in BW vs noise floor) to ignore background noise as "real data"
        (snapshot.phase2TrafficAudioOpen ||
         snapshot.phase2TrafficCallActive ||
         // Only fall back to pure sync/nid for very brief P1-style or initial acquisition; not for sustained follow.
         (snapshot.phase2VoiceCodewords == 0 && snapshot.phase2Bursts == 0 &&
          (snapshot.syncs > 0 || snapshot.nids > 0) && (snapshot.nowMs - snapshot.tunedAtMs) < 3000));

    const bool trustedEncryptedEss =
        snapshot.phase2EssKnown &&
        snapshot.phase2EssEncrypted &&
        snapshot.phase2MacCrcValid > 0 &&
        (snapshot.currentCallSessionId == 0 ||
         snapshot.essCallSessionId == 0 ||
         snapshot.essCallSessionId == snapshot.currentCallSessionId);
    const bool grantProvesEncrypted =
        snapshot.grantEncryptionKnown && snapshot.grantEncrypted;
    const bool trustedTrafficEncrypted =
        snapshot.phase2TrafficEncrypted &&
        snapshot.phase2EssKnown &&
        snapshot.phase2EssEncrypted &&
        snapshot.phase2MacCrcValid > 0;
    decision.encryptedOnVoice =
        trustedEncryptedEss ||
        grantProvesEncrypted ||
        trustedTrafficEncrypted;
    if (decision.encryptedOnVoice) {
        decision.action = P25FollowAction::ReturnEncrypted;
        return decision;
    }

    if (!snapshot.autoActive) {
        return decision;
    }

    const int64_t effectiveLastActiveMs = snapshot.lastActiveMs;

    const bool initialHoldExpired =
        snapshot.tunedAtMs > 0 && snapshot.nowMs - snapshot.tunedAtMs > 2500;
    const int64_t lastSignalMs = std::max(effectiveLastActiveMs, snapshot.tunedAtMs);
    const int64_t silenceSinceSignalMs = snapshot.nowMs - lastSignalMs;
    const bool clearGrantKnown =
        snapshot.grantEncryptionKnown && !snapshot.grantEncrypted;
    const bool strongTrafficCarrier =
        snapshot.rfMetricsPopulated &&
        (snapshot.recentSnrDb > 6.0 ||
         snapshot.recentSignalLevelDb > snapshot.recentNoiseFloorDb + 8.0);
    const int64_t tunedDurationMs = snapshot.tunedAtMs > 0
        ? std::max<int64_t>(0, snapshot.nowMs - snapshot.tunedAtMs)
        : 0;
    const bool phase2HardProgress =
        snapshot.decodedFrames > 0 ||
        snapshot.phase2MacCrcValid > 0 ||
        snapshot.phase2EssKnown ||
        snapshot.phase2TrafficAudioOpen;
    const bool phase2CurrentVoiceEvidence =
        snapshot.phase2VoiceCodewords > 0 ||
        snapshot.imbeFrames > 0;
    const bool phase2CurrentStructureEvidence =
        snapshot.phase2Bursts > 0 ||
        snapshot.phase2SuperframeBursts > 0 ||
        snapshot.phase2MaskedBursts > 0 ||
        snapshot.phase2MacPdus > 0;
    const bool phase2UntrustedClearAcquire =
        clearGrantKnown &&
        !phase2HardProgress &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2MacCrcValid == 0 &&
        !snapshot.phase2EssKnown;
    const int64_t untrustedClearAcquireLimitMs = phase2CurrentVoiceEvidence
        ? 18000
        : (phase2CurrentStructureEvidence ? 12000 : 6500);
    const int64_t phase2AcquireLimitMs = phase2UntrustedClearAcquire
        ? untrustedClearAcquireLimitMs
        : (clearGrantKnown ? 45000 : 30000);
    const bool phase2AcquisitionProgress =
        phase2Follow &&
        (phase2HardProgress ||
         phase2CurrentVoiceEvidence ||
         phase2CurrentStructureEvidence ||
         snapshot.phase2TrafficCallActive ||
         diagIs(snapshot.diag, P25FollowDiagCode::Phase2AudioLockMissing) ||
         diagIs(snapshot.diag, P25FollowDiagCode::Phase2LateEntryWaiting) ||
         diagIs(snapshot.diag, P25FollowDiagCode::WaitingForClearGrant) ||
         diagIs(snapshot.diag, P25FollowDiagCode::Phase2MaskAppliedNoMacCrc) ||
         (clearGrantKnown &&
           tunedDurationMs < untrustedClearAcquireLimitMs &&
           (diagIs(snapshot.diag, P25FollowDiagCode::NoSync) ||
            diagIs(snapshot.diag, P25FollowDiagCode::Idle))));
    const bool phase2StillAcquiring =
        phase2Follow &&
        snapshot.decodedFrames == 0 &&
        tunedDurationMs < phase2AcquireLimitMs &&
        (phase2AcquisitionProgress ||
         snapshot.phase2TrafficCallActive ||
         (clearGrantKnown &&
          strongTrafficCarrier &&
          tunedDurationMs < untrustedClearAcquireLimitMs));
    const int64_t activitySilenceLimitMs =
        phase2StillAcquiring ? 30000 : 3500;
    decision.activityGone =
        !recentSpeakerOutput &&
        !phase2StillAcquiring &&
        initialHoldExpired &&
        silenceSinceSignalMs > activitySilenceLimitMs &&
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

    const int64_t continuationAnchorMs = snapshot.lastActiveMs;
    const bool phase2RecentContinuation =
        phase2Follow &&
        continuationAnchorMs > snapshot.tunedAtMs &&
        snapshot.nowMs > continuationAnchorMs &&
        !snapshot.phase2EssEncrypted;
    const int64_t tdmaVcwNoSuperframeTunedMs = phase2RecentContinuation ? 22000 : 15000;
    const int64_t tdmaVcwNoSuperframeSilenceMs = phase2RecentContinuation ? 10000 : 3500;
    const bool waitingUnknownClearGrant =
        !snapshot.grantEncryptionKnown &&
        diagIs(snapshot.diag, P25FollowDiagCode::WaitingForClearGrant);
    const bool wrongSlotNoTargetVcw =
        snapshot.phase2VoiceCodewords == 0 &&
        snapshot.phase2OppositeVoiceCodewords > 0 &&
        snapshot.phase2Bursts > 0;
    const int64_t tdmaNoVcwTunedMs = waitingUnknownClearGrant
        ? 45000
        : (phase2UntrustedClearAcquire
            ? untrustedClearAcquireLimitMs
            : (clearGrantKnown && snapshot.phase2TrafficCallActive
            ? 60000
            : (clearGrantKnown
                ? 45000
                : (wrongSlotNoTargetVcw ? 30000 : (phase2RecentContinuation ? 18000 : 9000)))));
    const int64_t tdmaNoVcwSilenceMs = waitingUnknownClearGrant
        ? 40000
        : (phase2UntrustedClearAcquire
            ? (phase2CurrentStructureEvidence ? 2500 : 1200)
            : (clearGrantKnown && snapshot.phase2TrafficCallActive
            ? 45000
            : (clearGrantKnown
                ? 30000
                : (wrongSlotNoTargetVcw ? 25000 : (phase2RecentContinuation ? 10000 : 3500)))));
    const int64_t tdmaNoMacEssTunedMs = phase2UntrustedClearAcquire
        ? untrustedClearAcquireLimitMs
        : (phase2RecentContinuation ? 30000 : 15000);
    const int64_t tdmaNoMacEssSilenceMs = phase2UntrustedClearAcquire
        ? (phase2CurrentVoiceEvidence ? 3500 : 2500)
        : (phase2RecentContinuation ? 10000 : 3500);

    const bool firstDiagnosticGraceExpired =
        phase2Follow &&
        !hasPublishedVoiceDiagnostic &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > 30000;

    const int64_t hardTimeoutTuneMs = phase2Follow ? 45000 : 12000;
    const int64_t hardTimeoutSilenceMs = phase2Follow ? 12000 : 3000;
    decision.hardTimeout =
        snapshot.tunedAtMs > 0 &&
        (hasPublishedVoiceDiagnostic || firstDiagnosticGraceExpired) &&
        snapshot.nowMs - snapshot.tunedAtMs > hardTimeoutTuneMs &&
        silenceSinceSignalMs > hardTimeoutSilenceMs &&
        !phase2AcquisitionProgress &&
        !snapshot.phase2TrafficCallActive &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        !waitingUnknownClearGrant &&
        !wrongSlotNoTargetVcw;

    bool hasCarrierForDrop = true;
    if (snapshot.rfMetricsPopulated) {
        hasCarrierForDrop = snapshot.recentSnrDb > 3.0 ||
                            (snapshot.recentSignalLevelDb > snapshot.recentNoiseFloorDb + 5.0);
    }
    decision.carrierDropped = initialHoldExpired &&
        !hasCarrierForDrop &&
        !phase2StillAcquiring &&
        silenceSinceSignalMs > 800;  // quick return once carrier drops after voice ends

    decision.tdmaVcwNoSuperframeTimeout =
        !phase2StillAcquiring &&
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
        !phase2StillAcquiring &&
        noTrustedPhase2MacEssYet &&
        snapshot.nowMs - snapshot.tunedAtMs > tdmaNoMacEssTunedMs &&
        silenceSinceSignalMs > tdmaNoMacEssSilenceMs &&
        snapshot.phase2VoiceCodewords >= 4 &&
        snapshot.phase2SuperframeBursts > 0 &&
        snapshot.phase2MaskedBursts > 0 &&
        (snapshot.phase2SuperframeBursts < 10 || snapshot.phase2MaskedBursts < 10);

    decision.tdmaNoProgressTimeout =
        (!phase2StillAcquiring &&
         decision.tdmaEpochLockedNoMacEss &&
         snapshot.nowMs - snapshot.tunedAtMs > tdmaNoMacEssTunedMs &&
         silenceSinceSignalMs > tdmaNoMacEssSilenceMs) ||
        decision.tdmaVcwNoSuperframeTimeout ||
        tdmaPartialEpochNoProgress;


    const bool tdmaNoAcquireIdleTimeout =
        phase2Follow &&
        hasPublishedVoiceDiagnostic &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > (waitingUnknownClearGrant ? 12000 : 4500) &&
        silenceSinceSignalMs > (waitingUnknownClearGrant ? 4000 : 1200) &&
        !snapshot.phase2TrafficCallActive &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        diagIs(snapshot.diag, P25FollowDiagCode::Idle);

    const bool clearGrantCarrierAcquireHold =
        clearGrantKnown &&
        strongTrafficCarrier &&
        snapshot.tunedAtMs > 0 &&
        tunedDurationMs <= (phase2UntrustedClearAcquire ? untrustedClearAcquireLimitMs : 45000);

    decision.tdmaNoVcwTimeout =
        !phase2StillAcquiring &&
        phase2Follow &&
        hasPublishedVoiceDiagnostic &&
        snapshot.tunedAtMs > 0 &&
        (snapshot.nowMs - snapshot.tunedAtMs > tdmaNoVcwTunedMs ||
         tdmaNoAcquireIdleTimeout) &&
        silenceSinceSignalMs > tdmaNoVcwSilenceMs &&
        !snapshot.phase2TrafficCallActive &&
        !clearGrantCarrierAcquireHold &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        !wrongSlotNoTargetVcw &&
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
    decision.selectedSlotAudioHold =
        snapshot.recentSelectedSlotAudio ||
        (snapshot.lastSelectedSlotAudioMs > 0 &&
         snapshot.nowMs >= snapshot.lastSelectedSlotAudioMs &&
         snapshot.nowMs - snapshot.lastSelectedSlotAudioMs <=
             std::max<int64_t>(0, snapshot.selectedSlotAudioHoldMs));

    const int minVoiceCodewords = std::max(1, snapshot.minVoiceCodewords);
    const int unknownEpochVoiceCodewords =
        (snapshot.grantClearStateUnknown && decision.tdmaEpochLocked)
            ? std::min(minVoiceCodewords, 2)
            : minVoiceCodewords;
    const bool oppositeSlotDominates =
        snapshot.phase2TargetVoiceCodewords == 0 &&
        snapshot.phase2OppositeVoiceCodewords >= minVoiceCodewords &&
        snapshot.phase2OppositeVoiceCodewords >=
            snapshot.phase2TargetVoiceCodewords + minVoiceCodewords;

    decision.wrongSlotEligible =
        // Main/CLI callers deliberately downgrade the diagnostic to Decoding
        // whenever the selected slot produced PCM in the current window.  Only
        // a pure wrong-slot window with no decoded audio reaches this point.
        diagIs(snapshot.diag, P25FollowDiagCode::Phase2WrongSlot) &&
        snapshot.inPassband &&
        snapshot.phase2VoiceCodewords >= minVoiceCodewords &&
        oppositeSlotDominates &&
        decision.noMacEssYet &&
        !decision.selectedSlotAudioHold;

    decision.resetWrongSlot =
        !diagIs(snapshot.diag, P25FollowDiagCode::Phase2WrongSlot) ||
        snapshot.phase2MacPdus > 0 ||
        snapshot.phase2EssKnown ||
        decision.selectedSlotAudioHold;

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
    // SDRTrunk keeps the TDMA timeslot from the control-channel grant
    // authoritative.  A busy Phase-2 RF carrier can carry an unrelated call on
    // the opposite slot; unlabelled VCWs there are not proof that the grant slot
    // is wrong.  Only allow this recovery for manual/late-entry follows that do
    // not yet have enough control-channel mask/grant metadata to trust a slot.
    const bool untrustedSlotAssignment = !snapshot.grantMaskParamsKnown;
    const bool maskedOppositeDominant =
        snapshot.grantClearStateUnknown &&
        untrustedSlotAssignment &&
        snapshot.inPassband &&
        snapshot.phase2TargetVoiceCodewords == 0 &&
        snapshot.phase2OppositeVoiceCodewords >= unknownEpochVoiceCodewords &&
        snapshot.phase2MaskedBursts >= 1 &&
        decision.noMacEssYet &&
        !decision.selectedSlotAudioHold;
    decision.maskedOppositeDominantFlip =
        maskedOppositeDominant &&
        decision.flipCountAfterObservation < snapshot.maxFlips &&
        decision.probeRateOk;

    const bool sustainedWrongSlot =
        decision.wrongSlotEligible &&
        decision.wrongSlotChecksAfterObservation >= snapshot.wrongSlotThreshold;
    decision.shouldFlip =
        (sustainedWrongSlot || decision.maskedOppositeDominantFlip) &&
        decision.flipCountAfterObservation < snapshot.maxFlips &&
        decision.probeRateOk;

    const bool earlyOppositeVcw =
        snapshot.grantClearStateUnknown &&
        untrustedSlotAssignment &&
        snapshot.inPassband &&
        snapshot.phase2TargetVoiceCodewords == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        snapshot.phase2OppositeVoiceCodewords >= unknownEpochVoiceCodewords &&
        decision.noMacEssYet &&
        !decision.selectedSlotAudioHold;
    const bool clearGrantBurstNoTargetVcw =
        snapshot.grantClearKnown &&
        untrustedSlotAssignment &&
        snapshot.inPassband &&
        snapshot.phase2TargetVoiceCodewords == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        snapshot.phase2Bursts > 0 &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs >= 6000 &&
        decision.noMacEssYet &&
        !decision.selectedSlotAudioHold;
    const bool unknownGrantAggregateVcw =
        snapshot.grantClearStateUnknown &&
        snapshot.inPassband &&
        !snapshot.grantMaskParamsKnown &&
        snapshot.phase2TargetVoiceCodewords == 0 &&
        snapshot.phase2VoiceCodewords >= unknownEpochVoiceCodewords &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs >= (decision.tdmaEpochLocked ? 2200 : 4000) &&
        decision.noMacEssYet &&
        !decision.selectedSlotAudioHold;
    const bool earlyNoSync =
        snapshot.grantClearStateUnknown &&
        !snapshot.grantMaskParamsKnown &&
        snapshot.inPassband &&
        snapshot.phase2TargetVoiceCodewords == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        snapshot.phase2OppositeVoiceCodewords == 0 &&
        snapshot.phase2Bursts == 0 &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs >= snapshot.earlyNoSyncFlipMs &&
        (diagIs(snapshot.diag, P25FollowDiagCode::WaitingForClearGrant) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NoSync) ||
         diagIs(snapshot.diag, P25FollowDiagCode::Phase2AudioLockMissing)) &&
        !decision.selectedSlotAudioHold;
    decision.earlyNoSyncFlip =
        (earlyOppositeVcw || earlyNoSync || clearGrantBurstNoTargetVcw || unknownGrantAggregateVcw) &&
        decision.flipCountAfterObservation == 0 &&
        decision.probeRateOk &&
        !decision.maskedOppositeDominantFlip;

    return decision;
}
