#include "AptImageDecoder.h"
#include "Ax25AprsDecoder.h"
#include "SatcomAsyncLog.h"
#include "DriverIoMutex.h"
#include "SatcomIqCursor.h"
#include "SatcomDoppler.h"
#include "Demod.h"
#include "SatcomHostServices.h"
#include "SatcomScannerEngine.h"
#include "SdrDeviceCandidate.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("Smart scan tiles cover boundaries and centre exclusions", "[satcom][smart]") {
    for (double rate : {256e3, 2.048e6, 10e6}) {
        const double lo = 240e6, hi = 243e6, bw = 25e3;
        const double stride = SatcomScanPolicy::tileStride(rate, bw);
        REQUIRE(stride > 0);
        double center = lo + stride * .5;
        const double first = center;
        std::vector<double> centers;
        do {
            centers.push_back(center);
            center = SatcomScanPolicy::nextTile(center, lo, hi, rate, bw);
            REQUIRE(centers.size() < 200);
        } while (center != first);
        for (double frequency = lo; frequency <= hi; frequency += 2500) {
            INFO("rate=" << rate << " frequency=" << frequency);
            REQUIRE(std::any_of(centers.begin(), centers.end(), [&](double c) {
                return SatcomScanPolicy::observable(frequency, c, rate, bw);
            }));
        }
    }
    CHECK(SatcomScanPolicy::tileStride(48e3, 25e3) == 0);
    CHECK_FALSE(SatcomScanPolicy::observable(250e6, 250e6, 2.048e6, 25e3));
}

TEST_CASE("Smart scan noise relative detection excludes DC and finds separate carriers", "[satcom][smart]") {
    constexpr double rate = 2.048e6, center = 250e6;
    std::mt19937 random(48);
    std::normal_distribution<float> noise(0, .0002f);
    std::vector<std::complex<float>> iq(8192);
    for (size_t i = 0; i < iq.size(); ++i) {
        iq[i] = std::complex<float>(noise(random), noise(random)) + std::complex<float>(.8f, .4f);
        iq[i] += .02f * std::polar(1.0f, float(2 * std::numbers::pi * -300e3 * i / rate));
        iq[i] += .01f * std::polar(1.0f, float(2 * std::numbers::pi * 250e3 * i / rate));
    }
    auto power = SatcomScanPolicy::spectrum(iq);
    REQUIRE(power.size() == 8192);
    auto found = SatcomScanPolicy::discover(power, center, rate, 249e6, 251e6, 25e3, 25e3, true, 12, -90);
    REQUIRE(found.size() == 2);
    CHECK(std::find(found.begin(), found.end(), center - 300e3) != found.end());
    CHECK(std::find(found.begin(), found.end(), center + 250e3) != found.end());
    CHECK(std::find(found.begin(), found.end(), center) == found.end());
    for (auto& s : iq) s = {noise(random), noise(random)};
    power = SatcomScanPolicy::spectrum(iq);
    CHECK(SatcomScanPolicy::discover(power, center, rate, 249e6, 251e6, 25e3, 25e3, true, 12, -90).empty());
    SatcomScanPolicy::Gate previouslyActive;
    previouslyActive.active = true;
    for (int block = 0; block < 40; ++block) {
        for (auto& s : iq) s = {noise(random), noise(random)};
        const auto m = SatcomScanPolicy::measure(SatcomScanPolicy::spectrum(iq), center, rate, center + 250e3, 25e3);
        REQUIRE(m.valid);
        CHECK_FALSE(previouslyActive.update(m, .02, true, 12, -90, .6));
    }
    CHECK_FALSE(previouslyActive.active);
    iq[20] = {std::numeric_limits<float>::quiet_NaN(), 0};
    CHECK(SatcomScanPolicy::spectrum(iq).empty());
}

TEST_CASE("Smart priorities preserve conversation and return after measured quiet", "[satcom][smart]") {
    SatcomScanPolicy::Gate gate;
    CHECK(gate.update({true, -65, -90}, .02, true, 12, -90, .6));
    CHECK(gate.active);
    for (int i = 0; i < 29; ++i) {
        CHECK_FALSE(gate.update({true, -90, -90}, .02, true, 12, -90, .6));
        CHECK(gate.active);
    }
    gate.update({true, -90, -90}, .021, true, 12, -90, .6);
    CHECK_FALSE(gate.active);
    std::vector<SatcomWatchStatus> states(3);
    for (int i = 0; i < 3; ++i) {
        states[i].channel = {250e6 + i * 25e3, "", 1, true};
        states[i].inBand = states[i].active = true;
    }
    CHECK(SatcomScanPolicy::selectForeground(states, states[1].channel.frequencyHz) == states[1].channel.frequencyHz);
    states[2].channel.priority = 2;
    CHECK(SatcomScanPolicy::selectForeground(states, states[1].channel.frequencyHz) == states[2].channel.frequencyHz);
    states[2].channel.enabled = false;
    CHECK(SatcomScanPolicy::selectForeground(states, states[1].channel.frequencyHz) == states[1].channel.frequencyHz);
    states[1].active = false;
    CHECK(SatcomScanPolicy::selectForeground(states, states[1].channel.frequencyHz) == states[0].channel.frequencyHz);
    states[0].inBand = false;
    CHECK(SatcomScanPolicy::selectForeground(states, 0) == 0);
    gate.update({}, .02, true, 12, -90, .6);
    CHECK_FALSE(gate.active);
}

TEST_CASE("Smart channel priorities survive configuration and reject invalid inputs", "[satcom][smart]") {
    auto config = SatcomScannerConfig::defaults();
    config.watchChannels = {{250e6, "Local", 2, true}, {251e6, "Muted", 0, false}};
    auto restored = SatcomScannerConfig::fromJson(config.toJson());
    REQUIRE(restored.watchChannels.size() == 2);
    CHECK(restored.watchChannels[0].priority == 2);
    CHECK(restored.watchChannels[0].label == "Local");
    CHECK_FALSE(restored.watchChannels[1].enabled);
    CHECK(restored.smartScan);
    auto malformed = config.toJson();
    malformed["watchChannels"].push_back(nullptr);
    malformed["watchChannels"].push_back({{"frequencyHz", "bad"}});
    malformed["watchChannels"].push_back({{"frequencyHz", 245e6}, {"priority", "High"}});
    CHECK(SatcomScannerConfig::fromJson(malformed).watchChannels.size() == config.watchChannels.size());
    CHECK(restored.autoSquelch);
    config.watchChannels[1].priority = 7;
    CHECK_FALSE(config.scanValidationError().empty());
    config.watchChannels.clear();
    config.squelchMarginDb = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(config.scanValidationError().empty());
}

TEST_CASE("Independent smart channel demods preserve clocks and reject the other voice", "[satcom][smart][dsp]") {
    constexpr double rate = 256000, center = 250e6;
    constexpr size_t block = 8192, blocks = 40;
    Demodulator left, right;
    std::vector<float> first, second;
    for (size_t b = 0; b < blocks; ++b) {
        std::vector<std::complex<float>> iq(block);
        for (size_t i = 0; i < block; ++i) {
            const double t = (b * block + i) / rate;
            const double a = 2 * std::numbers::pi * (-50e3) * t +
                2.5 * std::sin(2 * std::numbers::pi * 1000 * t);
            const double c = 2 * std::numbers::pi * 50e3 * t +
                (2500.0 / 1700) * std::sin(2 * std::numbers::pi * 1700 * t);
            iq[i] = .25f * std::polar(1.0f, static_cast<float>(a)) +
                    .35f * std::polar(1.0f, static_cast<float>(c));
        }
        double level = 0;
        auto a = left.demodulateToAudio(iq, rate, center, center - 50e3, DemodMode::NFM,
            level, 3000, -120, 1, 75, .96, 12500, 1536, 48000);
        auto c = right.demodulateToAudio(iq, rate, center, center + 50e3, DemodMode::NFM,
            level, 3000, -120, 1, 75, .96, 12500, 1536, 48000);
        REQUIRE(a.size() == 1536);
        REQUIRE(c.size() == 1536);
        first.insert(first.end(), a.begin(), a.end());
        second.insert(second.end(), c.begin(), c.end());
    }
    const auto amplitude = [](const auto& pcm, double frequency) {
        std::complex<double> sum{};
        for (size_t i = 4800; i < pcm.size(); ++i)
            sum += double(pcm[i]) * std::polar(1.0, -2 * std::numbers::pi * frequency * i / 48000);
        return std::abs(sum) / (pcm.size() - 4800);
    };
    REQUIRE(amplitude(first, 1000) > .001);
    REQUIRE(amplitude(second, 1700) > .001);
    CHECK(amplitude(first, 1700) < amplitude(first, 1000) * .05);
    CHECK(amplitude(second, 1000) < amplitude(second, 1700) * .05);
}

TEST_CASE("Satcom receive ranges validate and count the inclusive tune grid", "[satcom][range]") {
    auto config = SatcomScannerConfig::defaults();
    CHECK(config.lowHz == 243e6);
    CHECK(config.highHz == 270e6);
    CHECK(config.scanValidationError().empty());
    CHECK(config.scanPositionCount() == 1081);
    config.lowHz = 240e6; config.highHz = 322e6;
    CHECK(config.scanPositionCount() == 3281);
    config.highHz = config.lowHz;
    CHECK(config.scanPositionCount() == 1);
    config.highHz += 1.5 * config.stepHz;
    CHECK(config.scanPositionCount() == 2);
    SECTION("Invalid ranges fail closed") {
        config.lowHz = 400e6;
        CHECK_FALSE(config.scanValidationError().empty());
        CHECK(config.scanPositionCount() == 0);
    }
    SECTION("Nonfinite inputs fail closed") {
        for (auto* field : {&config.lowHz, &config.highHz, &config.stepHz, &config.bandwidthHz, &config.squelchDb}) {
            const double previous = *field;
            *field = std::numeric_limits<double>::quiet_NaN();
            CHECK_FALSE(config.scanValidationError().empty());
            *field = std::numeric_limits<double>::infinity();
            CHECK_FALSE(config.scanValidationError().empty());
            *field = previous;
        }
    }
    SECTION("Tune step dwell and mode are bounded") {
        config.stepHz = 0; CHECK_FALSE(config.scanValidationError().empty());
        config.stepHz = 100; CHECK(config.scanValidationError().empty());
        config.dwellMs = 19; CHECK_FALSE(config.scanValidationError().empty());
        config.dwellMs = 5001; CHECK_FALSE(config.scanValidationError().empty());
        config.dwellMs = 80; config.mode = "MUOS"; CHECK_FALSE(config.scanValidationError().empty());
    }
}

TEST_CASE("Satcom saved ranges migrate without changing operator settings", "[satcom][range]") {
    const nlohmann::json legacy = {
        {"lowHz", 420e6}, {"highHz", 430e6}, {"stepHz", 12.5e3}, {"bandwidthHz", 15e3},
        {"presets", {{{"name", "My range"}, {"lowHz", 250e6}, {"highHz", 251e6},
            {"stepHz", 5e3}, {"bandwidthHz", 15e3}, {"mode", "AM"}}}}
    };
    const auto config = SatcomScannerConfig::fromJson(legacy);
    CHECK(config.lowHz == 420e6);
    CHECK(config.highHz == 430e6);
    CHECK(config.bandwidthHz == 15e3);
    CHECK(config.presets.front().name == "My range");
    CHECK(config.presets.front().mode == "AM");
    CHECK(config.presets.front().dwellMs == 80);
    CHECK(config.presets.size() == SatcomScannerConfig::defaults().presets.size() + 1);
    const auto roundtrip = SatcomScannerConfig::fromJson(config.toJson());
    CHECK(roundtrip.toJson() == config.toJson());
    auto edited = roundtrip;
    edited.presets[1].lowHz = 245e6;
    edited.presets[1].dwellMs = 123;
    CHECK(SatcomScannerConfig::fromJson(edited.toJson()).presets[1].lowHz == 245e6);
    CHECK(SatcomScannerConfig::fromJson(edited.toJson()).presets[1].dwellMs == 123);
}

TEST_CASE("Inmarsat host preflight is explicit and fails closed", "[inmarsat][handover]") {
    auto& host = SatcomHostServices::instance();
    host.clear();
    struct Cleanup { ~Cleanup() { SatcomHostServices::instance().clear(); } } cleanup;
    REQUIRE(host.prepareInmarsatTakeover(1).ready); // Standalone callers still use the lease guard.
    int stops = 0;
    SatcomHostCallbacks callbacks;
    callbacks.prepareInmarsatTakeover = [&](size_t device, bool stop) -> InmarsatTakeoverResult {
        if (device == 1) return {true, false, {}}; // P25 is on a different receiver.
        if (!stop) return {false, true, "P25 configured"};
        ++stops;
        return {true, false, {}};
    };
    host.install(callbacks);
    REQUIRE(host.installed());
    CHECK(host.prepareInmarsatTakeover(1, true).ready);
    CHECK(stops == 0);
    auto result = host.prepareInmarsatTakeover(0);
    CHECK_FALSE(result.ready); CHECK(result.needsP25Confirmation); CHECK(stops == 0);
    CHECK(host.prepareInmarsatTakeover(0, true).ready); CHECK(stops == 1);
    callbacks.prepareInmarsatTakeover = [](size_t, bool) -> InmarsatTakeoverResult {
        throw std::runtime_error("host unavailable");
    };
    host.install(callbacks);
    result = host.prepareInmarsatTakeover(0, true);
    CHECK_FALSE(result.ready); CHECK_FALSE(result.needsP25Confirmation);
    CHECK(result.error.find("host unavailable") != std::string::npos);
    host.clear();
    CHECK_FALSE(host.installed());
}
namespace {

void appendCallsign(std::vector<uint8_t>& out, const char* call, int ssid, bool last)
{
    char padded[6] = {' ', ' ', ' ', ' ', ' ', ' '};
    for (int i = 0; call[i] && i < 6; ++i) padded[i] = call[i];
    for (int i = 0; i < 6; ++i)
        out.push_back(static_cast<uint8_t>((padded[i] << 1) & 0xFE));
    uint8_t ss = static_cast<uint8_t>(((ssid & 0x0F) << 1) | 0x60);
    if (last) ss |= 0x01;
    out.push_back(ss);
}

std::vector<uint8_t> makeUiFrame(const char* dest, const char* src, const std::string& info)
{
    std::vector<uint8_t> frame;
    appendCallsign(frame, dest, 0, false);
    appendCallsign(frame, src, 0, true);
    frame.push_back(0x03); // UI
    frame.push_back(0xF0); // no layer 3
    for (unsigned char c : info) frame.push_back(c);
    const uint16_t crc = Ax25AprsDecoder::crc16Fcs(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

std::vector<bool> bitsFromFrame(const std::vector<uint8_t>& frame)
{
    std::vector<bool> bits;
    auto pushByte = [&](uint8_t byte, bool stuff) {
        int ones = 0;
        for (int i = 0; i < 8; ++i) {
            const bool bit = ((byte >> i) & 1) != 0;
            bits.push_back(bit);
            if (stuff) {
                if (bit) {
                    ++ones;
                    if (ones == 5) {
                        bits.push_back(false);
                        ones = 0;
                    }
                } else {
                    ones = 0;
                }
            }
        }
    };
    // Multiple opening flags are representative of an actual TNC preamble and
    // allow every timing hypothesis to acquire before frame bytes begin.
    for (int i = 0; i < 4; ++i) pushByte(0x7E, false);
    for (uint8_t byte : frame) pushByte(byte, true);
    pushByte(0x7E, false);
    return bits;
}

std::vector<float> afskEncode(const std::vector<bool>& dataBits,
                              double sampleRate,
                              int leadingSamples = 0)
{
    // NRZI: data 0 = transition, data 1 = no transition. Tone: mark=1200, space=2200.
    std::vector<float> pcm(static_cast<size_t>(std::max(0, leadingSamples)), 0.0f);
    const int samplesPerBit = static_cast<int>(std::lround(sampleRate / 1200.0));
    bool level = true; // current NRZI level (true=mark)
    double phase = 0.0;
    const double twoPi = 2.0 * std::numbers::pi_v<double>;
    for (bool dataBit : dataBits) {
        if (!dataBit) level = !level;
        const double frequency = level ? 1200.0 : 2200.0;
        for (int i = 0; i < samplesPerBit; ++i) {
            pcm.push_back(static_cast<float>(0.7 * std::sin(phase)));
            phase += twoPi * frequency / sampleRate;
            if (phase >= twoPi) phase -= twoPi;
        }
    }
    return pcm;
}

std::vector<std::complex<float>> iqSequence(int first, int count)
{
    std::vector<std::complex<float>> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        out.emplace_back(static_cast<float>(first + i), static_cast<float>(-(first + i)));
    return out;
}

void putSyncA(std::vector<uint8_t>& words, size_t offset)
{
    constexpr uint8_t low = 11;
    constexpr uint8_t high = 244;
    std::fill_n(words.begin() + static_cast<std::ptrdiff_t>(offset), 39, low);
    for (int pulse = 0; pulse < 7; ++pulse) {
        const size_t begin = offset + 4 + static_cast<size_t>(pulse * 4);
        words[begin] = high;
        words[begin + 1] = high;
    }
}

void putSyncB(std::vector<uint8_t>& words, size_t offset)
{
    constexpr uint8_t low = 11;
    constexpr uint8_t high = 244;
    std::fill_n(words.begin() + static_cast<std::ptrdiff_t>(offset), 39, low);
    for (int pulse = 0; pulse < 7; ++pulse) {
        const size_t begin = offset + 4 + static_cast<size_t>(pulse * 5);
        words[begin] = high;
        words[begin + 1] = high;
    }
}

std::vector<uint8_t> makeAptLineWords()
{
    constexpr size_t lineWords = 2080;
    constexpr size_t halfLineWords = 1040;
    constexpr size_t syncWords = 39;
    constexpr size_t spaceWords = 47;
    constexpr size_t imageWords = 909;
    constexpr size_t telemetryWords = 45;
    static_assert(syncWords + spaceWords + imageWords + telemetryWords == halfLineWords);

    std::vector<uint8_t> words(lineWords, 32);
    putSyncA(words, 0);
    std::fill(words.begin() + static_cast<std::ptrdiff_t>(syncWords),
              words.begin() + static_cast<std::ptrdiff_t>(syncWords + spaceWords), 24);
    for (size_t i = 0; i < imageWords; ++i) {
        words[syncWords + spaceWords + i] =
            static_cast<uint8_t>(24 + (i * 210) / (imageWords - 1));
    }
    for (size_t i = 0; i < telemetryWords; ++i) {
        words[syncWords + spaceWords + imageWords + i] =
            static_cast<uint8_t>(20 + (i / 5) * 23);
    }

    putSyncB(words, halfLineWords);
    std::fill(words.begin() + static_cast<std::ptrdiff_t>(halfLineWords + syncWords),
              words.begin() + static_cast<std::ptrdiff_t>(halfLineWords + syncWords + spaceWords), 24);
    for (size_t i = 0; i < imageWords; ++i) {
        words[halfLineWords + syncWords + spaceWords + i] =
            static_cast<uint8_t>(234 - (i * 210) / (imageWords - 1));
    }
    for (size_t i = 0; i < telemetryWords; ++i) {
        words[halfLineWords + syncWords + spaceWords + imageWords + i] =
            static_cast<uint8_t>(227 - (i / 5) * 23);
    }
    return words;
}

std::vector<float> aptSubcarrierEncode(const std::vector<uint8_t>& words,
                                       double sampleRate,
                                       size_t leadingSamples = 0)
{
    constexpr double wordRate = 4160.0;
    constexpr double subcarrier = 2400.0;
    const size_t bodySamples = static_cast<size_t>(
        std::ceil((static_cast<double>(words.size()) + 8.0) * sampleRate / wordRate));
    std::vector<float> pcm(leadingSamples + bodySamples, 0.0f);
    double phase = 0.31; // deliberately not aligned to the decoder oscillator
    const double phaseIncrement = 2.0 * std::numbers::pi_v<double> * subcarrier / sampleRate;
    for (size_t n = 0; n < bodySamples; ++n) {
        const size_t wordIndex = std::min(
            words.size() - 1,
            static_cast<size_t>(std::floor(static_cast<double>(n) * wordRate / sampleRate)));
        const double normalized = static_cast<double>(words[wordIndex]) / 255.0;
        const double envelope = 0.05 + 0.90 * normalized;
        pcm[leadingSamples + n] = static_cast<float>(envelope * std::sin(phase));
        phase += phaseIncrement;
        if (phase >= 2.0 * std::numbers::pi_v<double>)
            phase -= 2.0 * std::numbers::pi_v<double>;
    }
    return pcm;
}

} // namespace

TEST_CASE("Deferred hardware proxy is eligible but demo stubs are rejected", "[satcom][device]")
{
    CHECK(SdrDeviceCandidate::isDeferredHardwareProxyLabel(
        "RTL-SDR placeholder (enable will try hardware)"));
    CHECK(SdrDeviceCandidate::canAttemptRealHardware(
        "RTL-SDR placeholder (enable will try hardware)"));
    CHECK(SdrDeviceCandidate::canAttemptRealHardware("SDRplay RSPdx"));
    CHECK_FALSE(SdrDeviceCandidate::canAttemptRealHardware("RTL-SDR (stub)"));
    CHECK_FALSE(SdrDeviceCandidate::canAttemptRealHardware("SDRplay RSPdx (stub)"));
    CHECK_FALSE(SdrDeviceCandidate::canAttemptRealHardware("generic placeholder"));
}

TEST_CASE("AX.25 FCS matches known payload", "[satcom][ax25]")
{
    const std::vector<uint8_t> payload = {'T', 'E', 'S', 'T'};
    const uint16_t crc = Ax25AprsDecoder::crc16Fcs(payload.data(), payload.size());
    std::vector<uint8_t> framed = payload;
    framed.push_back(static_cast<uint8_t>(crc & 0xFF));
    framed.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    REQUIRE(Ax25AprsDecoder::verifyFcs(framed));
    framed.back() ^= 0xFF;
    REQUIRE_FALSE(Ax25AprsDecoder::verifyFcs(framed));
}

TEST_CASE("AX.25 UI frame formats APRS text", "[satcom][ax25]")
{
    auto frame = makeUiFrame("APRS", "N0CALL", ">Hello satcom");
    REQUIRE(Ax25AprsDecoder::verifyFcs(frame));
    const std::string text = Ax25AprsDecoder::formatUiFrame(frame);
    REQUIRE(text.find("APRS>N0CALL") != std::string::npos);
    REQUIRE(text.find("Hello satcom") != std::string::npos);
}

TEST_CASE("AX.25 AFSK1200 decodes a synthetic APRS frame", "[satcom][ax25]")
{
    const auto frame = makeUiFrame("CQ", "NOAA", ":TEST PACKET");
    const auto bits = bitsFromFrame(frame);
    const double rate = 48000.0;
    const auto pcm = afskEncode(bits, rate, static_cast<int>(rate * 0.05));

    Ax25AprsDecoder decoder;
    const auto output = decoder.processAudio(pcm.data(), pcm.size(), rate);
    REQUIRE_FALSE(output.empty());
    REQUIRE(std::any_of(output.begin(), output.end(), [](const std::string& text) {
        return text.find("TEST PACKET") != std::string::npos;
    }));
}

TEST_CASE("AX.25 AFSK1200 survives arbitrary chunk and sample alignment", "[satcom][ax25]")
{
    const auto frame = makeUiFrame("APRS", "VK2ABC", ">CHUNKED");
    const auto bits = bitsFromFrame(frame);
    const double rate = 48000.0;
    const auto pcm = afskEncode(bits, rate, 17); // deliberately not a symbol boundary

    Ax25AprsDecoder decoder;
    std::vector<std::string> output;
    const size_t chunkPattern[] = {37, 511, 83, 1024, 19, 257};
    constexpr size_t chunkPatternCount = sizeof(chunkPattern) / sizeof(chunkPattern[0]);
    size_t offset = 0;
    size_t pattern = 0;
    while (offset < pcm.size()) {
        const size_t count = std::min(chunkPattern[pattern % chunkPatternCount],
                                      pcm.size() - offset);
        auto completed = decoder.processAudio(pcm.data() + offset, count, rate);
        output.insert(output.end(), completed.begin(), completed.end());
        offset += count;
        ++pattern;
    }
    REQUIRE(std::any_of(output.begin(), output.end(), [](const std::string& text) {
        return text.find("CHUNKED") != std::string::npos;
    }));
}

TEST_CASE("Satcom IQ cursor emits overlapping snapshots exactly once", "[satcom][iq]")
{
    SatcomIqCursor cursor;
    const auto first = cursor.consume(iqSequence(0, 8), 100, 108, 1);
    REQUIRE(first.discontinuity);
    REQUIRE(first.samples.size() == 8);
    REQUIRE(first.startAbsolute == 100);
    REQUIRE(first.endAbsolute == 108);

    const auto overlap = cursor.consume(iqSequence(4, 8), 104, 112, 1);
    REQUIRE_FALSE(overlap.discontinuity);
    REQUIRE(overlap.samples.size() == 4);
    CHECK(overlap.samples.front().real() == 8.0f);
    CHECK(overlap.samples.back().real() == 11.0f);
    CHECK(overlap.startAbsolute == 108);
    CHECK(overlap.endAbsolute == 112);

    const auto duplicate = cursor.consume(iqSequence(4, 8), 104, 112, 1);
    CHECK(duplicate.samples.empty());
    CHECK_FALSE(duplicate.discontinuity);
}

TEST_CASE("Satcom IQ cursor reports ring gaps and stream epochs", "[satcom][iq]")
{
    SatcomIqCursor cursor;
    REQUIRE(cursor.consume(iqSequence(0, 4), 10, 14, 7).discontinuity);

    const auto gap = cursor.consume(iqSequence(20, 4), 30, 34, 7);
    REQUIRE(gap.discontinuity);
    REQUIRE(gap.samples.size() == 4);
    CHECK(gap.startAbsolute == 30);
    CHECK(gap.endAbsolute == 34);

    const auto epoch = cursor.consume(iqSequence(40, 4), 40, 44, 8);
    REQUIRE(epoch.discontinuity);
    REQUIRE(epoch.samples.size() == 4);
    CHECK(epoch.streamEpoch == 8);
}

TEST_CASE("Satellite Doppler translates both signs without phase jumps", "[satcom][doppler]")
{
    constexpr double rate = 192000.0;
    constexpr double nominal = 145800000.0;
    constexpr double twoPi = 2.0 * std::numbers::pi;
    SatcomDoppler whole, split;
    double phase = 0.0;
    uint64_t position = 0;
    for (double offset : {3100.0, -1700.0, 0.0, 8100.0, -8900.0}) {
        std::vector<std::complex<float>> signal(16387);
        for (auto& sample : signal) {
            sample = static_cast<std::complex<float>>(std::polar(0.5, phase));
            phase = std::remainder(phase + twoPi * (offset + 1500.0) / rate, twoPi);
        }
        auto expected = signal;
        REQUIRE(whole.translate(expected, rate, nominal, nominal + offset, nominal, 3000));
        std::vector<std::complex<float>> actual;
        for (size_t start = 0; start < signal.size();) {
            const size_t count = std::min<size_t>(511, signal.size() - start);
            std::vector<std::complex<float>> part(signal.begin() + start, signal.begin() + start + count);
            REQUIRE(split.translate(part, rate, nominal, nominal + offset, nominal, 3000));
            actual.insert(actual.end(), part.begin(), part.end());
            start += count;
        }
        REQUIRE(actual == expected);
        double maxError = 0.0;
        for (auto sample : actual) {
            const auto ideal = std::polar(0.5, twoPi * 1500.0 * position++ / rate);
            maxError = std::max(maxError, std::abs(std::complex<double>(sample) - ideal));
        }
        CHECK(maxError < 1e-6);
    }
}

TEST_CASE("Satellite Doppler rejects uncaptured channels without mutating state", "[satcom][doppler]")
{
    SatcomDoppler mixer, reference;
    std::vector<std::complex<float>> iq(8193, {0.5f, 0.25f});
    auto expected = iq;
    REQUIRE(mixer.translate(iq, 48000, 100000, 101000, 100000, 3000));
    REQUIRE(reference.translate(expected, 48000, 100000, 101000, 100000, 3000));
    const auto before = iq;
    CHECK_FALSE(mixer.translate(iq, 48000, 100000, 123000, 100000, 3000));
    CHECK_FALSE(mixer.translate(iq, 48000, 100000, 100000, 123000, 3000));
    CHECK_FALSE(mixer.translate(iq, 0, 100000, 100000, 100000, 3000));
    CHECK_FALSE(mixer.translate(iq, 48000, 100000,
                                std::numeric_limits<double>::quiet_NaN(), 100000, 3000));
    CHECK(iq == before);
    REQUIRE(mixer.translate(iq, 48000, 100000, 99000, 100000, 3000));
    REQUIRE(reference.translate(expected, 48000, 100000, 99000, 100000, 3000));
    CHECK(iq == expected);
    mixer.reset();
    iq.assign(8193, {0.5f, 0.25f});
    expected = iq;
    REQUIRE(mixer.translate(iq, 48000, 100000, 100000, 100000, 3000));
    CHECK(iq == expected);
}

TEST_CASE("Satellite sideband decoder keeps its clock through Doppler updates", "[satcom][doppler][hf]")
{
    constexpr double rate = 192000.0, nominal = 145800000.0;
    constexpr double twoPi = 2.0 * std::numbers::pi;
    SatcomDoppler mixer;
    Demodulator tracked, reference;
    double rfPhase = 0.0, voicePhase = 0.0, rms = 0.0;
    uint64_t epoch = 0, nextSample = 0;
    double maxError = 0.0;
    size_t totalAudio = 0;
    // Include blocks below the scanner's former 1024-sample discard threshold.
    const size_t sizes[] = {17, 511, 8192, 71, 4096};
    for (size_t block = 0; block < 80; ++block) {
        const double offset = block < 20 ? 3100.0 : (block < 50 ? -1700.0 : 0.0);
        std::vector<std::complex<float>> iq(sizes[block % 5]), ideal(iq.size());
        for (size_t i = 0; i < iq.size(); ++i) {
            iq[i] = static_cast<std::complex<float>>(std::polar(0.2, rfPhase));
            ideal[i] = static_cast<std::complex<float>>(std::polar(0.2, voicePhase));
            rfPhase = std::remainder(rfPhase + twoPi * (offset + 1500.0) / rate, twoPi);
            voicePhase = std::remainder(voicePhase + twoPi * 1500.0 / rate, twoPi);
        }
        REQUIRE(mixer.translate(iq, rate, nominal, nominal + offset, nominal, 3000));
        FmMultiplexBlock data;
        const auto actual = tracked.demodulateToAudio(iq, rate, nominal, nominal,
            DemodMode::USB, rms, 3000, -120, 1, 75, .96, 3000, 0, 48000,
            std::numeric_limits<double>::quiet_NaN(), true, &data, nominal);
        const auto expected = reference.demodulateToAudio(ideal, rate, nominal, nominal,
            DemodMode::USB, rms, 3000, -120, 1, 75, .96, 3000, 0, 48000);
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i)
            maxError = std::max(maxError, double(std::abs(actual[i] - expected[i])));
        totalAudio += actual.size();
        if (!data.samples.empty()) {
            if (nextSample) {
                CHECK_FALSE(data.discontinuity);
                CHECK(data.epoch == epoch);
            }
            CHECK(data.firstSample == nextSample);
            CHECK(data.targetHz == nominal);
            epoch = data.epoch;
            nextSample += data.samples.size();
        }
    }
    CHECK(totalAudio > 48000);
    CHECK(nextSample > 0);
    CHECK(maxError < 1e-4);
}

TEST_CASE("APT decoder recovers synchronized NOAA line from 2400 Hz subcarrier", "[satcom][apt]")
{
    const auto words = makeAptLineWords();
    const double rate = 48000.0;
    const auto pcm = aptSubcarrierEncode(words, rate, 23);

    AptImageDecoder apt;
    bool gotLine = false;
    const size_t chunkPattern[] = {113, 4096, 71, 997, 2048, 29};
    constexpr size_t chunkPatternCount = sizeof(chunkPattern) / sizeof(chunkPattern[0]);
    size_t offset = 0;
    size_t pattern = 0;
    while (offset < pcm.size()) {
        const size_t count = std::min(chunkPattern[pattern % chunkPatternCount],
                                      pcm.size() - offset);
        if (apt.processAudio(pcm.data() + offset, count, rate)) gotLine = true;
        offset += count;
        ++pattern;
    }

    REQUIRE(gotLine);
    REQUIRE(apt.height() == 1);
    REQUIRE(apt.width() == 2080);
    REQUIRE(apt.syncCount() >= 1);
    REQUIRE(apt.lastSyncScore() > 0.65);
    REQUIRE(apt.lastSyncBScore() > 0.60);
    REQUIRE(apt.lines().front().size() == 2080);
    CHECK(apt.lines().front()[0] < apt.lines().front()[5]);
    CHECK(apt.lines().front()[100] < apt.lines().front()[900]);
    CHECK(apt.lines().front()[1140] > apt.lines().front()[1950]);

    const auto outputPath = std::filesystem::temp_directory_path() /
                            "sdr_town_noaa_apt_synthetic.pgm";
    std::error_code ignored;
    std::filesystem::remove(outputPath, ignored);
    REQUIRE(apt.writePgm(outputPath.string()));
    REQUIRE(apt.lastPath() == outputPath.string());
    REQUIRE(std::filesystem::file_size(outputPath) > 2080);
    std::filesystem::remove(outputPath, ignored);
}

TEST_CASE("APT decoder does not invent lines without NOAA sync", "[satcom][apt]")
{
    const double rate = 48000.0;
    std::vector<uint8_t> flatWords(2080 * 3, 128);
    const auto pcm = aptSubcarrierEncode(flatWords, rate, 11);

    AptImageDecoder apt;
    bool gotLine = false;
    for (size_t offset = 0; offset < pcm.size();) {
        const size_t count = std::min<size_t>(733, pcm.size() - offset);
        gotLine = apt.processAudio(pcm.data() + offset, count, rate) || gotLine;
        offset += count;
    }
    CHECK_FALSE(gotLine);
    CHECK(apt.height() == 0);
    CHECK(apt.syncCount() == 0);
}

TEST_CASE("Satcom async log drops oldest under flood", "[satcom][log]")
{
    SatcomLog::AsyncLog log(64);
    for (int i = 0; i < 200; ++i) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "evt-%d", i);
        REQUIRE(log.tryPush(SatcomLog::EventType::Info, 145.8e6, buffer));
    }
    REQUIRE(log.eventsDropped() > 0);
    log.start();
    log.stop(); // Joining drains the retained POD events and formats on the writer.
    CHECK(log.eventsWritten() + log.eventsDropped() == 200);
    const auto lines = log.recentLines(20);
    REQUIRE(lines.size() <= 20);
    REQUIRE_FALSE(lines.empty());
}

TEST_CASE("Satcom log accounts for concurrent UI and worker producers", "[satcom][log]") {
    SatcomLog::AsyncLog log(64);
    log.start();
    auto producer = [&](double hz, const char* payload) {
        for (int i = 0; i < 5000; ++i) log.tryPush(SatcomLog::EventType::Info,hz,payload);
    };
    std::thread a(producer,145.8e6,"producer-alpha"), b(producer,146.8e6,"producer-beta"),
        c(producer,147.8e6,"producer-gamma");
    a.join(); b.join(); c.join(); log.stop();
    CHECK(log.eventsWritten() + log.eventsDropped() == 15000);
    for (const auto& line : log.recentLines())
        CHECK((line == "INFO 145.80000MHz producer-alpha" || line == "INFO 146.80000MHz producer-beta" ||
            line == "INFO 147.80000MHz producer-gamma"));
}

TEST_CASE("Driver IO admission preserves queued command order", "[satcom][log][ownership]") {
    DriverIoMutex mutex;
    std::vector<int> order;
    std::vector<std::thread> threads;
    bool admitted = true;
    mutex.lock();
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i] { std::lock_guard lock(mutex); order.push_back(i); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (mutex.waiting() != static_cast<uint64_t>(i + 1) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (mutex.waiting() != static_cast<uint64_t>(i + 1)) { admitted = false; break; }
    }
    mutex.unlock();
    for (auto& thread : threads) thread.join();
    REQUIRE(admitted);
    CHECK(order == std::vector<int>{0,1,2,3});
    CHECK(mutex.waiting() == 0);
}

TEST_CASE("Driver IO table isolates different device keys", "[satcom][log][ownership]") {
    DriverIoMutexTable table;
    auto radioA = table.mutexFor("usb:sdrplay:rspdx");
    auto radioB = table.mutexFor("usb:rtlsdr:00000001");
    REQUIRE(radioA);
    REQUIRE(radioB);
    REQUIRE(radioA.get() != radioB.get());
    REQUIRE(table.mutexFor("usb:sdrplay:rspdx").get() == radioA.get());
    REQUIRE(table.mutexFor("").get() == table.mutexFor("unkeyed").get());

    radioA->lock();
    CHECK(radioB->try_lock());
    radioB->unlock();
    CHECK_FALSE(radioA->try_lock());
    radioA->unlock();
}

TEST_CASE("Driver IO try_lock does not take a ticket or barge waiters", "[satcom][log][ownership]") {
    DriverIoMutex mutex;
    CHECK(mutex.try_lock());
    CHECK_FALSE(mutex.try_lock());
    mutex.unlock();
    mutex.lock();
    CHECK_FALSE(mutex.try_lock());
    std::atomic<bool> waiterReady{false};
    std::thread waiter([&] {
        waiterReady.store(true, std::memory_order_release);
        std::lock_guard lock(mutex);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!waiterReady.load(std::memory_order_acquire) || mutex.waiting() != 1) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(mutex.waiting() == 1);
    CHECK_FALSE(mutex.try_lock());
    mutex.unlock();
    waiter.join();
    CHECK(mutex.try_lock());
    mutex.unlock();
}

TEST_CASE("Satcom host services forward MainWindow ownership", "[satcom][host]")
{
    auto& host = SatcomHostServices::instance();
    host.clear();
    struct Reset {
        SatcomHostServices& host;
        ~Reset() { host.clear(); }
    } reset{host};

    int beginCount = 0;
    int endCount = 0;
    int spectrumCount = 0;
    int statusCount = 0;
    size_t selectedDevice = static_cast<size_t>(-1);
    double publishedCenter = 0.0;

    SatcomHostCallbacks callbacks;
    callbacks.beginReceiverTakeover =
        [&](const DeviceOwnership::Token& token, std::string* error) {
            ++beginCount;
            selectedDevice = token.index;
            if (error) error->clear();
            return true;
        };
    callbacks.endReceiverTakeover = [&](const DeviceOwnership::Token&, bool) { ++endCount; };
    callbacks.acquireAudioEngine = [](std::string* error) -> AudioEngine* {
        if (error) *error = "test has no audio device";
        return nullptr;
    };
    callbacks.publishSpectrum =
        [&](const DeviceOwnership::Token&, const std::vector<float>& power, double centerHz, double) {
            ++spectrumCount;
            publishedCenter = centerHz;
            CHECK(power.size() == 3);
        };
    callbacks.publishStatus = [&](const std::string& status) {
        ++statusCount;
        CHECK(status == "armed");
    };
    host.install(std::move(callbacks));

    REQUIRE(host.installed());
    std::string error;
    const DeviceOwnership::Token token{2, 1, 1, DeviceOwnership::Owner::Satcom, "host-test"};
    REQUIRE(host.beginReceiverTakeover(token, &error));
    CHECK(error.empty());
    CHECK(beginCount == 1);
    CHECK(selectedDevice == 2);

    host.publishSpectrum(token, {-100.0f, -80.0f, -95.0f}, 145.8e6, 2.048e6);
    CHECK(spectrumCount == 1);
    CHECK(publishedCenter == 145.8e6);

    host.publishStatus("armed");
    CHECK(statusCount == 1);
    host.endReceiverTakeover(token);
    CHECK(endCount == 1);

    CHECK(host.acquireAudioEngine(&error) == nullptr);
    CHECK(error == "test has no audio device");

    host.clear();
    CHECK_FALSE(host.installed());
    error = "stale";
    CHECK(host.beginReceiverTakeover(token, &error));
    CHECK(error.empty());
}

TEST_CASE("Saved satellite auto-capture does not arm until this session", "[satcom][dec-0205]") {
    SatcomScannerConfig saved = SatcomScannerConfig::defaults();
    saved.autoCapture = true;
    saved.monitorAudio = false;
    saved.save("qa-dec0205-autocapture");
    SatcomScannerEngine engine("qa-dec0205-autocapture");
    CHECK_FALSE(engine.autoCaptureEnabled());
    engine.setAutoCaptureEnabled(true);
    CHECK(engine.autoCaptureEnabled());
    engine.setAutoCaptureEnabled(false);
    CHECK_FALSE(engine.autoCaptureEnabled());
}

TEST_CASE("Default satcom hub engine starts with auto-capture off", "[satcom][dec-0205]") {
    CHECK_FALSE(SatcomScannerEngine::instance().autoCaptureEnabled());
}
