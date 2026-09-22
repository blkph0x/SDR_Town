#include "ModeS.h"
#include "AdsBTrackStore.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> hexFrame(const std::string& text)
{
    auto nibble = [](char ch) -> uint8_t {
        if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
        if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
        if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
        return 0;
    };
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < text.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>((nibble(text[i]) << 4) | nibble(text[i + 1])));
    return bytes;
}

bool interval(double value, double begin, double end)
{
    return value >= begin && value < end;
}

std::vector<float> synthesizePpm(const std::vector<uint8_t>& frame,
                                 double sampleRateHz,
                                 double startSample)
{
    const size_t count = static_cast<size_t>(
        std::ceil(startSample + sampleRateHz * 125.0e-6 + 16.0));
    std::vector<float> magnitude(count, 0.0f);
    for (size_t i = 0; i < count; ++i) {
        const double tUs = (static_cast<double>(i) - startSample) /
                           sampleRateHz * 1.0e6;
        bool pulse = interval(tUs, 0.0, 0.5) ||
                     interval(tUs, 1.0, 1.5) ||
                     interval(tUs, 3.5, 4.0) ||
                     interval(tUs, 4.5, 5.0);
        if (tUs >= 8.0 && tUs < 120.0) {
            const double dataTime = tUs - 8.0;
            const int bitIndex = static_cast<int>(std::floor(dataTime));
            const double bitPhase = dataTime - bitIndex;
            const bool bit = ((frame[static_cast<size_t>(bitIndex / 8)] >>
                               (7 - (bitIndex % 8))) & 1u) != 0;
            pulse = bit ? bitPhase < 0.5 : bitPhase >= 0.5;
        }
        const double deterministicNoise =
            0.006 * std::sin(static_cast<double>(i) * 0.37) +
            0.004 * std::cos(static_cast<double>(i) * 0.11);
        magnitude[i] = static_cast<float>(0.055 + deterministicNoise + (pulse ? 0.95 : 0.0));
    }
    return magnitude;
}

} // namespace

TEST_CASE("Mode-S DF and ICAO helpers", "[adsb]")
{
    uint8_t msg[14]{};
    msg[0] = 0x8D; // DF17
    msg[1] = 0xAB;
    msg[2] = 0xCD;
    msg[3] = 0xEF;
    REQUIRE(ModeS::downlinkFormat(msg) == 17);
    REQUIRE(ModeS::icaoAddress(msg) == 0xABCDEFu);
}

TEST_CASE("Mode-S CRC accepts known DF17 messages and rejects corruption", "[adsb]")
{
    auto even = hexFrame("8D40621D58C382D690C8AC2863A7");
    auto odd = hexFrame("8D40621D58C386435CC412692AD6");
    REQUIRE(even.size() == 14);
    REQUIRE(odd.size() == 14);
    REQUIRE(ModeS::crcOk(even.data(), 14));
    REQUIRE(ModeS::crcOk(odd.data(), 14));
    even[8] ^= 0x01;
    REQUIRE_FALSE(ModeS::crcOk(even.data(), 14));
}

TEST_CASE("Mode-S identity alphabet decodes a real callsign", "[adsb]")
{
    const auto msg = hexFrame("8D4840D6202CC371C32CE0576098");
    REQUIRE(ModeS::crcOk(msg.data(), 14));
    REQUIRE(ModeS::typeCode(msg.data()) == 4);
    const auto id = ModeS::decodeIdentity(msg.data());
    REQUIRE(id.valid);
    REQUIRE(id.callsign == "KLM1023");
}

TEST_CASE("Mode-S velocity decodes subtype one and scales subtype two", "[adsb]")
{
    auto msg = hexFrame("8D485020994409940838175B284F");
    REQUIRE(ModeS::crcOk(msg.data(), 14));
    const auto velocity = ModeS::decodeVelocity(msg.data());
    REQUIRE(velocity.valid);
    REQUIRE(velocity.groundSpeedKt == Catch::Approx(159.201).margin(0.01));
    REQUIRE(velocity.trackDeg == Catch::Approx(182.88).margin(0.02));
    REQUIRE(velocity.verticalRateFpm == Catch::Approx(-832.0));

    msg[4] = static_cast<uint8_t>((19 << 3) | 2); // same vector represented as supersonic subtype
    const auto subtypeTwo = ModeS::decodeVelocity(msg.data());
    REQUIRE(subtypeTwo.valid);
    REQUIRE(subtypeTwo.groundSpeedKt == Catch::Approx(velocity.groundSpeedKt * 4.0).margin(0.05));
}

TEST_CASE("CPR known even and odd pair selects the matching longitude frame", "[adsb]")
{
    const auto evenNewer = ModeS::decodeCprPair(
        true, 93000, 51372, true, 74158, 50194);
    REQUIRE(evenNewer.valid);
    REQUIRE(evenNewer.latDeg == Catch::Approx(52.257202).margin(1e-5));
    REQUIRE(evenNewer.lonDeg == Catch::Approx(3.919373).margin(1e-5));

    const auto oddNewer = ModeS::decodeCprPair(
        false, 93000, 51372, true, 74158, 50194);
    REQUIRE(oddNewer.valid);
    REQUIRE(oddNewer.latDeg == Catch::Approx(52.265780).margin(1e-5));
    REQUIRE(oddNewer.lonDeg == Catch::Approx(3.938913).margin(1e-5));
    REQUIRE(std::abs(evenNewer.lonDeg - oddNewer.lonDeg) > 0.01);
}

TEST_CASE("CPR local decode is finite in both hemispheres", "[adsb]")
{
    const auto south = ModeS::decodeCprLocal(false, 50000, 60000, -33.87, 151.21);
    REQUIRE(south.valid);
    REQUIRE(std::isfinite(south.latDeg));
    REQUIRE(std::isfinite(south.lonDeg));
    REQUIRE(south.latDeg < 0.0);
    REQUIRE(south.latDeg > -90.0);

    const auto north = ModeS::decodeCprLocal(true, 40000, 70000, 51.5, -0.1);
    REQUIRE(north.valid);
    REQUIRE(std::isfinite(north.latDeg));
    REQUIRE(std::isfinite(north.lonDeg));
}

TEST_CASE("ADS-B PPM extractor handles fractional phase at common SDR rates", "[adsb][ppm]")
{
    const auto expected = hexFrame("8D40621D58C382D690C8AC2863A7");
    for (const double sampleRate : {2.0e6, 2.4e6, 4.0e6}) {
        const auto magnitude = synthesizePpm(expected, sampleRate, 31.37);
        const auto frames = ModeS::extractFramesFromMagnitude(
            magnitude.data(), magnitude.size(), sampleRate);
        REQUIRE(std::find(frames.begin(), frames.end(), expected) != frames.end());
    }
}

TEST_CASE("ADS-B PPM extractor rejects structured noise without a preamble", "[adsb][ppm]")
{
    constexpr double sampleRate = 2.4e6;
    std::vector<float> magnitude(6000);
    for (size_t i = 0; i < magnitude.size(); ++i) {
        magnitude[i] = static_cast<float>(
            0.08 + 0.03 * std::sin(2.0 * std::numbers::pi_v<double> *
                                  static_cast<double>(i) / 17.0));
    }
    REQUIRE(ModeS::extractFramesFromMagnitude(
        magnitude.data(), magnitude.size(), sampleRate).empty());
}

TEST_CASE("AdsBTrackStore merges observer and snapshot", "[adsb]")
{
    AdsBTrackStore::instance().setObserver(-33.87, 151.21, 80.0);
    const auto snap = AdsBTrackStore::instance().snapshot();
    REQUIRE(snap.centerLat == Catch::Approx(-33.87).margin(1e-6));
    REQUIRE(snap.centerLon == Catch::Approx(151.21).margin(1e-6));
    REQUIRE(snap.radiusNm >= 20.0);
}

TEST_CASE("ADS-C merges into AdsBTrackStore", "[adsb]")
{
    AdsBTrackStore::instance().ingestAdscPosition(0xABCDEF, -33.87, 151.21, "ABCDEF", "QFA1");
    const auto track = AdsBTrackStore::instance().trackByIcao(0xABCDEF);
    REQUIRE(track.fromAdsc);
    REQUIRE(track.positionValid);
    REQUIRE(track.latDeg == Catch::Approx(-33.87).margin(1e-4));
}
