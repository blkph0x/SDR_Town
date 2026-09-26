#include "InmarsatDemod.h"
#include "InmarsatMessageStore.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

std::vector<std::complex<float>> makeBpsk(
    double sampleRateHz,
    double symbolRateHz,
    size_t symbols,
    double phaseRadians = 0.37)
{
    const size_t samplesPerSymbol = static_cast<size_t>(
        std::llround(sampleRateHz / symbolRateHz));
    std::vector<std::complex<float>> iq;
    iq.reserve(symbols * samplesPerSymbol);
    const std::complex<float> rotation{
        static_cast<float>(std::cos(phaseRadians)),
        static_cast<float>(std::sin(phaseRadians))};

    uint32_t state = 0x51A7C3D9u;
    for (size_t symbol = 0; symbol < symbols; ++symbol) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const float level = (state & 1u) ? 1.0f : -1.0f;
        const std::complex<float> value = level * rotation;
        for (size_t i = 0; i < samplesPerSymbol; ++i) iq.push_back(value);
    }
    return iq;
}

std::vector<std::complex<float>> makePhaseNoise(size_t count)
{
    std::vector<std::complex<float>> iq;
    iq.reserve(count);
    uint32_t state = 0xC001D00Du;
    for (size_t i = 0; i < count; ++i) {
        state = state * 1664525u + 1013904223u;
        const double phase = 2.0 * std::numbers::pi_v<double> *
                             static_cast<double>(state) /
                             static_cast<double>(UINT32_MAX);
        state = state * 1664525u + 1013904223u;
        const double amplitude = 0.3 + 0.7 *
            static_cast<double>(state & 0xFFFFu) / 65535.0;
        iq.emplace_back(
            static_cast<float>(amplitude * std::cos(phase)),
            static_cast<float>(amplitude * std::sin(phase)));
    }
    return iq;
}

void feedInChunks(InmarsatDemod& demod,
                  const std::vector<std::complex<float>>& iq)
{
    const size_t pattern[] = {37, 4093, 211, 8191, 73, 1024};
    constexpr size_t patternCount = sizeof(pattern) / sizeof(pattern[0]);
    size_t offset = 0;
    size_t patternIndex = 0;
    while (offset < iq.size()) {
        const size_t count = std::min(pattern[patternIndex % patternCount],
                                      iq.size() - offset);
        demod.process(iq.data() + offset, count);
        offset += count;
        ++patternIndex;
    }
}

} // namespace

TEST_CASE("Inmarsat demod mode mapping is explicit", "[inmarsat]")
{
    CHECK(InmarsatDemod::modeFromBaud(600, false) ==
          InmarsatDemodMode::AeroMsk600);
    CHECK(InmarsatDemod::modeFromBaud(1200, false) ==
          InmarsatDemodMode::AeroMsk1200);
    CHECK(InmarsatDemod::modeFromBaud(8400, false) ==
          InmarsatDemodMode::AeroVoice8400);
    CHECK(InmarsatDemod::modeFromBaud(10500, false) ==
          InmarsatDemodMode::AeroOqpsk10500);
    CHECK(InmarsatDemod::modeFromBaud(1200, true) ==
          InmarsatDemodMode::EgcBpsk1200);
    CHECK(InmarsatDemod::symbolRate(InmarsatDemodMode::EgcBpsk1200) == 1200.0);
}

TEST_CASE("Inmarsat random phase noise cannot create carrier or frames", "[inmarsat]")
{
    constexpr double sampleRate = 1.2288e6;
    InmarsatDemod demod;
    size_t deliveredBlocks = 0;
    demod.setByteSink([&](const uint8_t*, size_t) { ++deliveredBlocks; });
    demod.reset(InmarsatDemodMode::EgcBpsk1200, sampleRate, 0.0);

    const auto noise = makePhaseNoise(300000);
    feedInChunks(demod, noise);
    const auto stats = demod.stats();

    CHECK_FALSE(stats.carrierDetected);
    CHECK_FALSE(stats.locked);
    CHECK(stats.framesOut == 0);
    CHECK(stats.rawBlocksOut == 0);
    CHECK(deliveredBlocks == 0);
}

TEST_CASE("Inmarsat coherent BPSK is diagnostic only and never a frame", "[inmarsat]")
{
    constexpr double sampleRate = 1.2288e6;
    constexpr double symbolRate = 1200.0;
    InmarsatDemod demod;
    size_t deliveredBlocks = 0;
    demod.setByteSink([&](const uint8_t*, size_t) { ++deliveredBlocks; });
    demod.reset(InmarsatDemodMode::EgcBpsk1200, sampleRate, 0.0);

    const auto iq = makeBpsk(sampleRate, symbolRate, 1100);
    feedInChunks(demod, iq);
    const auto stats = demod.stats();

    REQUIRE(stats.symbolsOut > 900);
    REQUIRE(stats.carrierDetected);
    REQUIRE(stats.quality > 0.60);
    REQUIRE(stats.rawBlocksOut >= 1);
    CHECK_FALSE(stats.locked);
    CHECK(stats.framesOut == 0);
    // The legacy byte callback is deliberately disabled until a validated
    // unique-word/FEC layer exists, so raw bits cannot reach ACARS/messages.
    CHECK(deliveredBlocks == 0);
}

TEST_CASE("Inmarsat retune resets physical-layer confidence", "[inmarsat]")
{
    constexpr double sampleRate = 1.2288e6;
    InmarsatDemod demod;
    demod.reset(InmarsatDemodMode::EgcBpsk1200, sampleRate, 0.0);
    const auto iq = makeBpsk(sampleRate, 1200.0, 700);
    feedInChunks(demod, iq);
    REQUIRE(demod.stats().carrierDetected);

    demod.setChannelOffset(2500.0);
    const auto resetStats = demod.stats();
    CHECK_FALSE(resetStats.carrierDetected);
    CHECK_FALSE(resetStats.locked);
    CHECK(resetStats.rawBlocksOut == 0);
    CHECK(resetStats.framesOut == 0);
}

TEST_CASE("Inmarsat map positions outlive the chronological message ring", "[inmarsat][map]")
{
    auto& store = InmarsatMessageStore::instance();
    store.clear();

    InmarsatMessage position;
    position.kind = InmarsatMsgKind::Acars;
    position.validated = true;
    position.hasPosition = true;
    position.aesId = 0x123456;
    position.latDeg = -34.4;
    position.lonDeg = 150.9;
    store.push(position);

    for (size_t i = 0; i < 750; ++i) {
        InmarsatMessage traffic;
        traffic.kind = InmarsatMsgKind::Su;
        traffic.validated = true;
        traffic.aesId = static_cast<uint32_t>(i + 1);
        store.push(std::move(traffic));
    }

    const auto messages = store.recent(500);
    REQUIRE(messages.size() == 500);
    CHECK(std::none_of(messages.begin(), messages.end(),
        [](const InmarsatMessage& message) { return message.hasPosition; }));
    const auto positions = store.recentPositions();
    REQUIRE(positions.size() == 1);
    CHECK(positions.front().aesId == 0x123456);
    CHECK(positions.front().latDeg == -34.4);
    CHECK(positions.front().lonDeg == 150.9);

    auto update = position;
    update.latDeg = -35.0;
    update.lonDeg = 151.5;
    store.push(update);
    const auto updated = store.recentPositions();
    REQUIRE(updated.size() == 1);
    CHECK(updated.front().latDeg == -35.0);
    CHECK(updated.front().lonDeg == 151.5);

    store.clear();
    CHECK(store.recentPositions().empty());
}
