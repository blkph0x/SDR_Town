#include <catch2/catch_all.hpp>

#include <string>

#include "P25AudioDropClass.h"

TEST_CASE("P25 audio drop classifier: follow return is bucket E", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.targetVcw = 40;
    sample.fed = 40;
    sample.emittedPcm = 40;
    sample.windowSeconds = 1.0;
    sample.followReturned = true;
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Follow);
    REQUIRE(p25AudioDropBucketLabel(P25AudioDropBucket::Follow)[0] == 'E');
}

TEST_CASE("P25 audio drop classifier: overlap dups with no unique VCWs is A", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.targetVcw = 40;
    sample.dups = 40;
    sample.fed = 0;
    sample.emittedPcm = 0;
    sample.windowSeconds = 1.0;
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Extract);
    REQUIRE(std::string(p25AudioDropBucketLabel(P25AudioDropBucket::Extract)) == "A");
}

TEST_CASE("P25 audio drop classifier: unique VCWs present but not fed is B", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.targetVcw = 20;
    sample.dups = 2;
    sample.fed = 0;
    sample.emittedPcm = 0;
    sample.windowSeconds = 1.0;
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Feed);
    REQUIRE(std::string(p25AudioDropBucketLabel(P25AudioDropBucket::Feed)) == "B");
}

TEST_CASE("P25 audio drop classifier: fed without emit is C", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.targetVcw = 20;
    sample.fed = 16;
    sample.emittedPcm = 0;
    sample.windowSeconds = 1.0;
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Emit);
    REQUIRE(std::string(p25AudioDropBucketLabel(P25AudioDropBucket::Emit)) == "C");
}

TEST_CASE("P25 audio drop classifier: sparse emit islands are D", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.targetVcw = 20;
    sample.fed = 16;
    sample.emittedPcm = 10; // duty = 0.20 < 0.65
    sample.windowSeconds = 1.0;
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Playout);
    REQUIRE(std::string(p25AudioDropBucketLabel(P25AudioDropBucket::Playout)) == "D");
}

TEST_CASE("P25 audio drop classifier: continuous duty is ok", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.targetVcw = 50;
    sample.fed = 48;
    sample.emittedPcm = 40; // duty = 0.80
    sample.dups = 2;
    sample.windowSeconds = 1.0;
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Ok);
    REQUIRE(std::string(p25AudioDropBucketLabel(P25AudioDropBucket::Ok)) == "ok");
}

TEST_CASE("P25 audio drop classifier: idle zeros are extract (no unique VCWs)", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.windowSeconds = 1.0;
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Extract);
}

TEST_CASE("P25 audio drop classifier: voicetest span scales the unique floor", "[p25][drop]")
{
    P25AudioDropSample sample;
    sample.targetVcw = 20;
    sample.dups = 0;
    sample.fed = 0;
    sample.emittedPcm = 0;
    sample.windowSeconds = 8.0; // unique/s = 2.5 < 8 → A, not B
    REQUIRE(classifyP25AudioDrop(sample) == P25AudioDropBucket::Extract);
}

TEST_CASE("DEC-0046 wall timeout must not clear speaker pending", "[p25][drop][dec0046]")
{
    REQUIRE_FALSE(p25Phase2WallTimeoutMayClearSpeakerPending("decode-wall-timeout", false));
    REQUIRE_FALSE(p25Phase2WallTimeoutMayClearSpeakerPending("decode-wall-overbudget-kept", false));
    REQUIRE_FALSE(p25Phase2WallTimeoutMayClearSpeakerPending("decode-wall-timeout", true));
    REQUIRE(p25Phase2WallTimeoutMayClearSpeakerPending("traffic-generation-stale", false));
    REQUIRE(p25Phase2WallTimeoutMayClearSpeakerPending("call-session-changed", false));
}

TEST_CASE("DEC-0046 healthy sustain wall equals global wall until cooperative abort", "[p25][drop][dec0046]")
{
    REQUIRE(p25Phase2LiveSustainBudgetWallSane(80, 320, 320));
    REQUIRE_FALSE(p25Phase2LiveSustainBudgetWallSane(80, 105, 320)); // DEC-0045 rejected
    REQUIRE_FALSE(p25Phase2LiveSustainBudgetWallSane(80, 60, 320));  // wall < budget
    REQUIRE_FALSE(p25Phase2LiveSustainBudgetWallSane(0, 320, 320));
}
