#include "ModeS.h"
#include "AdsBTrackStore.h"
#include "AircraftReceivePlan.h"
#include "AircraftMagnitudeStream.h"
#include <QSettings>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
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

TEST_CASE("Aircraft geometric-altitude reports reach the position decoder", "[adsb]") {
    // Known 40621D CPR coordinates, TC20/21/22 and recomputed CRC24 (DEC-0197).
    const char* pairs[][2]={
        {"8D40621DA0C382D690C8AC5C84CA","8D40621DA0C386435CC4121DCDBB"},
        {"8D40621DA8C382D690C8ACBF775F","8D40621DA8C386435CC412FE3E2E"},
        {"8D40621DB0C382D690C8AC6497E9","8D40621DB0C386435CC41225DE98"}};
    for(const auto& pair:pairs) {
        AdsBTrackStore store("qa-geometric");store.setNetworkEnabled(false);
        const auto even=hexFrame(pair[0]),odd=hexFrame(pair[1]);
        REQUIRE(ModeS::crcOk(even.data(),14));REQUIRE(ModeS::crcOk(odd.data(),14));
        store.ingestModeSFrame(even.data());CHECK_FALSE(store.trackByIcao(0x40621d).positionValid);
        store.ingestModeSFrame(odd.data());const auto track=store.trackByIcao(0x40621d);
        REQUIRE(track.positionValid);
        CHECK(track.latDeg==Catch::Approx(52.26).margin(.01));
        CHECK(track.lonDeg==Catch::Approx(3.93).margin(.02));
        CHECK_FALSE(track.trackValid);
    }
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
nlohmann::json networkAircraft(std::string icao, std::string call, double lat = 40,
                              double utc = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()) {
    return nlohmann::json::array({icao, call, "Test", std::floor(utc), std::floor(utc), 30, lat, 1000,
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

TEST_CASE("Aircraft ground track is validated independently of position", "[adsb]") {
    AdsBTrackStore store("qa-direction");store.setNetworkEnabled(true);
    auto row=networkAircraft("a0b099","DIRECTION");
    for(const auto value:nlohmann::json::array({0,90,359.9,-1,360,nullptr,"90"})) {
        row[10]=value;
        REQUIRE(store.mergeNetworkJson(nlohmann::json{{"states",{row}}}.dump(),store.networkGeneration()));
        const auto track=store.trackByIcao(0xa0b099);
        CHECK(track.positionValid);
        const bool valid=value.is_number() && value.get<double>()>=0 && value.get<double>()<360;
        CHECK(track.trackValid==valid);
        if(valid)CHECK(track.trackDeg==value.get<double>());
    }
    const auto frame=hexFrame("8D485020994409940838175B284F");
    store.ingestModeSFrame(frame.data());
    CHECK(store.trackByIcao(0x485020).trackValid);
    CHECK(store.trackByIcao(0x485020).trackDeg==Catch::Approx(182.88).margin(.02));
    store.setNetworkEnabled(false);
}

namespace {
// Readdress the reference frames (or alter unavailable-value bits), then encode
// CRC24 using the Mode-S polynomial already exercised by the reference fixtures.
std::vector<uint8_t> withAircraftCrc(std::vector<uint8_t> frame, uint32_t icao = 0x40621D) {
    frame[1] = uint8_t(icao >> 16); frame[2] = uint8_t(icao >> 8); frame[3] = uint8_t(icao);
    frame[11] = frame[12] = frame[13] = 0;
    uint32_t remainder = 0;
    for (int bit = 0; bit < 112; ++bit) {
        const bool top = (remainder & 0x800000u) != 0;
        remainder = ((remainder << 1) & 0xFFFFFFu) | ((frame[bit / 8] >> (7 - bit % 8)) & 1u);
        if (top) remainder ^= 0xFFF409u;
    }
    frame[11] = uint8_t(remainder >> 16); frame[12] = uint8_t(remainder >> 8); frame[13] = uint8_t(remainder);
    REQUIRE(ModeS::crcOk(frame.data(), 14));
    return frame;
}

struct AircraftTimeFixture {
    double utc = 1800000000.0;
    double monotonic = 0.0;
    QSettings settings{"SDR_Town", "SDR Town"};
    QVariant saved = settings.value("aircraft/sessions/qa-freshness/networkEnabled");
    AdsBTrackStore store{"qa-freshness", [this] { return AircraftClockSample{utc, monotonic}; }};

    AircraftTimeFixture() { store.setNetworkEnabled(false); }
    ~AircraftTimeFixture() {
        if (saved.isValid()) settings.setValue("aircraft/sessions/qa-freshness/networkEnabled", saved);
        else settings.remove("aircraft/sessions/qa-freshness/networkEnabled");
    }
    void advance(double seconds) { utc += seconds; monotonic += seconds; }
    void frame(const char* text) { const auto bytes = withAircraftCrc(hexFrame(text)); store.ingestModeSFrame(bytes.data()); }
    void position() {
        frame("8D40621D58C386435CC412692AD6");
        advance(1);
        frame("8D40621D58C382D690C8AC2863A7");
    }
    void identity() { frame("8D4840D6202CC371C32CE0576098"); }
    void velocity() { frame("8D485020994409940838175B284F"); }
    nlohmann::json row() const { return networkAircraft("40621d", "NETWORK", 40, utc); }
    void network(nlohmann::json row) {
        store.setNetworkEnabled(true);
        REQUIRE(store.mergeNetworkJson(nlohmann::json{{"states", {row}}}.dump(), store.networkGeneration()));
    }
    AircraftTrack track() const { return store.trackByIcao(0x40621D); }
};
}

TEST_CASE("Aircraft cold and position-only frames never manufacture altitude or motion", "[adsb][freshness]") {
    AircraftTimeFixture f;
    CHECK(f.store.snapshot().tracks.empty());
    CHECK(f.store.statusJson()["tracks"].empty());
    CHECK(f.store.snapshot().networkAgeSec == -1);
    CHECK_FALSE(f.track().altitudeValid);
    CHECK(std::isnan(f.track().altFt));
    f.position();
    const auto track = f.track();
    REQUIRE(track.positionValid);
    CHECK(track.position.source == AircraftSource::LocalAdsb);
    CHECK(track.position.ageSec == 0);
    CHECK_FALSE(track.altitudeValid);
    CHECK_FALSE(track.groundSpeedValid);
    CHECK_FALSE(track.trackValid);
    CHECK_FALSE(track.verticalRateValid);
    const auto row = f.store.statusJson()["tracks"].at(0);
    for (const auto key : {"altFt", "gsKt", "trackDeg", "vrateFpm"}) CHECK(row[key].is_null());
    CHECK(row["fields"]["altitude"].is_null());
    CHECK(row["fields"]["position"]["source"] == "local_adsb");
}

TEST_CASE("Aircraft identification cannot renew position or velocity", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.position(); f.velocity();
    const double observed = f.track().position.unixTime;
    for (int i = 0; i < 4; ++i) { f.advance(30); f.identity(); }
    CHECK(f.track().positionValid); // Existing retention boundary is inclusive.
    CHECK(f.track().position.ageSec == 120);
    f.advance(1); f.identity();
    const auto track = f.track();
    REQUIRE(track.icao == 0x40621D);
    CHECK(track.callsign == "KLM1023");
    CHECK(track.presence.ageSec == 0);
    CHECK(track.position.unixTime == observed);
    CHECK(track.position.ageSec == 121);
    CHECK_FALSE(track.positionValid);
    CHECK_FALSE(track.groundSpeedValid);
    CHECK_FALSE(track.trackValid);
    CHECK_FALSE(track.verticalRateValid);
    CHECK(std::isnan(track.latDeg));
    CHECK(f.store.snapshot().tracks.empty());
    f.advance(121);
    CHECK(f.track().icao == 0);
}

TEST_CASE("Aircraft velocity and identity expire independently of each other and position", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.identity(); f.velocity();
    f.advance(110); f.position();
    f.advance(10);
    const auto track = f.track();
    CHECK(track.positionValid);
    CHECK(track.position.ageSec == 10);
    CHECK(track.callsign.empty());
    CHECK_FALSE(track.groundSpeedValid);
    CHECK_FALSE(track.trackValid);
    CHECK_FALSE(track.verticalRateValid);
    f.velocity();
    CHECK(f.track().groundSpeedValid);
    CHECK(f.track().position.ageSec == 10);
}

TEST_CASE("Aircraft fresh network position wins despite newer local identity and velocity", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.position();
    f.advance(10); f.network(f.row());
    f.advance(1); f.identity(); f.velocity();
    const auto track = f.track();
    REQUIRE(track.positionValid);
    CHECK(track.latDeg == 40);
    CHECK(track.fromNetwork);
    CHECK_FALSE(track.fromLocal);
    CHECK(track.position.source == AircraftSource::OpenSky);
    CHECK(track.identity.source == AircraftSource::LocalAdsb);
    CHECK(track.groundSpeed.source == AircraftSource::LocalAdsb);
    CHECK(track.altitude.source == AircraftSource::OpenSky);
    CHECK(track.altitudeReference == "barometric");
    CHECK(track.callsign == "KLM1023");
    f.store.setNetworkEnabled(false);
    CHECK(f.track().position.source == AircraftSource::LocalAdsb);
    CHECK_FALSE(f.track().altitudeValid);
    f.advance(110); f.identity();
    CHECK_FALSE(f.track().positionValid);
    CHECK(f.store.snapshot().tracks.empty());
}

TEST_CASE("Aircraft network-off removes all enrichment without reviving stale RF coordinates", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.position(); f.advance(120); f.identity(); f.advance(1);
    f.network(f.row());
    CHECK(f.track().positionValid);
    const auto generation = f.store.networkGeneration();
    const auto body = nlohmann::json{{"states", {f.row()}}}.dump();
    f.store.setNetworkEnabled(false);
    CHECK_FALSE(f.track().positionValid);
    CHECK_FALSE(f.track().altitudeValid);
    CHECK_FALSE(f.track().groundSpeedValid);
    CHECK(f.track().squawk.empty());
    CHECK(f.track().callsign == "KLM1023");
    CHECK(f.store.statusJson()["tracks"].empty());
    f.store.setNetworkEnabled(true);
    CHECK_FALSE(f.store.mergeNetworkJson(body, generation));
    CHECK_FALSE(f.track().positionValid);
}

TEST_CASE("Aircraft ADS-C presence and position do not masquerade as local ADS-B", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.store.ingestAdscPosition(0x40621D, -34, 151, {}, "SAT1");
    f.advance(1); f.identity(); f.velocity();
    CHECK(f.track().fromAdsc);
    CHECK_FALSE(f.track().fromLocal);
    CHECK(f.track().position.source == AircraftSource::Adsc);
    CHECK(f.track().identity.source == AircraftSource::LocalAdsb);
    CHECK_FALSE(f.track().altitudeValid);
    f.advance(120); f.identity();
    CHECK_FALSE(f.track().positionValid);
    CHECK_FALSE(f.track().fromAdsc);
    f.store.ingestAdscPosition(0x40621D, -35, 152);
    CHECK(f.track().fromAdsc);
    CHECK(f.track().latDeg == -35);
}

TEST_CASE("Aircraft lifetime and CPR pairing use monotonic time through UTC jumps", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.position(); f.velocity(); f.network(f.row());
    f.utc -= 86400;
    CHECK(f.track().positionValid);
    CHECK(f.track().position.ageSec == 0);
    f.advance(121); f.identity();
    CHECK_FALSE(f.track().positionValid);
    CHECK_FALSE(f.track().groundSpeedValid);
    CHECK(f.store.snapshot().networkAgeSec == 121);
    CHECK_FALSE(f.store.snapshot().networkOnline);
    f.utc += 172800;
    f.frame("8D40621D58C382D690C8AC2863A7");
    CHECK_FALSE(f.track().positionValid); // Old odd CPR cannot be paired after a clock jump.
    f.advance(1); f.frame("8D40621D58C386435CC412692AD6");
    CHECK(f.track().positionValid);
    CHECK(f.track().position.ageSec == 0);
}

TEST_CASE("Aircraft source timestamps reject stale future and missing network fixes", "[adsb][freshness]") {
    AircraftTimeFixture f;
    auto row = f.row(); row[3] = f.utc - 121;
    f.network(row);
    CHECK(f.track().icao == 0x40621D);
    CHECK_FALSE(f.track().positionValid);
    CHECK(f.store.snapshot().tracks.empty());
    for (auto stamp : nlohmann::json::array({nullptr, 0, f.utc + 1, "now"})) {
        row = f.row(); row[3] = stamp; f.network(row);
        CHECK_FALSE(f.track().positionValid);
    }
    row = f.row(); row[4] = f.utc - 121; f.network(row);
    CHECK(f.track().icao == 0);
    row = f.row(); row[4] = nullptr; f.network(row);
    CHECK(f.track().icao == 0);
    row = f.row(); row[5] = row[6] = nullptr; f.network(row);
    CHECK(f.track().callsign == "NETWORK");
    CHECK_FALSE(f.track().positionValid);
}

TEST_CASE("Aircraft repeated network states do not renew cached telemetry", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.network(f.row());
    f.advance(60); f.network(f.row());
    CHECK(f.track().position.ageSec == 0);
    CHECK(f.track().altitude.ageSec == 60);
    CHECK(f.track().groundSpeed.ageSec == 60);
    f.advance(61); f.network(f.row());
    CHECK(f.track().positionValid);
    CHECK_FALSE(f.track().altitudeValid);
    CHECK_FALSE(f.track().groundSpeedValid);
    CHECK_FALSE(f.track().trackValid);
    CHECK_FALSE(f.track().verticalRateValid);
    CHECK(f.track().callsign.empty());
    auto row = f.row(); row[7] = 1001; row[9] = 101; f.network(row);
    CHECK(f.track().altitudeValid);
    CHECK(f.track().groundSpeedValid);
    CHECK(f.track().altitude.timeBasis == "last_contact");
    CHECK_FALSE(f.track().trackValid);
}

TEST_CASE("Aircraft missing network values are null while measured zero is valid", "[adsb][freshness]") {
    AircraftTimeFixture f;
    auto row = f.row();
    for (const int i : {7, 9, 10, 11, 13}) row[i] = nullptr;
    f.network(row);
    const auto missing = f.store.statusJson()["tracks"].at(0);
    for (const auto key : {"altFt", "gsKt", "trackDeg", "vrateFpm"}) CHECK(missing[key].is_null());
    row[7] = row[9] = row[10] = row[11] = 0;
    f.advance(1); row[3] = row[4] = f.utc; f.network(row);
    CHECK(f.track().altitudeValid);
    CHECK(f.track().groundSpeedValid);
    CHECK(f.track().trackValid);
    CHECK(f.track().verticalRateValid);
    CHECK(f.store.statusJson()["tracks"].at(0)["altFt"] == 0);
    row[7] = nullptr; row[13] = 50; f.network(row);
    CHECK(f.track().altitudeValid);
    CHECK(f.track().altitudeReference == "geometric");
    CHECK(f.track().altFt == Catch::Approx(50 * 3.28084));
    row[13] = nullptr; row[9] = -1; row[10] = "0"; row[11] = nullptr; f.network(row);
    CHECK_FALSE(f.track().altitudeValid);
    CHECK_FALSE(f.track().groundSpeedValid);
    CHECK_FALSE(f.track().trackValid);
    CHECK_FALSE(f.track().verticalRateValid);
}

TEST_CASE("Aircraft local velocity missing bits do not invent zero rate or north heading", "[adsb][freshness]") {
    AircraftTimeFixture f;
    auto bytes = hexFrame("8D485020994409940838175B284F");
    bytes[8] &= 0xF8; bytes[9] &= 3; // Vertical-rate code 0: unavailable.
    bytes = withAircraftCrc(bytes); f.store.ingestModeSFrame(bytes.data());
    CHECK(f.track().groundSpeedValid);
    CHECK(f.track().trackValid);
    CHECK_FALSE(f.track().verticalRateValid);
    bytes[8] |= 0; bytes[9] |= 4; // Code 1: measured zero, not unavailable.
    bytes = withAircraftCrc(bytes); f.store.ingestModeSFrame(bytes.data());
    CHECK(f.track().verticalRateValid);
    CHECK(f.track().verticalRateFpm == 0);
    bytes[5] &= 0xFC; bytes[6] = 1;
    bytes[7] &= 0x80; bytes[8] = uint8_t((bytes[8] & 0x1F) | 0x20);
    bytes = withAircraftCrc(bytes); f.store.ingestModeSFrame(bytes.data());
    CHECK(f.track().groundSpeedValid);
    CHECK(f.track().gsKt == 0);
    CHECK_FALSE(f.track().trackValid);
    f.advance(121);
    bytes[5] &= 0xFC; bytes[6] = 0; // No horizontal vector; vertical is still present.
    bytes = withAircraftCrc(bytes); f.store.ingestModeSFrame(bytes.data());
    CHECK_FALSE(f.track().groundSpeedValid);
    CHECK_FALSE(f.track().trackValid);
    CHECK(f.track().verticalRateValid);
}

TEST_CASE("Aircraft magnitude to frame to store path shares the injected clock", "[adsb][freshness][ppm]") {
    AircraftTimeFixture f;
    for (const char* text : {"8D40621D58C386435CC412692AD6", "8D40621D58C382D690C8AC2863A7"}) {
        const auto samples = synthesizePpm(hexFrame(text), 2.4e6, 31.37);
        f.store.processMagnitude(samples.data(), samples.size(), 2.4e6);
        f.advance(1);
    }
    REQUIRE(f.track().positionValid);
    CHECK(f.store.snapshot().localCrcOk == 2);
    CHECK(f.track().position.ageSec == 1);
    f.advance(120); f.identity();
    CHECK_FALSE(f.track().positionValid);
    CHECK(f.store.statusJson()["tracks"].empty());
}

TEST_CASE("Aircraft remote observation ages survive new replies across UTC corrections", "[adsb][freshness]") {
    AircraftTimeFixture f;
    auto row = f.row();
    const double sourceUtc = f.utc;
    f.network(row);
    f.advance(30); f.utc -= 86400;
    f.network(row);
    CHECK(f.track().positionValid);
    CHECK(f.track().position.ageSec == 30);
    CHECK(f.track().presence.ageSec == 30);
    f.advance(30); f.utc += 172800;
    row[4] = sourceUtc + 60; f.network(row);
    CHECK(f.track().presence.ageSec == 0);
    CHECK(f.track().position.ageSec == 60);
    CHECK(f.track().groundSpeed.ageSec == 60);
    f.advance(61); row[4] = sourceUtc + 121; f.network(row);
    CHECK(f.track().icao == 0x40621D);
    CHECK_FALSE(f.track().positionValid);
    CHECK(f.track().position.ageSec == 121);
    CHECK_FALSE(f.track().groundSpeedValid);
    row[3] = sourceUtc + 121; f.network(row);
    CHECK(f.track().positionValid);
    CHECK(f.track().position.ageSec == 0);
}

TEST_CASE("Aircraft CPR and presence ignore invalid and unpaired frames", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.frame("8D40621D58C386435CC412692AD6");
    f.advance(11); f.frame("8D40621D58C382D690C8AC2863A7");
    CHECK_FALSE(f.track().positionValid);
    f.advance(1); f.frame("8D40621D58C386435CC412692AD6");
    REQUIRE(f.track().positionValid);
    f.advance(121);
    auto invalid = hexFrame("8D40621D58C386435CC412692AD6"); invalid[7] ^= 1;
    f.store.ingestModeSFrame(invalid.data());
    CHECK(f.track().icao == 0);
    CHECK(f.store.snapshot().localCrcOk == 3);
}

TEST_CASE("Aircraft detail and map lists share nullable field and provenance contracts", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.position(); f.velocity(); f.identity();
    REQUIRE(AdsBTrackStore::trackJson(f.track()) == f.store.statusJson()["tracks"].at(0));
    f.advance(120); f.identity();
    f.advance(1); f.identity();
    const auto detail = AdsBTrackStore::trackJson(f.track());
    CHECK(detail["icao"] == "40621D");
    CHECK(detail["callsign"] == "KLM1023");
    for (const auto key : {"lat", "lon", "altFt", "gsKt", "trackDeg", "vrateFpm"}) CHECK(detail[key].is_null());
    for (const auto key : {"positionValid", "altitudeValid", "groundSpeedValid", "trackValid", "verticalRateValid"})
        CHECK(detail[key] == false);
    CHECK(detail["fields"]["position"]["ageSec"] == 121);
    CHECK(f.store.statusJson()["tracks"].empty());
}

TEST_CASE("Aircraft partial velocity reports age only the absent components", "[adsb][freshness]") {
    AircraftTimeFixture f;
    f.velocity();
    f.advance(100);
    auto bytes = hexFrame("8D485020994409940838175B284F");
    bytes[8] &= 0xF8; bytes[9] &= 3;
    bytes = withAircraftCrc(bytes); f.store.ingestModeSFrame(bytes.data());
    CHECK(f.track().groundSpeed.ageSec == 0);
    CHECK(f.track().verticalRate.ageSec == 100);
    CHECK(f.track().verticalRateFpm == -832);
    f.advance(21); f.store.ingestModeSFrame(bytes.data());
    CHECK(f.track().groundSpeedValid);
    CHECK_FALSE(f.track().verticalRateValid);
    CHECK(std::isnan(f.track().verticalRateFpm));
    bytes = hexFrame("8D485020994409940838175B284F");
    bytes[5] &= 0xFC; bytes[6] = 0;
    bytes = withAircraftCrc(bytes);
    f.advance(100); f.store.ingestModeSFrame(bytes.data());
    CHECK(f.track().verticalRateValid);
    CHECK(f.track().verticalRate.ageSec == 0);
    CHECK(f.track().groundSpeed.ageSec == 100);
    f.advance(21); f.store.ingestModeSFrame(bytes.data());
    CHECK_FALSE(f.track().groundSpeedValid);
    CHECK_FALSE(f.track().trackValid);
    CHECK(f.track().verticalRateValid);
}
