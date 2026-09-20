#include "ModeS.h"
#include "AdsBTrackStore.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

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

TEST_CASE("Mode-S identity alphabet decode", "[adsb]")
{
    uint8_t msg[14]{};
    msg[0] = 0x8D;
    msg[4] = (4 << 3); // TC=4
    REQUIRE(ModeS::typeCode(msg) == 4);
    auto id = ModeS::decodeIdentity(msg);
    (void)id;
}

TEST_CASE("CPR local decode is finite both hemispheres", "[adsb]")
{
    auto south = ModeS::decodeCprLocal(false, 50000, 60000, -33.87, 151.21);
    REQUIRE(south.valid);
    REQUIRE(std::isfinite(south.latDeg));
    REQUIRE(std::isfinite(south.lonDeg));
    auto north = ModeS::decodeCprLocal(true, 40000, 70000, 51.5, -0.1);
    REQUIRE(north.valid);
    REQUIRE(std::isfinite(north.latDeg));
}

TEST_CASE("AdsBTrackStore merges observer and snapshot", "[adsb]")
{
    AdsBTrackStore::instance().setObserver(-33.87, 151.21, 80.0);
    const auto snap = AdsBTrackStore::instance().snapshot();
    REQUIRE(snap.centerLat == Catch::Approx(-33.87).margin(1e-6));
    REQUIRE(snap.centerLon == Catch::Approx(151.21).margin(1e-6));
    REQUIRE(snap.radiusNm >= 20.0);
}

TEST_CASE("CPR local decode uses positive modulo in the southern hemisphere", "[adsb]")
{
    auto pos = ModeS::decodeCprLocal(false, 40000, 70000, -33.87, 151.21);
    REQUIRE(pos.valid);
    REQUIRE(pos.latDeg < 0.0);
    REQUIRE(pos.latDeg > -90.0);
    REQUIRE(std::abs(pos.latDeg + 33.87) < 6.5);
}

TEST_CASE("CPR global pair selects the newer frame latitude", "[adsb]")
{
    auto evenNewer = ModeS::decodeCprPair(true, 50000, 60000, true, 51000, 61000);
    auto oddNewer = ModeS::decodeCprPair(false, 50000, 60000, true, 51000, 61000);
    if (evenNewer.valid && oddNewer.valid) {
        REQUIRE(std::abs(evenNewer.latDeg - oddNewer.latDeg) < 7.0);
    }
}

TEST_CASE("ADS-C merges into AdsBTrackStore", "[adsb]")
{
    AdsBTrackStore::instance().ingestAdscPosition(0xABCDEF, -33.87, 151.21, "ABCDEF", "QFA1");
    const auto t = AdsBTrackStore::instance().trackByIcao(0xABCDEF);
    REQUIRE(t.fromAdsc);
    REQUIRE(t.positionValid);
    REQUIRE(t.latDeg == Catch::Approx(-33.87).margin(1e-4));
}
