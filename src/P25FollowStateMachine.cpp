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

    const bool diagnosticFresh =
        snapshot.diagUpdatedMs <= 0 ||
        snapshot.nowMs <= 0 ||
        snapshot.nowMs - snapshot.diagUpdatedMs <= 2500;

    const bool phase2Follow = snapshot.phase2Voice ||
        snapshot.phase2Bursts > 0 ||
        snapshot.phase2VoiceCodewords > 0 ||
        snapshot.phase2SuperframeBursts > 0 ||
        snapshot.phase2MaskedBursts > 0 ||
        snapshot.phase2MacPdus > 0 ||
        snapshot.phase2MacCrcValid > 0 ||
        snapshot.phase2EssKnown ||
        diagIsPhase2Specific(snapshot.diag);

    const bool phase2TrustedActivity =
        snapshot.decodedFrames > 0 ||
        snapshot.phase2MacCrcValid > 0 ||
        snapshot.phase2EssKnown ||
        (snapshot.phase2SuperframeBursts >= 10 && snapshot.phase2MaskedBursts >= 10);

    decision.voiceStillLooksActive = diagnosticFresh &&
        (snapshot.imbeFrames > 0 ||
         snapshot.decodedFrames > 0 ||
         // For Phase 1, sync/NID evidence is enough to keep following briefly.
         // For Phase 2 one-RTL traffic mode, partial repeated telemetry such as
         // p2sf=5/p2mask=5/p2vcw=6 is not a call-progress event in sdrtrunk; it
         // should not refresh the traffic-channel lifetime forever.
         ((snapshot.phase2Bursts == 0 && snapshot.phase2VoiceCodewords == 0) &&
          (snapshot.syncs > 0 || snapshot.nids > 0)) ||
         phase2TrustedActivity);

    const bool trustedEncryptedEss =
        snapshot.phase2EssKnown &&
        snapshot.phase2EssEncrypted &&
        snapshot.phase2MacCrcValid > 0;
    decision.encryptedOnVoice =
        diagIs(snapshot.diag, P25FollowDiagCode::SkippedEncrypted) ||
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

    decision.hardTimeout =
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > 20000 &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0;

    const bool noTrustedPhase2MacEssYet =
        phase2Follow &&
        snapshot.tunedAtMs > 0 &&
        snapshot.phase2MacCrcValid == 0 &&
        (!snapshot.phase2EssKnown || snapshot.phase2EssEncrypted) &&
        snapshot.decodedFrames == 0;

    decision.tdmaEpochLockedNoMacEss =
        noTrustedPhase2MacEssYet &&
        snapshot.phase2SuperframeBursts >= 4 &&
        snapshot.phase2MaskedBursts >= 4;

    decision.tdmaVcwNoSuperframeTimeout =
        noTrustedPhase2MacEssYet &&
        snapshot.nowMs - snapshot.tunedAtMs > 15000 &&
        silenceSinceSignalMs > 3500 &&
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
        snapshot.nowMs - snapshot.tunedAtMs > 4500 &&
        snapshot.phase2VoiceCodewords >= 4 &&
        snapshot.phase2SuperframeBursts > 0 &&
        snapshot.phase2MaskedBursts > 0 &&
        (snapshot.phase2SuperframeBursts < 10 || snapshot.phase2MaskedBursts < 10);

    decision.tdmaNoProgressTimeout =
        (decision.tdmaEpochLockedNoMacEss &&
         snapshot.nowMs - snapshot.tunedAtMs > 4500) ||
        decision.tdmaVcwNoSuperframeTimeout ||
        tdmaPartialEpochNoProgress;


    decision.tdmaNoVcwTimeout =
        phase2Follow &&
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > 9000 &&
        silenceSinceSignalMs > 3500 &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        snapshot.phase2Bursts == 0 &&
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
