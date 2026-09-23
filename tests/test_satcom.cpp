#include "AptImageDecoder.h"
#include "Ax25AprsDecoder.h"
#include "SatcomAsyncLog.h"
#include "SatcomIqCursor.h"
#include "SatcomHostServices.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

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
    const auto lines = log.recentLines(20);
    REQUIRE(lines.size() <= 20);
    REQUIRE_FALSE(lines.empty());
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
        [&](size_t deviceIndex, std::string* error) {
            ++beginCount;
            selectedDevice = deviceIndex;
            if (error) error->clear();
            return true;
        };
    callbacks.endReceiverTakeover = [&]() { ++endCount; };
    callbacks.acquireAudioEngine = [](std::string* error) -> AudioEngine* {
        if (error) *error = "test has no audio device";
        return nullptr;
    };
    callbacks.publishSpectrum =
        [&](const std::vector<float>& power, double centerHz, double) {
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
    REQUIRE(host.beginReceiverTakeover(2, &error));
    CHECK(error.empty());
    CHECK(beginCount == 1);
    CHECK(selectedDevice == 2);

    host.publishSpectrum({-100.0f, -80.0f, -95.0f}, 145.8e6, 2.048e6);
    CHECK(spectrumCount == 1);
    CHECK(publishedCenter == 145.8e6);

    host.publishStatus("armed");
    CHECK(statusCount == 1);
    host.endReceiverTakeover();
    CHECK(endCount == 1);

    CHECK(host.acquireAudioEngine(&error) == nullptr);
    CHECK(error == "test has no audio device");

    host.clear();
    CHECK_FALSE(host.installed());
    error = "stale";
    CHECK(host.beginReceiverTakeover(9, &error));
    CHECK(error.empty());
}
