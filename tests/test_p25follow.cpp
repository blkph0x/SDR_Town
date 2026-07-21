#include <catch2/catch_all.hpp>

#include "P25FollowStateMachine.h"
#include "P25ReceiverSession.h"

namespace {

constexpr int diag(P25FollowDiagCode code)
{
    return static_cast<int>(code);
}

} // namespace

TEST_CASE("P25 follow returns immediately when a voice channel proves encrypted", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.nowMs = 10'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.talkgroupId = 101;
    snapshot.diag = diag(P25FollowDiagCode::SkippedEncrypted);
    snapshot.grantEncryptionKnown = true;
    snapshot.grantEncrypted = true;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.action == P25FollowAction::ReturnEncrypted);
    REQUIRE(decision.encryptedOnVoice);
    REQUIRE(decision.effectiveTalkgroupId == 101);
}

TEST_CASE("P25 follow does not bail on SkippedEncrypted when grant security is unknown", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 2'500;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 2'500;
    snapshot.talkgroupId = 12068;
    snapshot.diag = diag(P25FollowDiagCode::SkippedEncrypted);
    snapshot.grantEncryptionKnown = false;
    snapshot.grantEncrypted = false;
    snapshot.phase2EssKnown = false;
    snapshot.phase2TrafficEncrypted = false;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.encryptedOnVoice);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 follow does not treat encrypted ESS without MAC CRC as final proof", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.nowMs = 20'500;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 20'500;
    snapshot.talkgroupId = 102;
    snapshot.diag = diag(P25FollowDiagCode::Phase2MaskAppliedNoMacCrc);
    snapshot.phase2SuperframeBursts = 6;
    snapshot.phase2MaskedBursts = 6;
    snapshot.phase2EssKnown = true;
    snapshot.phase2EssEncrypted = true;
    snapshot.phase2MacCrcValid = 0;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.encryptedOnVoice);
    REQUIRE(decision.tdmaEpochLockedNoMacEss);
    REQUIRE_FALSE(decision.tdmaNoProgressTimeout);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 follow holds Phase 2 acquisition before declaring activity gone", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 8'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diag = diag(P25FollowDiagCode::NoSync);
    snapshot.decodedFrames = 0;
    snapshot.phase2Bursts = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2TrafficProcessorActive = true;
    snapshot.phase2TrafficCallActive = true;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.activityGone);
    REQUIRE(decision.action == P25FollowAction::None);

    snapshot.nowMs = 17'000;
    const auto midDecision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(midDecision.activityGone);
    REQUIRE(midDecision.action == P25FollowAction::None);

    snapshot.nowMs = 25'000;
    const auto lateDecision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(lateDecision.activityGone);
    REQUIRE(lateDecision.action == P25FollowAction::None);

    snapshot.nowMs = 50'000;
    const auto timeoutDecision = evaluateP25Follow(snapshot);
    REQUIRE(timeoutDecision.activityGone);
    REQUIRE(timeoutDecision.action == P25FollowAction::ReturnActivityGone);
}

TEST_CASE("P25 follow holds briefly after tune before declaring a quiet voice channel ended", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.nowMs = 3'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diag = diag(P25FollowDiagCode::NoSync);

    REQUIRE(evaluateP25Follow(snapshot).action == P25FollowAction::None);

    snapshot.nowMs = 5'400;
    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.activityGone);
    REQUIRE(decision.action == P25FollowAction::ReturnActivityGone);
}

TEST_CASE("P25 follow uses explicit TDMA watchdog actions", "[p25][follow]")
{
    P25FollowSnapshot noMacEss;
    noMacEss.autoActive = true;
    noMacEss.phase2Voice = true;
    noMacEss.nowMs = 20'500;
    noMacEss.tunedAtMs = 1'000;
    noMacEss.lastActiveMs = 20'500;
    noMacEss.diag = diag(P25FollowDiagCode::Phase2MaskAppliedNoMacCrc);
    noMacEss.phase2SuperframeBursts = 6;
    noMacEss.phase2MaskedBursts = 6;
    noMacEss.phase2MacCrcValid = 0;
    noMacEss.phase2EssKnown = false;

    auto decision = evaluateP25Follow(noMacEss);
    REQUIRE(decision.tdmaEpochLockedNoMacEss);
    REQUIRE_FALSE(decision.tdmaNoProgressTimeout);
    REQUIRE(decision.action == P25FollowAction::None);

    noMacEss.nowMs = 22'500;
    noMacEss.lastActiveMs = 1'000;
    decision = evaluateP25Follow(noMacEss);
    REQUIRE_FALSE(decision.tdmaNoProgressTimeout);
    REQUIRE(decision.action == P25FollowAction::None);

    noMacEss.nowMs = 50'000;
    noMacEss.lastActiveMs = 1'000;
    decision = evaluateP25Follow(noMacEss);
    REQUIRE(decision.tdmaEpochLockedNoMacEss);
    REQUIRE(decision.tdmaNoProgressTimeout);
    REQUIRE(decision.action == P25FollowAction::ReturnNoMacEss);

    P25FollowSnapshot vcwNoSuperframe;
    vcwNoSuperframe.autoActive = true;
    vcwNoSuperframe.phase2Voice = true;
    vcwNoSuperframe.nowMs = 17'500;
    vcwNoSuperframe.tunedAtMs = 1'000;
    vcwNoSuperframe.lastActiveMs = 1'000;
    vcwNoSuperframe.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    vcwNoSuperframe.phase2VoiceCodewords = 4;
    vcwNoSuperframe.phase2SuperframeBursts = 0;
    vcwNoSuperframe.phase2MaskedBursts = 0;
    vcwNoSuperframe.phase2MacCrcValid = 0;
    vcwNoSuperframe.phase2EssKnown = false;

    decision = evaluateP25Follow(vcwNoSuperframe);
    REQUIRE_FALSE(decision.tdmaVcwNoSuperframeTimeout);
    REQUIRE(decision.action == P25FollowAction::None);

    vcwNoSuperframe.nowMs = 50'000;
    vcwNoSuperframe.lastActiveMs = 1'000;
    decision = evaluateP25Follow(vcwNoSuperframe);
    REQUIRE(decision.tdmaVcwNoSuperframeTimeout);
    REQUIRE(decision.tdmaNoProgressTimeout);
    REQUIRE(decision.action == P25FollowAction::ReturnNoMacEss);

    P25FollowSnapshot noVcw;
    noVcw.autoActive = true;
    noVcw.phase2Voice = true;
    noVcw.nowMs = 11'000;
    noVcw.tunedAtMs = 1'000;
    noVcw.lastActiveMs = 1'000;
    noVcw.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    noVcw.decodedFrames = 0;
    noVcw.phase2VoiceCodewords = 0;

    decision = evaluateP25Follow(noVcw);
    REQUIRE_FALSE(decision.tdmaNoVcwTimeout);
    REQUIRE(decision.action == P25FollowAction::None);

    noVcw.nowMs = 50'000;
    noVcw.lastActiveMs = 1'000;
    decision = evaluateP25Follow(noVcw);
    REQUIRE(decision.tdmaNoVcwTimeout);
    REQUIRE(decision.action == P25FollowAction::ReturnNoVoiceCodewords);
}

TEST_CASE("P25 follow returns quickly from untrusted clear Phase 2 acquire", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 17'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diagUpdatedMs = 17'000;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.grantEncryptionKnown = true;
    snapshot.grantEncrypted = false;
    snapshot.phase2TrafficProcessorActive = true;
    snapshot.phase2TrafficCallActive = true;
    snapshot.phase2Bursts = 26;
    snapshot.phase2SuperframeBursts = 24;
    snapshot.phase2MaskedBursts = 24;
    snapshot.phase2MacPdus = 24;
    snapshot.phase2MacCrcValid = 0;
    snapshot.phase2EssKnown = false;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.decodedFrames = 0;
    snapshot.rfMetricsPopulated = true;
    snapshot.recentSnrDb = 18.0;
    snapshot.recentSignalLevelDb = -40.0;
    snapshot.recentNoiseFloorDb = -70.0;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.tdmaNoProgressTimeout);
    REQUIRE(decision.action == P25FollowAction::ReturnNoMacEss);
}

TEST_CASE("P25 follow holds Phase 2 traffic after recent activity while reacquiring timing", "[p25][follow]")
{
    P25FollowSnapshot reacquiring;
    reacquiring.autoActive = true;
    reacquiring.phase2Voice = true;
    reacquiring.nowMs = 17'500;
    reacquiring.tunedAtMs = 1'000;
    reacquiring.lastActiveMs = 13'000;
    reacquiring.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    reacquiring.phase2VoiceCodewords = 4;
    reacquiring.phase2SuperframeBursts = 0;
    reacquiring.phase2MaskedBursts = 0;
    reacquiring.phase2MacCrcValid = 0;
    reacquiring.phase2EssKnown = false;

    auto decision = evaluateP25Follow(reacquiring);
    REQUIRE_FALSE(decision.tdmaVcwNoSuperframeTimeout);
    REQUIRE(decision.action == P25FollowAction::None);

    reacquiring.nowMs = 26'000;
    decision = evaluateP25Follow(reacquiring);
    REQUIRE_FALSE(decision.tdmaVcwNoSuperframeTimeout);
    REQUIRE(decision.action == P25FollowAction::None);

    reacquiring.nowMs = 50'000;
    reacquiring.lastActiveMs = 1'000;
    decision = evaluateP25Follow(reacquiring);
    REQUIRE(decision.tdmaVcwNoSuperframeTimeout);
    REQUIRE(decision.action == P25FollowAction::ReturnNoMacEss);
}

TEST_CASE("P25 follow does not call idle unpublished diagnostics a no-VCW voice failure", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 12'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diagUpdatedMs = 0;
    snapshot.diag = diag(P25FollowDiagCode::Idle);
    snapshot.decodedFrames = 0;
    snapshot.phase2Bursts = 0;
    snapshot.phase2VoiceCodewords = 0;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.tdmaNoVcwTimeout);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 follow trusts the persistent traffic processor for call lifetime", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 26'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2Bursts = 0;
    snapshot.phase2TrafficProcessorActive = true;
    snapshot.phase2TrafficCallActive = true;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.voiceStillLooksActive);
    REQUIRE_FALSE(decision.hardTimeout);
    REQUIRE_FALSE(decision.tdmaNoVcwTimeout);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 follow keeps Phase 2 audio open while MAC or ESS catches up", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 8'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diag = diag(P25FollowDiagCode::Phase2LateEntryWaiting);
    snapshot.phase2VoiceCodewords = 8;
    snapshot.phase2SuperframeBursts = 6;
    snapshot.phase2MaskedBursts = 6;
    snapshot.phase2MacCrcValid = 0;
    snapshot.phase2EssKnown = false;
    snapshot.phase2TrafficProcessorActive = true;
    snapshot.phase2TrafficAudioOpen = true;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.voiceStillLooksActive);
    REQUIRE_FALSE(decision.tdmaEpochLockedNoMacEss);
    REQUIRE_FALSE(decision.tdmaNoProgressTimeout);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 follow returns when the traffic processor proves encryption", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 8'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.talkgroupId = 505;
    snapshot.diag = diag(P25FollowDiagCode::Phase2LateEntryWaiting);
    snapshot.phase2TrafficProcessorActive = true;
    snapshot.phase2TrafficEncrypted = true;
    snapshot.phase2EssKnown = true;
    snapshot.phase2EssEncrypted = true;
    snapshot.phase2MacCrcValid = 1;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.encryptedOnVoice);
    REQUIRE(decision.action == P25FollowAction::ReturnEncrypted);
    REQUIRE(decision.effectiveTalkgroupId == 505);
}

TEST_CASE("P25 follow ignores traffic processor encrypted flag without trusted ESS", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 8'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.talkgroupId = 506;
    snapshot.diag = diag(P25FollowDiagCode::Phase2LateEntryWaiting);
    snapshot.phase2TrafficProcessorActive = true;
    snapshot.phase2TrafficEncrypted = true;
    snapshot.phase2EssKnown = false;
    snapshot.phase2MacCrcValid = 0;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.encryptedOnVoice);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 slot probe flips on repeated wrong-slot VCWs while MAC/ESS is absent", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 10'000;
    snapshot.tunedAtMs = 2'000;
    snapshot.trackedArmMs = 2'000;
    snapshot.talkgroupId = 202;
    snapshot.trackedTalkgroupId = 202;
    snapshot.voiceHz = 420'250'000.0;
    snapshot.trackedVoiceHz = 420'250'000.0;
    snapshot.inPassband = true;
    snapshot.diag = diag(P25FollowDiagCode::Phase2WrongSlot);
    snapshot.phase2VoiceCodewords = 4;
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 4;
    snapshot.phase2SuperframeBursts = 6;
    snapshot.phase2MaskedBursts = 6;
    snapshot.phase2MacCrcValid = 0;
    snapshot.phase2EssKnown = false;
    snapshot.wrongSlotChecks = 2;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.wrongSlotEligible);
    REQUIRE(decision.noMacEssYet);
    REQUIRE(decision.wrongSlotChecksAfterObservation == 3);
    REQUIRE(decision.shouldFlip);
}

TEST_CASE("P25 slot probe resets tracking on new grants and refuses proven MAC or ESS", "[p25][follow]")
{
    P25SlotProbeSnapshot changed;
    changed.nowMs = 20'000;
    changed.tunedAtMs = 5'000;
    changed.trackedArmMs = 2'000;
    changed.talkgroupId = 303;
    changed.trackedTalkgroupId = 202;
    changed.voiceHz = 420'350'000.0;
    changed.trackedVoiceHz = 420'250'000.0;
    changed.wrongSlotChecks = 3;
    changed.flipCount = 2;
    changed.lastFlipMs = 18'000;
    changed.inPassband = true;
    changed.diag = diag(P25FollowDiagCode::Phase2WrongSlot);
    changed.phase2VoiceCodewords = 4;
    changed.phase2TargetVoiceCodewords = 0;
    changed.phase2OppositeVoiceCodewords = 4;

    auto decision = evaluateP25SlotProbe(changed);
    REQUIRE(decision.resetTracking);
    REQUIRE(decision.wrongSlotChecksAfterObservation == 1);
    REQUIRE(decision.flipCountAfterObservation == 0);
    REQUIRE_FALSE(decision.shouldFlip);

    changed.trackedTalkgroupId = changed.talkgroupId;
    changed.trackedVoiceHz = changed.voiceHz;
    changed.trackedArmMs = changed.tunedAtMs;
    changed.wrongSlotChecks = 2;
    changed.lastFlipMs = 0;
    decision = evaluateP25SlotProbe(changed);
    REQUIRE(decision.wrongSlotChecksAfterObservation == 3);
    REQUIRE(decision.shouldFlip);

    P25SlotProbeSnapshot proven = changed;
    proven.trackedTalkgroupId = proven.talkgroupId;
    proven.trackedVoiceHz = proven.voiceHz;
    proven.trackedArmMs = proven.tunedAtMs;
    proven.wrongSlotChecks = 2;
    proven.flipCount = 0;
    proven.phase2MacPdus = 1;
    proven.phase2MacCrcValid = 1;

    decision = evaluateP25SlotProbe(proven);
    REQUIRE_FALSE(decision.wrongSlotEligible);
    REQUIRE(decision.resetWrongSlot);
    REQUIRE(decision.wrongSlotChecksAfterObservation == 0);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 follow ignores encrypted ESS from a prior call session", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.nowMs = 8'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.talkgroupId = 606;
    snapshot.currentCallSessionId = 0x0000012C00000064ull;
    snapshot.essCallSessionId = 0x000000C800000032ull;
    snapshot.diag = diag(P25FollowDiagCode::Phase2LateEntryWaiting);
    snapshot.phase2EssKnown = true;
    snapshot.phase2EssEncrypted = true;
    snapshot.phase2MacCrcValid = 1;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.encryptedOnVoice);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 slot probe is rate limited and bounded", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 10'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 404;
    snapshot.trackedTalkgroupId = 404;
    snapshot.voiceHz = 420'250'000.0;
    snapshot.trackedVoiceHz = 420'250'000.0;
    snapshot.inPassband = true;
    snapshot.diag = diag(P25FollowDiagCode::Phase2WrongSlot);
    snapshot.phase2VoiceCodewords = 8;
    snapshot.lastFlipMs = 8'000;
    snapshot.wrongSlotChecks = 1;
    snapshot.flipCount = 1;

    REQUIRE_FALSE(evaluateP25SlotProbe(snapshot).shouldFlip);

    snapshot.lastFlipMs = 0;
    snapshot.flipCount = snapshot.maxFlips;
    REQUIRE_FALSE(evaluateP25SlotProbe(snapshot).shouldFlip);
}

TEST_CASE("P25 follow returns quickly from quiet known clear-grant acquisition", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 5'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diagUpdatedMs = 4'500;
    snapshot.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    snapshot.grantEncryptionKnown = true;
    snapshot.grantEncrypted = false;
    snapshot.rfMetricsPopulated = true;
    snapshot.recentSnrDb = 20.0;
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2Bursts = 0;

    const auto midDecision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(midDecision.tdmaNoVcwTimeout);
    REQUIRE(midDecision.action == P25FollowAction::None);

    snapshot.nowMs = 20'000;
    snapshot.diagUpdatedMs = 19'500;
    snapshot.lastActiveMs = 1'000;
    const auto lateDecision = evaluateP25Follow(snapshot);
    REQUIRE(lateDecision.tdmaNoVcwTimeout);
    REQUIRE(lateDecision.action == P25FollowAction::ReturnNoVoiceCodewords);
}

TEST_CASE("P25 follow holds active traffic processor call during clear-grant no-VCW acquisition", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 40'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diagUpdatedMs = 39'500;
    snapshot.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    snapshot.grantEncryptionKnown = true;
    snapshot.grantEncrypted = false;
    snapshot.phase2TrafficProcessorActive = true;
    snapshot.phase2TrafficCallActive = true;
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.tdmaNoVcwTimeout);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 follow holds unknown clear-grant acquisition longer before no-VCW return", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 20'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diagUpdatedMs = 19'500;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.grantEncryptionKnown = false;
    snapshot.grantEncrypted = false;
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2Bursts = 0;

    const auto midDecision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(midDecision.tdmaNoVcwTimeout);
    REQUIRE(midDecision.action == P25FollowAction::None);

    snapshot.nowMs = 46'500;
    snapshot.lastActiveMs = 1'000;
    const auto lateDecision = evaluateP25Follow(snapshot);
    REQUIRE(lateDecision.tdmaNoVcwTimeout);
    REQUIRE(lateDecision.action == P25FollowAction::ReturnNoVoiceCodewords);
}

TEST_CASE("P25 follow holds when opposite-slot VCWs suggest wrong grant slot", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 20'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diagUpdatedMs = 19'500;
    snapshot.diag = diag(P25FollowDiagCode::Phase2WrongSlot);
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 6;
    snapshot.phase2Bursts = 4;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.tdmaNoVcwTimeout);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 follow does not treat recent speaker output as live voice by itself", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 10'000;
    snapshot.tunedAtMs = 2'000;
    snapshot.lastActiveMs = 2'000;
    snapshot.recentSpeakerOutputMs = 9'900;
    snapshot.diagUpdatedMs = 9'900;
    snapshot.diag = diag(P25FollowDiagCode::NoSync);
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2TrafficProcessorActive = true;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.activityGone);
    REQUIRE_FALSE(decision.voiceStillLooksActive);
    REQUIRE(decision.action == P25FollowAction::None);

    snapshot.nowMs = 20'500;
    snapshot.recentSpeakerOutputMs = 2'000;
    const auto lateDecision = evaluateP25Follow(snapshot);
    REQUIRE(lateDecision.activityGone);
    REQUIRE(lateDecision.action == P25FollowAction::ReturnNoVoiceCodewords);

    snapshot.nowMs = 50'500;
    snapshot.recentSpeakerOutputMs = 50'400;
    const auto expiredDecision = evaluateP25Follow(snapshot);
    REQUIRE(expiredDecision.action != P25FollowAction::None);
    REQUIRE((expiredDecision.action == P25FollowAction::ReturnActivityGone ||
             expiredDecision.action == P25FollowAction::ReturnNoVoiceCodewords ||
             expiredDecision.action == P25FollowAction::ReturnHardTimeout));
}

TEST_CASE("P25 slot probe flips once on long unknown clear-grant no-sync acquisition", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 16'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 20202;
    snapshot.trackedTalkgroupId = 20202;
    snapshot.voiceHz = 417'675'000.0;
    snapshot.trackedVoiceHz = 417'675'000.0;
    snapshot.inPassband = true;
    snapshot.grantClearStateUnknown = true;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.phase2Bursts = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 0;
    snapshot.earlyNoSyncFlipMs = 15'000;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.earlyNoSyncFlip);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 slot probe does not early no-sync flip when grant mask params are known", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 16'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 30302;
    snapshot.trackedTalkgroupId = 30302;
    snapshot.voiceHz = 421'975'000.0;
    snapshot.trackedVoiceHz = 421'975'000.0;
    snapshot.inPassband = true;
    snapshot.grantClearStateUnknown = true;
    snapshot.grantMaskParamsKnown = true;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.phase2Bursts = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 0;
    snapshot.earlyNoSyncFlipMs = 15'000;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE_FALSE(decision.earlyNoSyncFlip);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 slot probe refuses aggregate-only VCWs when mask params are known", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 6'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.inPassband = true;
    snapshot.grantClearKnown = false;
    snapshot.grantClearStateUnknown = true;
    snapshot.grantMaskParamsKnown = true;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.phase2Bursts = 2;
    snapshot.phase2VoiceCodewords = 6;
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 0;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE_FALSE(decision.earlyNoSyncFlip);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 slot probe refuses aggregate wrong-slot flip when target VCWs are present", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 12'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 30302;
    snapshot.trackedTalkgroupId = 30302;
    snapshot.voiceHz = 421'850'000.0;
    snapshot.trackedVoiceHz = 421'850'000.0;
    snapshot.inPassband = true;
    snapshot.diag = diag(P25FollowDiagCode::Phase2WrongSlot);
    snapshot.phase2VoiceCodewords = 12;
    snapshot.phase2TargetVoiceCodewords = 8;
    snapshot.phase2OppositeVoiceCodewords = 4;
    snapshot.phase2MaskedBursts = 6;
    snapshot.phase2SuperframeBursts = 6;
    snapshot.wrongSlotChecks = 3;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE_FALSE(decision.wrongSlotEligible);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 slot probe holds selected slot after fresh decoded audio", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 18'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 12068;
    snapshot.trackedTalkgroupId = 12068;
    snapshot.voiceHz = 420'600'000.0;
    snapshot.trackedVoiceHz = 420'600'000.0;
    snapshot.inPassband = true;
    snapshot.grantClearStateUnknown = true;
    snapshot.diag = diag(P25FollowDiagCode::Phase2WrongSlot);
    snapshot.phase2VoiceCodewords = 4;
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 4;
    snapshot.phase2MaskedBursts = 12;
    snapshot.phase2SuperframeBursts = 12;
    snapshot.wrongSlotChecks = 3;
    snapshot.recentSelectedSlotAudio = true;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.selectedSlotAudioHold);
    REQUIRE_FALSE(decision.wrongSlotEligible);
    REQUIRE(decision.resetWrongSlot);
    REQUIRE(decision.wrongSlotChecksAfterObservation == 0);
    REQUIRE_FALSE(decision.maskedOppositeDominantFlip);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 slot probe holds selected slot for a short time after decoded audio", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 23'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 12068;
    snapshot.trackedTalkgroupId = 12068;
    snapshot.voiceHz = 420'600'000.0;
    snapshot.trackedVoiceHz = 420'600'000.0;
    snapshot.inPassband = true;
    snapshot.grantClearStateUnknown = true;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 4;
    snapshot.phase2MaskedBursts = 1;
    snapshot.phase2SuperframeBursts = 1;
    snapshot.lastSelectedSlotAudioMs = 18'000;
    snapshot.selectedSlotAudioHoldMs = 12'000;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.selectedSlotAudioHold);
    REQUIRE_FALSE(decision.maskedOppositeDominantFlip);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 follow does not return on brief carrier dip during clear-grant acquisition", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 5'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.lastActiveMs = 1'000;
    snapshot.diagUpdatedMs = 4'500;
    snapshot.diag = diag(P25FollowDiagCode::NoSync);
    snapshot.grantEncryptionKnown = true;
    snapshot.grantEncrypted = false;
    snapshot.rfMetricsPopulated = true;
    snapshot.recentSnrDb = 1.5;
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.carrierDropped);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 slot probe flips early on untrusted clear grant with bursts but no target VCWs", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 8'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.inPassband = true;
    snapshot.grantClearKnown = true;
    snapshot.grantClearStateUnknown = false;
    snapshot.grantMaskParamsKnown = false;
    snapshot.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    snapshot.phase2Bursts = 3;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 0;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.earlyNoSyncFlip);
}

TEST_CASE("P25 slot probe keeps control-channel clear grant slot authoritative", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 8'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.inPassband = true;
    snapshot.grantClearKnown = true;
    snapshot.grantClearStateUnknown = false;
    snapshot.grantMaskParamsKnown = true;
    snapshot.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    snapshot.phase2Bursts = 3;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 4;
    snapshot.phase2MaskedBursts = 2;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE_FALSE(decision.earlyNoSyncFlip);
    REQUIRE_FALSE(decision.maskedOppositeDominantFlip);
    REQUIRE_FALSE(decision.shouldFlip);
}

TEST_CASE("P25 follow holds after brief Phase 2 speaker output before hard timeout", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 20'000;
    snapshot.tunedAtMs = 2'000;
    snapshot.lastActiveMs = 2'000;
    snapshot.recentSpeakerOutputMs = 18'500;
    snapshot.diagUpdatedMs = 19'500;
    snapshot.phase2TrafficCallActive = true;
    snapshot.diag = diag(P25FollowDiagCode::Decoding);
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 0;
    snapshot.phase2Bursts = 0;
    snapshot.diagUpdatedMs = 19'500;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.hardTimeout);
    REQUIRE(decision.action == P25FollowAction::None);

    snapshot.nowMs = 50'000;
    snapshot.recentSpeakerOutputMs = 2'000;
    snapshot.phase2TrafficCallActive = false;
    snapshot.diagUpdatedMs = 49'500;
    const auto lateDecision = evaluateP25Follow(snapshot);
    REQUIRE(lateDecision.hardTimeout);
    REQUIRE(lateDecision.action == P25FollowAction::ReturnHardTimeout);
}

TEST_CASE("P25 follow holds Phase 2 VCW acquisition before activity-gone return", "[p25][follow]")
{
    P25FollowSnapshot snapshot;
    snapshot.autoActive = true;
    snapshot.phase2Voice = true;
    snapshot.nowMs = 20'000;
    snapshot.tunedAtMs = 2'000;
    snapshot.lastActiveMs = 2'000;
    snapshot.diagUpdatedMs = 19'500;
    snapshot.diag = diag(P25FollowDiagCode::Phase2AudioLockMissing);
    snapshot.decodedFrames = 0;
    snapshot.phase2VoiceCodewords = 4;
    snapshot.phase2Bursts = 1;

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE_FALSE(decision.activityGone);
    REQUIRE_FALSE(decision.hardTimeout);
    REQUIRE(decision.action == P25FollowAction::None);
}

TEST_CASE("P25 slot probe flips on masked opposite-slot VCWs with no target voice", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 5'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 20202;
    snapshot.trackedTalkgroupId = 20202;
    snapshot.voiceHz = 421'975'000.0;
    snapshot.trackedVoiceHz = 421'975'000.0;
    snapshot.inPassband = true;
    snapshot.grantClearStateUnknown = true;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 4;
    snapshot.phase2MaskedBursts = 1;
    snapshot.phase2SuperframeBursts = 1;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.maskedOppositeDominantFlip);
    REQUIRE(decision.shouldFlip);
    REQUIRE_FALSE(decision.earlyNoSyncFlip);
}

TEST_CASE("P25 slot probe refuses opposite-slot proof for known control-channel grant", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 3'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 30302;
    snapshot.trackedTalkgroupId = 30302;
    snapshot.voiceHz = 417'675'000.0;
    snapshot.trackedVoiceHz = 417'675'000.0;
    snapshot.inPassband = true;
    snapshot.grantClearStateUnknown = true;
    snapshot.grantMaskParamsKnown = true;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 2;
    snapshot.phase2MaskedBursts = 6;
    snapshot.phase2SuperframeBursts = 6;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.tdmaEpochLocked);
    REQUIRE_FALSE(decision.maskedOppositeDominantFlip);
    REQUIRE_FALSE(decision.shouldFlip);
    REQUIRE_FALSE(decision.earlyNoSyncFlip);
}

TEST_CASE("P25 call security latch is monotonic Unknown to Clear or Encrypted", "[p25][follow][continuity]")
{
    P25ReceiverSessionState session;
    REQUIRE(session.callSecurityLatch == P25CallSecurityLatch::Unknown);
    session.callSecurityLatch = P25CallSecurityLatch::Clear;
    REQUIRE(session.callSecurityLatch == P25CallSecurityLatch::Clear);
    // Encrypted may override Clear for fail-closed; Clear must not regress to Unknown
    // without clearAll (call boundary).
    session.callSecurityLatch = P25CallSecurityLatch::Encrypted;
    REQUIRE(session.callSecurityLatch == P25CallSecurityLatch::Encrypted);
    session.clearAll();
    REQUIRE(session.callSecurityLatch == P25CallSecurityLatch::Unknown);
}

TEST_CASE("P25 protocol frame keys sort in burst order", "[p25][follow][continuity]")
{
    Phase2VoiceFrameKey a;
    a.streamDibitKnown = true;
    a.streamDibit = 1000;
    a.superframeAnchor = 1000;
    a.burstIndex = 4;
    a.voiceIndex = 0;
    a.slot = 1;

    Phase2VoiceFrameKey b = a;
    b.voiceIndex = 3;

    Phase2VoiceFrameKey c = a;
    c.burstIndex = 5;

    REQUIRE(p25Phase2CompareVoiceFrameKeys(a, b) < 0);
    REQUIRE(p25Phase2CompareVoiceFrameKeys(b, c) < 0);
    REQUIRE(p25Phase2CompareVoiceFrameKeys(c, a) > 0);
}

TEST_CASE("P25 stream dibit keys order across overlapping windows", "[p25][follow][continuity]")
{
    Phase2VoiceFrameKey windowA;
    windowA.streamDibitKnown = true;
    windowA.streamDibit = 1'000'900;
    windowA.sessionCodewordIdKnown = true;
    windowA.sessionCodewordId = 42;
    windowA.superframeAnchor = 900;
    windowA.burstIndex = 4;
    windowA.voiceIndex = 0;
    windowA.slot = 1;

    Phase2VoiceFrameKey windowB;
    windowB.streamDibitKnown = true;
    windowB.streamDibit = 1'006'420;
    windowB.sessionCodewordIdKnown = true;
    windowB.sessionCodewordId = 43;
    windowB.superframeAnchor = 420;
    windowB.burstIndex = 5;
    windowB.voiceIndex = 0;
    windowB.slot = 1;

    REQUIRE(p25Phase2CompareVoiceFrameKeys(windowA, windowB) < 0);
    REQUIRE(p25Phase2CompareVoiceFrameKeys(windowB, windowA) > 0);

    Phase2VoiceFrameKey duplicateB = windowB;
    duplicateB.sessionCodewordId = windowB.sessionCodewordId;
    REQUIRE(duplicateB == windowB);
    REQUIRE_FALSE(windowA == windowB);
}

TEST_CASE("P25 frame keys with mixed stable coordinates are unorderable", "[p25][follow][continuity]")
{
    Phase2VoiceFrameKey stable;
    stable.streamDibitKnown = true;
    stable.streamDibit = 5000;
    stable.voiceIndex = 1;
    stable.slot = 0;

    Phase2VoiceFrameKey fallback;
    fallback.superframeAnchor = 100;
    fallback.burstIndex = 2;
    fallback.voiceIndex = 1;
    fallback.slot = 0;

    REQUIRE(p25Phase2CompareVoiceFrameKeys(stable, fallback) == 2);
    REQUIRE(p25Phase2CompareVoiceFrameKeys(fallback, stable) == 2);
}

TEST_CASE("P25 frame sequencer resets only on clearAll call boundary", "[p25][follow][continuity]")
{
    P25ReceiverSessionState session;
    session.frameSequencer.armed = true;
    session.frameSequencer.nextSpeechOrdinal = 12;
    session.frameSequencer.acceptedFrames = 12;
    session.callSecurityLatch = P25CallSecurityLatch::Clear;
    REQUIRE(session.frameSequencer.nextSpeechOrdinal == 12);
    session.clearAll();
    REQUIRE_FALSE(session.frameSequencer.armed);
    REQUIRE(session.frameSequencer.nextSpeechOrdinal == 0);
    REQUIRE(session.callSecurityLatch == P25CallSecurityLatch::Unknown);
}

TEST_CASE("P25 slot probe uses faster opposite-slot proof after untrusted unknown grant epoch lock", "[p25][follow]")
{
    P25SlotProbeSnapshot snapshot;
    snapshot.nowMs = 3'000;
    snapshot.tunedAtMs = 1'000;
    snapshot.trackedArmMs = 1'000;
    snapshot.talkgroupId = 30302;
    snapshot.trackedTalkgroupId = 30302;
    snapshot.voiceHz = 417'675'000.0;
    snapshot.trackedVoiceHz = 417'675'000.0;
    snapshot.inPassband = true;
    snapshot.grantClearStateUnknown = true;
    snapshot.grantMaskParamsKnown = false;
    snapshot.diag = diag(P25FollowDiagCode::WaitingForClearGrant);
    snapshot.phase2TargetVoiceCodewords = 0;
    snapshot.phase2OppositeVoiceCodewords = 2;
    snapshot.phase2MaskedBursts = 6;
    snapshot.phase2SuperframeBursts = 6;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.tdmaEpochLocked);
    REQUIRE(decision.maskedOppositeDominantFlip);
    REQUIRE(decision.shouldFlip);
    REQUIRE_FALSE(decision.earlyNoSyncFlip);
}
