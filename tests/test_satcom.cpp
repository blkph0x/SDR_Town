#include "AptImageDecoder.h"
#include "Ax25AprsDecoder.h"
#include "SatcomAsyncLog.h"
#include "SatcomIqCursor.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
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
    size_t offset = 0;
    size_t pattern = 0;
    while (offset < pcm.size()) {
        const size_t count = std::min(chunkPattern[pattern % std::size(chunkPattern)],
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

TEST_CASE("APT decoder assembles grayscale lines from envelope", "[satcom][apt]")
{
    AptImageDecoder apt;
    const double rate = 11025.0;
    // Enough samples for at least one full 2080-pixel line (~5512.5 samples/line).
    const size_t n = static_cast<size_t>(rate * 3.0);
    std::vector<float> pcm(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / rate;
        // AM-like carrier with slow amplitude ramp (visible gradient).
        const double env = 0.2 + 0.6 *
            (0.5 + 0.5 * std::sin(2.0 * std::numbers::pi_v<double> * 2.0 * t));
        pcm[i] = static_cast<float>(
            env * std::sin(2.0 * std::numbers::pi_v<double> * 2400.0 * t));
    }
    bool gotLine = false;
    for (size_t offset = 0; offset < pcm.size();) {
        const size_t chunk = std::min<size_t>(512, pcm.size() - offset);
        if (apt.processAudio(pcm.data() + offset, chunk, rate)) gotLine = true;
        offset += chunk;
    }
    REQUIRE(gotLine);
    REQUIRE(apt.height() >= 1);
    REQUIRE(apt.width() == 2080);
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
