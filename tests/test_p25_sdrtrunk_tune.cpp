#include <catch2/catch_all.hpp>

#include <cmath>

#include "P25SdrtrunkTune.h"

TEST_CASE("SDRTrunk single-channel center sits just right of the R820T DC hole", "[p25][tune][dec0015]")
{
    constexpr double voiceHz = 421.975e6;
    constexpr double sr = 2.048e6;
    const double center = p25SdrtrunkTunerCenterHz(voiceHz, sr);
    // DecodeConfigP25Phase2 bandwidth 12500; R8x DC_SPIKE 5000; +1 Hz.
    REQUIRE(center == Catch::Approx(voiceHz - 6250.0 - 5000.0 + 1.0).margin(0.5));
    REQUIRE(p25SdrtrunkTunerIsTunedFor(center, sr, voiceHz));
    REQUIRE_FALSE(p25SdrtrunkTunerIsTunedFor(voiceHz, sr, voiceHz));
}

TEST_CASE("SDRTrunk CC+traffic 095450 geometry nudges off the DC spike", "[p25][tune][dec0015]")
{
    constexpr double voiceHz = 420.225e6;
    constexpr double ccHz = 420.475e6;
    constexpr double sr = 2.048e6;
    REQUIRE(p25SdrtrunkCanTune(sr, voiceHz, ccHz));
    REQUIRE_FALSE(p25SdrtrunkTunerIsTunedFor(ccHz, sr, voiceHz, ccHz));
    const double center = p25SdrtrunkTunerCenterHz(voiceHz, sr, ccHz);
    // DEC-0016: follow LO is the single-channel voice park, not the set
    // calculator (that would be minChannel − 5000 = 420213750).
    REQUIRE(center == Catch::Approx(voiceHz - 6250.0 - 5000.0 + 1.0).margin(0.5));
    REQUIRE(p25SdrtrunkTunerIsTunedFor(center, sr, voiceHz, ccHz));
    REQUIRE(std::abs(voiceHz - center) == Catch::Approx(11249.0).margin(0.5));
}

TEST_CASE("115315 two-channel set LO must not be the follow center", "[p25][tune][dec0016]")
{
    constexpr double voiceHz = 421.975e6;
    constexpr double ccHz = 420.475e6;
    constexpr double sr = 2.048e6;
    const double setCenter = p25SdrtrunkGetCenterFrequencyHz(sr, voiceHz, ccHz);
    const double followCenter = p25SdrtrunkTunerCenterHz(voiceHz, sr, ccHz);
    REQUIRE(setCenter == Catch::Approx(420977730.0).margin(1.0));
    REQUIRE(std::abs(voiceHz - setCenter) == Catch::Approx(997270.0).margin(1.0));
    REQUIRE(followCenter == Catch::Approx(voiceHz - 6250.0 - 5000.0 + 1.0).margin(0.5));
    REQUIRE(p25SdrtrunkTunerIsTunedFor(followCenter, sr, voiceHz));
    REQUIRE_FALSE(p25SdrtrunkTunerIsTunedFor(followCenter, sr, voiceHz, ccHz));
}

TEST_CASE("SDRTrunk canTune rejects a span wider than usable bandwidth", "[p25][tune][dec0015]")
{
    constexpr double sr = 2.048e6;
    REQUIRE(p25SdrtrunkCanTune(sr, 420.225e6));
    REQUIRE_FALSE(p25SdrtrunkCanTune(sr, 400.0e6, 420.0e6));
}

TEST_CASE("constants match cited SDRTrunk files", "[p25][tune][dec0015]")
{
    REQUIRE(kP25SdrtrunkChannelBandwidthHz == 12500.0);
    REQUIRE(kP25SdrtrunkDcSpikeHalfBandwidthHz == 5000.0);
    REQUIRE(kP25SdrtrunkUsableBandwidthPercent == 0.98);
}
