#include "ModeS.h"
#include "AdsBTrackStore.h"
#include "AircraftReceivePlan.h"
#include "AircraftMagnitudeStream.h"
#include <QSettings>

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
    for (const double sampleRate : {2.0e6, 2.4e6, 4.0e6, 10.0e6, 20.0e6}) {
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

TEST_CASE("Aircraft capture request respects RF capabilities not audio filter", "[adsb]") {
    CHECK(aircraftReceivePlan(20e6, {2e6, 10e6, 20e6}, {}).sampleRateHz == 20e6);
    const auto rsp = aircraftReceivePlan(20e6, {2e6, 8e6, 10e6}, {200e3, 1.536e6, 8e6});
    CHECK(rsp.sampleRateHz == 10e6);
    CHECK(rsp.hardwareBandwidthHz == 8e6);
    CHECK(aircraftReceivePlan(20e6, {250e3, 1.024e6, 2.4e6}, {}).sampleRateHz == 2.4e6);
    CHECK(aircraftReceivePlan(20e6, {}, {}).sampleRateHz == 0);
    CHECK(aircraftReceivePlan(500e3, {2e6}, {}).sampleRateHz == 0);
    CHECK(aircraftReceivePlan(20e6, {INFINITY, NAN, -1}, {}).sampleRateHz == 0);
}

TEST_CASE("Aircraft streaming packet crosses chunks once but never crosses a gap", "[adsb]") {
    const auto expected = hexFrame("8D40621D58C382D690C8AC2863A7");
    const auto samples = synthesizePpm(expected, 20e6, 31.37);
    const size_t split = samples.size() / 2;
    AircraftMagnitudeStream stream;
    CHECK(stream.process(std::span(samples).first(split), 0, 1, 20e6).empty());
    CHECK(stream.process(std::span(samples).subspan(split), split, 1, 20e6).size() == 1);
    std::vector<float> quiet(3000, .055f);
    CHECK(stream.process(quiet, samples.size(), 1, 20e6).empty());
    stream.reset();
    stream.process(std::span(samples).first(split), 0, 1, 20e6);
    CHECK(stream.process(std::span(samples).subspan(split), split + 1, 1, 20e6).empty());
    stream.reset();
    stream.process(std::span(samples).first(split), 0, 1, 20e6);
    CHECK(stream.process(std::span(samples).subspan(split), split, 2, 20e6).empty());
}

namespace {
struct RestoreAircraftNetwork {
    QSettings settings{"SDR_Town", "SDR Town"};
    QVariant saved = settings.value("aircraft/networkEnabled");
    bool enabled = AdsBTrackStore::instance().networkEnabled();
    ~RestoreAircraftNetwork() {
        AdsBTrackStore::instance().setNetworkEnabled(enabled);
        if (saved.isValid()) settings.setValue("aircraft/networkEnabled", saved);
        else settings.remove("aircraft/networkEnabled");
    }
};
nlohmann::json networkAircraft(std::string icao, std::string call, double lat = 40) {
    return nlohmann::json::array({icao, call, "Test", 0, 0, 30, lat, 1000,
        false, 100, 90, 0, nullptr, 1000, "1200", false, 0});
}
}

TEST_CASE("Internet off removes network fields but preserves RF provenance", "[adsb]") {
    RestoreAircraftNetwork restore;
    auto& store = AdsBTrackStore::instance();
    store.setNetworkEnabled(false);
    store.setNetworkEnabled(true);
    const auto generation = store.networkGeneration();
    store.ingestAdscPosition(0xA0B001, -34, 151, {}, "LOCAL1");
    const std::string body = nlohmann::json{{"states", {
        networkAircraft("a0b001", "INTERNET1"), networkAircraft("a0b002", "INTERNET2")}}}.dump();
    REQUIRE(store.mergeNetworkJson(body, generation));
    const auto local = store.trackByIcao(0xA0B001);
    CHECK(local.callsign == "LOCAL1"); CHECK(local.latDeg == -34);
    CHECK_FALSE(local.fromNetwork);
    CHECK(store.trackByIcao(0xA0B002).squawk == "1200");
    store.setNetworkEnabled(false);
    CHECK(store.trackByIcao(0xA0B002).icao == 0);
    CHECK(store.trackByIcao(0xA0B001).callsign == "LOCAL1");
    CHECK_FALSE(store.mergeNetworkJson(body, generation));
    std::string error;
    CHECK_FALSE(store.refreshNetwork(&error)); // no HTTP request while disabled
    CHECK(error == "Internet aircraft disabled");
    store.setNetworkEnabled(true);
    CHECK_FALSE(store.mergeNetworkJson(body, generation)); // off/on cannot revive old reply
    store.setNetworkError("obsolete", generation);
    CHECK(store.snapshot().lastStatus != "obsolete");
}

TEST_CASE("Aircraft network decoder rejects malformed rows independently", "[adsb]") {
    RestoreAircraftNetwork restore;
    auto& store = AdsBTrackStore::instance();
    store.setNetworkEnabled(true);
    auto malformed = networkAircraft("a0b005", "BAD"); malformed[5] = "not longitude";
    auto j = nlohmann::json{{"states", {nlohmann::json::array({"a0b004", "short", 0, 0, 0, 0, 0, 0}),
        networkAircraft("a0b003x", "BAD"), networkAircraft("a0b003", "BAD", 999),
        malformed, networkAircraft("a0b006", "GOOD")}}};
    REQUIRE(store.mergeNetworkJson(j.dump(), store.networkGeneration()));
    CHECK(store.trackByIcao(0xA0B004).icao == 0);
    CHECK(store.trackByIcao(0xA0B003).icao == 0);
    CHECK(store.trackByIcao(0xA0B005).icao == 0);
    CHECK(store.trackByIcao(0xA0B006).callsign == "GOOD");
    CHECK_FALSE(store.mergeNetworkJson("{", store.networkGeneration()));
    CHECK_FALSE(store.mergeNetworkJson(std::string(2 * 1024 * 1024 + 1, 'x'), store.networkGeneration()));
    CHECK(store.statusJson().at("networkEnabled").get<bool>());
    store.setNetworkEnabled(false);
}
