#include "Sgp4.h"
#include "SatObserverConfig.h"
#include "SatPassPlanner.h"
#include "TleStore.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "HttpGet.h"

#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <QCoreApplication>
#include <QDir>

TEST_CASE("Observer parses both hemispheres", "[satcom][pass]")
{
    double lat = 0, lon = 0;
    REQUIRE(SatObserverConfig::parseLatLonToken("33.8S", true, &lat));
    REQUIRE(lat == Catch::Approx(-33.8).margin(1e-6));
    REQUIRE(SatObserverConfig::parseLatLonToken("151.2E", false, &lon));
    REQUIRE(lon == Catch::Approx(151.2).margin(1e-6));
    REQUIRE(SatObserverConfig::parseLatLonToken("-33.87", true, &lat));
    REQUIRE(lat == Catch::Approx(-33.87).margin(1e-6));
    REQUIRE(SatObserverConfig::parseLatLonToken("151.21 W", false, &lon));
    REQUIRE(lon == Catch::Approx(-151.21).margin(1e-6));
    REQUIRE_FALSE(SatObserverConfig::parseLatLonToken("33.8E", true, &lat));
}

TEST_CASE("SGP4 parses TLE and produces finite state", "[satcom][pass][sgp4]")
{
    // Classic ISS-like sample TLE (epoch / elements illustrative)
    const std::string l1 =
        "1 25544U 98067A   21001.00000000  .00002182  00000-0  40864-4 0  9990";
    const std::string l2 =
        "2 25544  51.6456 247.4627 0003000  45.0000 315.0000 15.48900000200000";
    Sgp4::Elements el{};
    REQUIRE(Sgp4::parseTle(l1, l2, &el));
    REQUIRE(el.satnum == 25544);
    auto st = Sgp4::propagate(el, 0.0);
    REQUIRE(st.ok);
    REQUIRE(std::isfinite(st.r[0]));
    REQUIRE(std::isfinite(st.v[0]));
    const double rmag = std::sqrt(st.r[0] * st.r[0] + st.r[1] * st.r[1] + st.r[2] * st.r[2]);
    REQUIRE(rmag > 6500.0);
    REQUIRE(rmag < 7500.0);
}

TEST_CASE("Look angles Doppler sign is finite both hemispheres", "[satcom][pass]")
{
    const std::string l1 =
        "1 25544U 98067A   21001.00000000  .00002182  00000-0  40864-4 0  9990";
    const std::string l2 =
        "2 25544  51.6456 247.4627 0003000  45.0000 315.0000 15.48900000200000";
    Sgp4::Elements el{};
    REQUIRE(Sgp4::parseTle(l1, l2, &el));
    const double jd = el.epochJd + 10.0 / 1440.0;
    auto st = Sgp4::propagate(el, 10.0);
    REQUIRE(st.ok);

    double rE[3], vE[3];
    Sgp4::temeToEcef(jd, st.r, st.v, rE, vE);
    const double rmagT = std::sqrt(st.r[0] * st.r[0] + st.r[1] * st.r[1] + st.r[2] * st.r[2]);
    const double rmagE = std::sqrt(rE[0] * rE[0] + rE[1] * rE[1] + rE[2] * rE[2]);
    REQUIRE(rmagE == Catch::Approx(rmagT).margin(1e-6));

    for (double lat : {-33.8, 51.5, 0.0}) {
        for (double lon : {151.2, -0.1, -74.0}) {
            double elDeg = 0, az = 0, range = 0, rr = 0;
            Sgp4::lookAnglesTeme(jd, st.r, st.v, lat, lon, 50.0, &elDeg, &az, &range, &rr);
            REQUIRE(std::isfinite(elDeg));
            REQUIRE(std::isfinite(rr));
            REQUIRE(range > 0.0);
            REQUIRE(std::abs(rr) < 12.0);
            const double doppler = Sgp4::dopplerShiftHz(145.8e6, rr);
            REQUIRE(std::isfinite(doppler));
            if (rr < 0.0) REQUIRE(doppler > 0.0);
            if (rr > 0.0) REQUIRE(doppler < 0.0);
        }
    }
}

TEST_CASE("TLE loadFromFile parses 3-line sets", "[satcom][pass][tle]")
{
    const QString path = QDir::temp().filePath("sdr_town_tle_test.txt");
    {
        std::ofstream out(path.toStdString());
        out << "ISS (ZARYA)\n"
            << "1 25544U 98067A   21001.00000000  .00002182  00000-0  40864-4 0  9990\n"
            << "2 25544  51.6456 247.4627 0003000  45.0000 315.0000 15.48900000200000\n";
    }
    std::string err;
    REQUIRE(TleStore::instance().loadFromFile(path.toStdString(), &err));
    auto t = TleStore::instance().get(25544);
    REQUIRE(t.noradId == 25544);
    REQUIRE(t.line1.find("25544") != std::string::npos);
}

TEST_CASE("Live CelesTrak TLE download parses ISS 25544", "[satcom][tle][network]")
{
    int argc = 0;
    char* argv[] = {nullptr};
    std::unique_ptr<QCoreApplication> ownedApp;
    if (!QCoreApplication::instance()) {
        ownedApp = std::make_unique<QCoreApplication>(argc, argv);
        ownedApp->setApplicationName("SDR Town Test");
    }
    std::string err;
    std::string body;
    const bool got = httpGetUrl(
        "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle", &body, &err, 15000);
    if (!got) {
        WARN("CelesTrak stations fetch failed: " + err);
        return;
    }
    REQUIRE(body.find("ISS") != std::string::npos);
    REQUIRE(body.find("25544") != std::string::npos);

    REQUIRE(TleStore::instance().refreshFromNetwork(&err));
    auto iss = TleStore::instance().get(25544);
    REQUIRE(iss.noradId == 25544);
    REQUIRE(iss.line1.size() >= 68);
    REQUIRE(iss.line2.size() >= 68);
    REQUIRE(iss.line1[0] == '1');
    REQUIRE(iss.line2[0] == '2');
    Sgp4::Elements el{};
    REQUIRE(Sgp4::parseTle(iss.line1, iss.line2, &el));
    REQUIRE(el.satnum == 25544);
    auto st = Sgp4::propagate(el, 0.0);
    REQUIRE(st.ok);
    const double rmag = std::sqrt(st.r[0] * st.r[0] + st.r[1] * st.r[1] + st.r[2] * st.r[2]);
    REQUIRE(rmag > 6500.0);
    REQUIRE(rmag < 7500.0);
}

TEST_CASE("Public satcom JSON omits home lat/lon", "[satcom][pass]")
{
    SatObserverConfig o;
    o.latDeg = -33.87;
    o.lonDeg = 151.21;
    o.altM = 50;
    SatPassPlanner::instance().setObserver(o);
    const auto pub = SatPassPlanner::instance().publicStatusJson();
    REQUIRE(pub.contains("observer"));
    REQUIRE(pub["observer"].value("configured", false));
    REQUIRE_FALSE(pub["observer"].contains("latDeg"));
    REQUIRE_FALSE(pub["observer"].contains("lonDeg"));
    REQUIRE_FALSE(pub["observer"].contains("altM"));
    const auto full = SatPassPlanner::instance().statusJson();
    REQUIRE(full["observer"].value("latDeg", 0.0) == Catch::Approx(-33.87).margin(1e-6));
}

TEST_CASE("Pass planner AOS ordering with injected TLE", "[satcom][pass]")
{
    TleSet t;
    t.noradId = 25544;
    t.name = "ISS";
    t.line1 = "1 25544U 98067A   21001.00000000  .00002182  00000-0  40864-4 0  9990";
    t.line2 = "2 25544  51.6456 247.4627 0003000  45.0000 315.0000 15.48900000200000";
    TleStore::instance().upsert(t);

    SatObserverConfig o;
    o.latDeg = -33.87;
    o.lonDeg = 151.21;
    o.altM = 50;
    o.minElevationDeg = 5.0;
    SatPassPlanner::instance().setObserver(o);
    SatPassPlanner::instance().setCatalogueSelection({"iss"});
    SatPassPlanner::instance().refreshPasses(48.0);
    const auto snap = SatPassPlanner::instance().snapshot();
    // May be empty if epoch is far from now — still must be sorted when present
    for (size_t i = 1; i < snap.passes.size(); ++i)
        REQUIRE(snap.passes[i].aosUnix >= snap.passes[i - 1].aosUnix);
}
