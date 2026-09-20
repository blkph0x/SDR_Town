#include <catch2/catch_test_macros.hpp>
#include "SdrplayDiversity.h"
#include <cmath>
#include <vector>

TEST_CASE("SdrplayDiversity mode names round-trip", "[sdrplay][diversity]") {
    CHECK(SdrplayDiversity::modeName(SdrplayDiversity::Mode::Off) == "off");
    CHECK(SdrplayDiversity::modeName(SdrplayDiversity::Mode::EqualGainSum) == "sum");
    CHECK(SdrplayDiversity::modeName(SdrplayDiversity::Mode::NullSteer) == "null");
    CHECK(SdrplayDiversity::modeFromName("sum") == SdrplayDiversity::Mode::EqualGainSum);
    CHECK(SdrplayDiversity::modeFromName("diversity") == SdrplayDiversity::Mode::EqualGainSum);
    CHECK(SdrplayDiversity::modeFromName("null") == SdrplayDiversity::Mode::NullSteer);
    CHECK(SdrplayDiversity::modeFromName("cancel") == SdrplayDiversity::Mode::NullSteer);
    CHECK(SdrplayDiversity::modeFromName("nope") == SdrplayDiversity::Mode::Off);
}

TEST_CASE("SdrplayDiversity equal-gain sum and null-steer", "[sdrplay][diversity]") {
    std::vector<std::complex<float>> a = {{1.0f, 0.0f}, {0.0f, 1.0f}, {0.5f, -0.5f}};
    std::vector<std::complex<float>> b = a; // identical → null should cancel; sum should double

    SdrplayDiversity::Config sumCfg;
    sumCfg.mode = SdrplayDiversity::Mode::EqualGainSum;
    sumCfg.phaseDeg = 0.0f;
    sumCfg.amplitudeB = 1.0f;
    auto summed = SdrplayDiversity::combine(a, b, sumCfg);
    REQUIRE(summed.size() == a.size());
    CHECK(std::abs(summed[0].real() - 2.0f) < 1e-5f);
    CHECK(std::abs(summed[1].imag() - 2.0f) < 1e-5f);

    SdrplayDiversity::Config nullCfg = sumCfg;
    nullCfg.mode = SdrplayDiversity::Mode::NullSteer;
    auto nulled = SdrplayDiversity::combine(a, b, nullCfg);
    REQUIRE(nulled.size() == a.size());
    for (const auto& s : nulled) {
        CHECK(std::abs(s.real()) < 1e-5f);
        CHECK(std::abs(s.imag()) < 1e-5f);
    }
}

TEST_CASE("SdrplayDiversity phase rotate on B", "[sdrplay][diversity]") {
    std::vector<std::complex<float>> a = {{1.0f, 0.0f}};
    std::vector<std::complex<float>> b = {{1.0f, 0.0f}};
    SdrplayDiversity::Config cfg;
    cfg.mode = SdrplayDiversity::Mode::EqualGainSum;
    cfg.phaseDeg = 90.0f;
    cfg.amplitudeB = 1.0f;
    auto out = SdrplayDiversity::combine(a, b, cfg);
    REQUIRE(out.size() == 1);
    // 1 + j*1
    CHECK(std::abs(out[0].real() - 1.0f) < 1e-4f);
    CHECK(std::abs(out[0].imag() - 1.0f) < 1e-4f);
}

TEST_CASE("SdrplayDiversity Off returns empty", "[sdrplay][diversity]") {
    std::vector<std::complex<float>> a = {{1.0f, 0.0f}};
    std::vector<std::complex<float>> b = {{1.0f, 0.0f}};
    SdrplayDiversity::Config cfg;
    CHECK(SdrplayDiversity::combine(a, b, cfg).empty());
}
