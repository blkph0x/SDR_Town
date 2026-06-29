#include <catch2/catch_all.hpp>

#include "P25FollowStateMachine.h"

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

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.action == P25FollowAction::ReturnEncrypted);
    REQUIRE(decision.encryptedOnVoice);
    REQUIRE(decision.effectiveTalkgroupId == 101);
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
    REQUIRE(decision.action == P25FollowAction::ReturnNoMacEss);
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
    REQUIRE(decision.tdmaNoVcwTimeout);
    REQUIRE(decision.action == P25FollowAction::ReturnNoVoiceCodewords);
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
    REQUIRE(decision.tdmaVcwNoSuperframeTimeout);
    REQUIRE(decision.action == P25FollowAction::ReturnNoMacEss);
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

    const auto decision = evaluateP25Follow(snapshot);
    REQUIRE(decision.encryptedOnVoice);
    REQUIRE(decision.action == P25FollowAction::ReturnEncrypted);
    REQUIRE(decision.effectiveTalkgroupId == 505);
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
    snapshot.phase2SuperframeBursts = 6;
    snapshot.phase2MaskedBursts = 6;
    snapshot.phase2MacCrcValid = 0;
    snapshot.phase2EssKnown = false;

    const auto decision = evaluateP25SlotProbe(snapshot);
    REQUIRE(decision.wrongSlotEligible);
    REQUIRE(decision.noMacEssYet);
    REQUIRE(decision.wrongSlotChecksAfterObservation == 1);
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

    auto decision = evaluateP25SlotProbe(changed);
    REQUIRE(decision.resetTracking);
    REQUIRE(decision.wrongSlotChecksAfterObservation == 1);
    REQUIRE(decision.flipCountAfterObservation == 0);
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
