#include "P25FollowStateMachine.h"

#include <algorithm>
#include <cmath>

namespace {

bool diagIs(int diag, P25FollowDiagCode code)
{
    return diag == static_cast<int>(code);
}

} // namespace

P25FollowDecision evaluateP25Follow(const P25FollowSnapshot& snapshot)
{
    P25FollowDecision decision;
    decision.effectiveTalkgroupId = snapshot.talkgroupId != 0
        ? snapshot.talkgroupId
        : snapshot.fallbackTalkgroupId;

    decision.voiceStillLooksActive =
        snapshot.syncs > 0 ||
        snapshot.nids > 0 ||
        snapshot.imbeFrames > 0 ||
        snapshot.decodedFrames > 0 ||
        snapshot.phase2Bursts > 0 ||
        snapshot.phase2VoiceCodewords > 0;

    decision.encryptedOnVoice =
        diagIs(snapshot.diag, P25FollowDiagCode::SkippedEncrypted) ||
        (snapshot.phase2EssKnown && snapshot.phase2EssEncrypted);
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
    decision.activityGone =
        initialHoldExpired &&
        snapshot.nowMs - lastSignalMs > 1800 &&
        (diagIs(snapshot.diag, P25FollowDiagCode::NoSync) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NoLduVoice));

    decision.hardTimeout =
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > 30000 &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0;

    const bool noPhase2MacEssYet =
        snapshot.tunedAtMs > 0 &&
        snapshot.phase2MacCrcValid == 0 &&
        !snapshot.phase2EssKnown &&
        snapshot.decodedFrames == 0;

    decision.tdmaEpochLockedNoMacEss =
        noPhase2MacEssYet &&
        snapshot.phase2SuperframeBursts >= 6 &&
        snapshot.phase2MaskedBursts >= 6;

    decision.tdmaVcwNoSuperframeTimeout =
        noPhase2MacEssYet &&
        snapshot.nowMs - snapshot.tunedAtMs > 15000 &&
        snapshot.phase2VoiceCodewords >= 4 &&
        snapshot.phase2SuperframeBursts == 0 &&
        snapshot.phase2MaskedBursts == 0 &&
        (diagIs(snapshot.diag, P25FollowDiagCode::Phase2AudioLockMissing) ||
         diagIs(snapshot.diag, P25FollowDiagCode::Phase2LateEntryWaiting) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NidUnlocked) ||
         diagIs(snapshot.diag, P25FollowDiagCode::NoDecodedAudio));

    decision.tdmaNoProgressTimeout =
        (decision.tdmaEpochLockedNoMacEss &&
         snapshot.nowMs - snapshot.tunedAtMs > 18000) ||
        decision.tdmaVcwNoSuperframeTimeout;


    decision.tdmaNoVcwTimeout =
        snapshot.tunedAtMs > 0 &&
        snapshot.nowMs - snapshot.tunedAtMs > 9000 &&
        snapshot.decodedFrames == 0 &&
        snapshot.phase2VoiceCodewords == 0 &&
        (diagIs(snapshot.diag, P25FollowDiagCode::NoSync) ||
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
