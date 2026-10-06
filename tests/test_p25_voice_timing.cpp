#include "P25VoiceTiming.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Active-clear speaker backlog catch-up uses measured 240/200/280", "[p25][voice-timing]") {
    REQUIRE(kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds == 0.240);
    REQUIRE(kP25Phase2VoiceDecodeSpeakerBacklogCatchUpMinFreshSeconds == 0.160);
    REQUIRE(kP25Phase2VoiceDecodeActiveSpeakerBacklogCatchUpMinFreshSeconds == 0.200);
    REQUIRE(kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds == 0.280);

    const auto plan = p25Phase2PlanVoiceDecodeChunk(
        /*streamingDdc=*/false,
        /*backlogCatchUp=*/true,
        /*activeSpeakerClearPath=*/true,
        /*wideReacquireWindow=*/false,
        /*maskEpochRepairWindow=*/false,
        /*speakerSustainDecode=*/true,
        /*phase2SustainDecodeWindow=*/true,
        /*firstColdEyeChunk=*/false,
        /*unacquiredAcquireWindow=*/false,
        /*decodeCursorAdvancedPastStart=*/true);

    CHECK(plan.maxChunkSeconds == Catch::Approx(0.240));
    CHECK(plan.minFreshSeconds == Catch::Approx(0.200));
    CHECK(plan.overlapSeconds == Catch::Approx(0.280));
}

TEST_CASE("Non-active speaker backlog catch-up preserves 240/160/280", "[p25][voice-timing]") {
    const auto plan = p25Phase2PlanVoiceDecodeChunk(
        false, true, /*activeSpeakerClearPath=*/false, false, false,
        true, true, false, false, true);
    CHECK(plan.maxChunkSeconds == Catch::Approx(0.240));
    CHECK(plan.minFreshSeconds == Catch::Approx(0.160));
    CHECK(plan.overlapSeconds == Catch::Approx(0.280));
}

TEST_CASE("Speaker sustain stays on 80/40/280 when not catching up", "[p25][voice-timing]") {
    const auto plan = p25Phase2PlanVoiceDecodeChunk(
        false, /*backlogCatchUp=*/false, true, false, false,
        true, true, false, false, true);
    CHECK(plan.maxChunkSeconds == Catch::Approx(0.080));
    CHECK(plan.minFreshSeconds == Catch::Approx(0.040));
    CHECK(plan.overlapSeconds == Catch::Approx(0.280));
}

TEST_CASE("Realtime catch-up constants are not compiled back in", "[p25][voice-timing]") {
    // Named identifiers must stay gone after DEC-0200 revert.
#if defined(kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpChunkSeconds) || \
    defined(kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpMinFreshSeconds)
#  error "realtime catch-up constants must remain removed"
#endif
    SUCCEED();
}

TEST_CASE("Locked-lattice first empty hop stays healthy 80/4", "[p25][voice-timing][dec0203]") {
    REQUIRE(kP25LiveLockedLatticeEmptyEscalateStreak == 2);
    REQUIRE(kP25LiveEyeLostReplayCandStreak == 1);
    REQUIRE(kP25LiveHealthySustainBudgetMs == 80);
    REQUIRE(kP25LiveHealthySustainCqpskCandidates == 4);

    const P25Phase2LiveEyeSnapshot lockedEmpty{0, 0, 8, 8, 8};
    const auto first = p25Phase2PlanLiveHotSearch(lockedEmpty, true, 0);
    CHECK(first.eyeLost);
    CHECK(first.lockedLatticeEmpty);
    CHECK_FALSE(first.escalateReplayCands);
    CHECK(first.eyeLostStreak == 1);
    CHECK(first.budgetMs == kP25LiveHealthySustainBudgetMs);
    CHECK(first.cqpskCandidates == kP25LiveHealthySustainCqpskCandidates);

    const auto second = p25Phase2PlanLiveHotSearch(lockedEmpty, true, first.eyeLostStreak);
    CHECK(second.lockedLatticeEmpty);
    CHECK(second.escalateReplayCands);
    CHECK(second.eyeLostStreak == 2);
    CHECK(second.budgetMs == kP25LiveEyeLostReplayBudgetMs);
    CHECK(second.cqpskCandidates == kP25ReplayHotCqpskCandidates);
}

TEST_CASE("True lost-eye still escalates on the first miss", "[p25][voice-timing][dec0203]") {
    const P25Phase2LiveEyeSnapshot noStructure{0, 0, 0, 0, 0};
    const auto plan = p25Phase2PlanLiveHotSearch(noStructure, true, 0);
    CHECK(plan.eyeLost);
    CHECK_FALSE(plan.lockedLatticeEmpty);
    CHECK(plan.escalateReplayCands);
    CHECK(plan.budgetMs == kP25LiveEyeLostReplayBudgetMs);
    CHECK(plan.cqpskCandidates == kP25ReplayHotCqpskCandidates);
}

TEST_CASE("Healthy target eye stays on 80/4", "[p25][voice-timing][dec0203]") {
    const P25Phase2LiveEyeSnapshot healthy{18, 14, 16, 13, 13};
    const auto plan = p25Phase2PlanLiveHotSearch(healthy, true, 3);
    CHECK_FALSE(plan.eyeLost);
    CHECK_FALSE(plan.lockedLatticeEmpty);
    CHECK(plan.eyeLostStreak == 0);
    CHECK(plan.budgetMs == kP25LiveHealthySustainBudgetMs);
    CHECK(plan.cqpskCandidates == kP25LiveHealthySustainCqpskCandidates);
}
