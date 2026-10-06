#include "P25VoiceTiming.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Active-clear speaker realtime catch-up uses 360/160/280 geometry", "[p25][voice-timing]") {
    REQUIRE(kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpChunkSeconds == 0.360);
    REQUIRE(kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpMinFreshSeconds == 0.160);
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

    CHECK(plan.maxChunkSeconds == Catch::Approx(0.360));
    CHECK(plan.minFreshSeconds == Catch::Approx(0.160));
    CHECK(plan.overlapSeconds == Catch::Approx(0.280));
}

TEST_CASE("Non-active speaker backlog catch-up keeps 240/160/280", "[p25][voice-timing]") {
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
