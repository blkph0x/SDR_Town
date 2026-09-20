#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

#include "P25AudioDropClass.h"
#include "P25TalkgroupRegistry.h"

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

TEST_CASE("P25 unresolved talkgroup grants use the resolved-grant correction budget", "[p25][control][talkgroup]")
{
    P25ControlEvent grant;
    grant.type = P25ControlEventType::GroupVoiceGrant;
    grant.talkgroupId = 12345;
    grant.channel = 0x7001;

    REQUIRE_FALSE(p25ControlEventIsResolvedVoiceGrant(grant));
    REQUIRE(kP25PendingVoiceGrantMaxCorrectedDibits == kP25VoiceGrantMaxCorrectedDibits);
    REQUIRE(p25TsbkPendingVoiceGrantEligible(kP25PendingVoiceGrantMaxCorrectedDibits, grant));

    std::vector<P25PendingVoiceGrant> pending;
    REQUIRE(p25RememberPendingVoiceGrant(
        pending, grant, kP25PendingVoiceGrantMaxCorrectedDibits, 1000));
    REQUIRE(pending.size() == 1);
    REQUIRE(pending.front().event.talkgroupId == 12345);
    REQUIRE(pending.front().correctedDibitErrors == kP25VoiceGrantMaxCorrectedDibits);

    P25ControlChannelAnalyzer analyzer;
    P25ChannelIdentifier identifier;
    identifier.valid = true;
    identifier.id = 7;
    identifier.channelType = 3;
    identifier.baseHz = 420000000.0;
    identifier.spacingHz = 12500.0;
    identifier.bandwidthHz = 12500.0;
    identifier.slotsPerCarrier = 2;
    identifier.phase2Capable = true;
    analyzer.setChannelIdentifier(identifier);

    const auto resolved = p25ResolvePendingVoiceGrants(pending, analyzer, 1100);
    REQUIRE(pending.empty());
    REQUIRE(resolved.size() == 1);
    REQUIRE(p25ControlEventIsResolvedVoiceGrant(resolved.front()));
    REQUIRE(resolved.front().talkgroupId == 12345);

    std::vector<P25TalkgroupEntry> registry;
    REQUIRE(mergeP25TalkgroupEvent(registry, 420475000.0, resolved.front(), 1200));
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.front().talkgroupId == 12345);
    REQUIRE(registry.front().lastVoiceFreqHz > 0.0);

    REQUIRE_FALSE(p25TsbkPendingVoiceGrantEligible(
        kP25PendingVoiceGrantMaxCorrectedDibits + 1, grant));
}
