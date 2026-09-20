#include "InmarsatAcars.h"
#include "InmarsatBandPlan.h"
#include "InmarsatDemod.h"
#include "InmarsatMessageStore.h"
#include "InmarsatVoice.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <cmath>
#include <vector>

TEST_CASE("Inmarsat band plans load or fallback", "[inmarsat]")
{
    InmarsatBandPlanStore::instance().reload(nullptr);
    const auto& plans = InmarsatBandPlanStore::instance().plans();
    REQUIRE_FALSE(plans.empty());
    const auto* p = InmarsatBandPlanStore::instance().findById("4f2");
    REQUIRE(p != nullptr);
    REQUIRE_FALSE(p->channels.empty());
}

TEST_CASE("Inmarsat ACARS ADS-C and C-assign parse", "[inmarsat]")
{
    double lat = 0, lon = 0;
    REQUIRE(InmarsatAcars::tryParseAdscPosition("POS 3352.10S 15112.40E END", &lat, &lon));
    REQUIRE(lat < 0);
    REQUIRE(lon > 0);

    uint32_t aes = 0;
    double rx = 0, tx = 0;
    REQUIRE(InmarsatAcars::tryParseCassign(
        "C-ASSIGN AES=ABCDEF RX=1544.500 TX=1645.000", &aes, &rx, &tx));
    REQUIRE(aes == 0xABCDEFu);
    REQUIRE(rx == Catch::Approx(1544.5e6).margin(1.0));

    auto m = InmarsatAcars::parseAcarsText("Label H1 ADS-C -33.8700 151.2100 AES ABCDEF");
    REQUIRE(m.hasPosition);
    REQUIRE(m.aesId != 0);
}

TEST_CASE("Inmarsat demod processes synthetic IQ", "[inmarsat]")
{
    InmarsatDemod dem;
    dem.reset(InmarsatDemodMode::AeroOqpsk10500, 2.048e6, 0.0);
    size_t bytes = 0;
    dem.setByteSink([&](const uint8_t*, size_t n) { bytes += n; });
    std::vector<std::complex<float>> iq(8192);
    for (size_t i = 0; i < iq.size(); ++i) {
        const float ph = static_cast<float>(i) * 0.15f;
        iq[i] = {std::cos(ph), std::sin(ph)};
    }
    dem.process(iq.data(), iq.size());
    const auto st = dem.stats();
    REQUIRE(st.bitsOut >= 0);
    (void)bytes;
}

TEST_CASE("Inmarsat voice backend smoke", "[inmarsat]")
{
    InmarsatVoice v;
    uint8_t frame[12]{};
    int16_t pcm[160]{};
    const int errs = v.decodeFrame(frame, pcm);
    if (v.backendAvailable()) {
        REQUIRE(errs >= 0);
    } else {
        REQUIRE(errs == -1);
    }
}

TEST_CASE("Inmarsat message store ring", "[inmarsat]")
{
    InmarsatMessageStore::instance().clear();
    InmarsatMessage m;
    m.kind = InmarsatMsgKind::Acars;
    m.text = "hello";
    InmarsatMessageStore::instance().push(m);
    auto recent = InmarsatMessageStore::instance().recent(10);
    REQUIRE(recent.size() == 1);
    REQUIRE(recent[0].text == "hello");
}
