#include "AptImageDecoder.h"
#include "Ax25AprsDecoder.h"
#include "InmarsatDemod.h"
#include "ModeS.h"
#include "SatcomIqCursor.h"
#include "SdrplayProfile.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace {

int failures = 0;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

std::vector<uint8_t> hexFrame(const std::string& text) {
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

void appendCallsign(std::vector<uint8_t>& out, const char* call, bool last) {
    char padded[6] = {' ', ' ', ' ', ' ', ' ', ' '};
    for (int i = 0; call[i] && i < 6; ++i) padded[i] = call[i];
    for (char ch : padded) out.push_back(static_cast<uint8_t>(ch << 1));
    out.push_back(static_cast<uint8_t>(0x60 | (last ? 1 : 0)));
}

std::vector<uint8_t> makeAprsFrame() {
    std::vector<uint8_t> frame;
    appendCallsign(frame, "APRS", false);
    appendCallsign(frame, "N0CALL", true);
    frame.push_back(0x03);
    frame.push_back(0xF0);
    for (const unsigned char ch : std::string(">SDR TOWN HARDENED")) frame.push_back(ch);
    const uint16_t crc = Ax25AprsDecoder::crc16Fcs(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

std::vector<bool> hdlcBits(const std::vector<uint8_t>& frame) {
    std::vector<bool> bits;
    auto appendByte = [&](uint8_t byte, bool stuff) {
        int ones = 0;
        for (int i = 0; i < 8; ++i) {
            const bool bit = ((byte >> i) & 1u) != 0;
            bits.push_back(bit);
            if (!stuff) continue;
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
    };
    for (int i = 0; i < 4; ++i) appendByte(0x7E, false);
    for (uint8_t byte : frame) appendByte(byte, true);
    appendByte(0x7E, false);
    return bits;
}

std::vector<float> afskEncode(const std::vector<bool>& bits) {
    std::vector<float> pcm(17, 0.0f); // deliberately off the symbol boundary
    bool mark = true;
    double phase = 0.31;
    for (const bool bit : bits) {
        if (!bit) mark = !mark;
        const double frequency = mark ? 1200.0 : 2200.0;
        for (int sample = 0; sample < 40; ++sample) {
            pcm.push_back(static_cast<float>(0.7 * std::sin(phase)));
            phase += 2.0 * std::numbers::pi_v<double> * frequency / 48000.0;
            if (phase >= 2.0 * std::numbers::pi_v<double>)
                phase -= 2.0 * std::numbers::pi_v<double>;
        }
    }
    return pcm;
}

void putSyncA(std::vector<uint8_t>& words, size_t offset) {
    std::fill_n(words.begin() + static_cast<std::ptrdiff_t>(offset), 39, uint8_t{11});
    for (int pulse = 0; pulse < 7; ++pulse) {
        const size_t begin = offset + 4 + static_cast<size_t>(pulse * 4);
        words[begin] = 244;
        words[begin + 1] = 244;
    }
}

void putSyncB(std::vector<uint8_t>& words, size_t offset) {
    std::fill_n(words.begin() + static_cast<std::ptrdiff_t>(offset), 39, uint8_t{11});
    for (int pulse = 0; pulse < 7; ++pulse) {
        const size_t begin = offset + 4 + static_cast<size_t>(pulse * 5);
        words[begin] = 244;
        words[begin + 1] = 244;
    }
}

std::vector<float> makeAptAudio() {
    std::vector<uint8_t> words(2080, 32);
    putSyncA(words, 0);
    putSyncB(words, 1040);
    for (size_t i = 86; i < 995; ++i)
        words[i] = static_cast<uint8_t>(24 + ((i - 86) * 210) / 908);
    for (size_t i = 1126; i < 2035; ++i)
        words[i] = static_cast<uint8_t>(234 - ((i - 1126) * 210) / 908);

    constexpr double sampleRate = 48000.0;
    constexpr double wordRate = 4160.0;
    constexpr double subcarrier = 2400.0;
    const size_t leading = 23;
    const size_t body = static_cast<size_t>(
        std::ceil((words.size() + 8.0) * sampleRate / wordRate));
    std::vector<float> pcm(leading + body, 0.0f);
    double phase = 0.31;
    for (size_t n = 0; n < body; ++n) {
        const size_t index = std::min(
            words.size() - 1,
            static_cast<size_t>(std::floor(n * wordRate / sampleRate)));
        const double envelope = 0.05 + 0.90 * words[index] / 255.0;
        pcm[leading + n] = static_cast<float>(envelope * std::sin(phase));
        phase += 2.0 * std::numbers::pi_v<double> * subcarrier / sampleRate;
        if (phase >= 2.0 * std::numbers::pi_v<double>)
            phase -= 2.0 * std::numbers::pi_v<double>;
    }
    return pcm;
}

std::vector<std::complex<float>> makeBpsk() {
    constexpr double sampleRate = 1228800.0;
    constexpr double symbolRate = 1200.0;
    const size_t samplesPerSymbol = static_cast<size_t>(sampleRate / symbolRate);
    std::vector<std::complex<float>> iq;
    iq.reserve(1100 * samplesPerSymbol);
    uint32_t state = 0x51A7C3D9u;
    const std::complex<float> rotation{
        static_cast<float>(std::cos(0.37)), static_cast<float>(std::sin(0.37))};
    for (int symbol = 0; symbol < 1100; ++symbol) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const std::complex<float> value = ((state & 1u) ? 1.0f : -1.0f) * rotation;
        for (size_t i = 0; i < samplesPerSymbol; ++i) iq.push_back(value);
    }
    return iq;
}

void testSatcomCursor() {
    SatcomIqCursor cursor;
    std::vector<std::complex<float>> first(8);
    std::vector<std::complex<float>> second(8);
    for (int i = 0; i < 8; ++i) {
        first[static_cast<size_t>(i)] = {static_cast<float>(i), 0.0f};
        second[static_cast<size_t>(i)] = {static_cast<float>(i + 4), 0.0f};
    }
    const auto a = cursor.consume(first, 100, 108, 1);
    const auto b = cursor.consume(second, 104, 112, 1);
    const auto duplicate = cursor.consume(second, 104, 112, 1);
    require(a.discontinuity && a.samples.size() == 8, "initial IQ anchor");
    require(!b.discontinuity && b.samples.size() == 4 && b.samples.front().real() == 8.0f,
            "overlap is emitted exactly once");
    require(duplicate.samples.empty(), "duplicate IQ snapshot is suppressed");
}

void testAprs() {
    const auto pcm = afskEncode(hdlcBits(makeAprsFrame()));
    Ax25AprsDecoder decoder;
    std::vector<std::string> frames;
    const size_t chunks[] = {37, 511, 83, 1024, 19, 257};
    size_t offset = 0;
    size_t index = 0;
    while (offset < pcm.size()) {
        const size_t count = std::min(chunks[index % 6], pcm.size() - offset);
        auto completed = decoder.processAudio(pcm.data() + offset, count, 48000.0);
        frames.insert(frames.end(), completed.begin(), completed.end());
        offset += count;
        ++index;
    }
    require(std::any_of(frames.begin(), frames.end(), [](const std::string& frame) {
        return frame.find("SDR TOWN HARDENED") != std::string::npos;
    }), "AX.25/APRS frame and FCS decode");
}

void testApt() {
    const auto pcm = makeAptAudio();
    AptImageDecoder decoder;
    bool completed = false;
    for (size_t offset = 0; offset < pcm.size();) {
        const size_t count = std::min<size_t>(997, pcm.size() - offset);
        completed = decoder.processAudio(pcm.data() + offset, count, 48000.0) || completed;
        offset += count;
    }
    require(completed && decoder.height() == 1 && decoder.width() == 2080,
            "NOAA APT synchronized 2080-word line");
    require(decoder.lastSyncScore() > 0.65 && decoder.lastSyncBScore() > 0.60,
            "NOAA APT Sync A/Sync B confidence");

    std::vector<float> noSync(48000, 0.0f);
    for (size_t i = 0; i < noSync.size(); ++i)
        noSync[i] = static_cast<float>(0.4 * std::sin(
            2.0 * std::numbers::pi_v<double> * 2400.0 * i / 48000.0));
    AptImageDecoder negative;
    negative.processAudio(noSync.data(), noSync.size(), 48000.0);
    require(negative.height() == 0 && negative.syncCount() == 0,
            "NOAA APT does not invent unsynchronized lines");
}

void testAdsb() {
    auto message = hexFrame("8D40621D58C382D690C8AC2863A7");
    require(message.size() == 14 && ModeS::crcOk(message.data(), 14),
            "known-valid ADS-B CRC");
    message[8] ^= 1;
    require(!ModeS::crcOk(message.data(), 14), "corrupt ADS-B CRC rejection");

    const auto even = ModeS::decodeCprPair(true, 93000, 51372, true, 74158, 50194);
    const auto odd = ModeS::decodeCprPair(false, 93000, 51372, true, 74158, 50194);
    require(even.valid && std::abs(even.latDeg - 52.257202) < 1e-5 &&
            std::abs(even.lonDeg - 3.919373) < 1e-5,
            "ADS-B even-newer CPR");
    require(odd.valid && std::abs(odd.latDeg - 52.265780) < 1e-5 &&
            std::abs(odd.lonDeg - 3.938913) < 1e-5,
            "ADS-B odd-newer CPR");
}

void testInmarsatFailClosed() {
    InmarsatDemod demod;
    size_t delivered = 0;
    demod.setByteSink([&](const uint8_t*, size_t) { ++delivered; });
    demod.reset(InmarsatDemodMode::EgcBpsk1200, 1228800.0, 0.0);
    const auto iq = makeBpsk();
    for (size_t offset = 0; offset < iq.size();) {
        const size_t count = std::min<size_t>(4093, iq.size() - offset);
        demod.process(iq.data() + offset, count);
        offset += count;
    }
    const auto stats = demod.stats();
    require(stats.carrierDetected && stats.quality > 0.60,
            "Inmarsat coherent-carrier diagnostic");
    require(!stats.locked && stats.framesOut == 0 && stats.rawBlocksOut > 0 && delivered == 0,
            "Inmarsat raw bits cannot masquerade as validated protocol frames");
}

void testSdrplayProfile() {
    const auto rspDx = SdrplayProfile::modelCapabilities("RSPdx-R2");
    require(rspDx.model == "RSPdx-R2" && rspDx.hardwareApiSupported &&
            !rspDx.websocketOnly && rspDx.ports.size() == 3,
            "RSPdx-R2 physical ports");
    require(SdrplayProfile::frequencyAllowedForAntenna(
                "RSPdx-R2", "Antenna C", 199.9e6),
            "RSPdx-R2 BNC in-range frequency");
    require(!SdrplayProfile::frequencyAllowedForAntenna(
                "RSPdx-R2", "Antenna C", 201.0e6),
            "RSPdx-R2 BNC out-of-range rejection");
    require(!SdrplayProfile::biasTAllowedForAntenna("RSPdx-R2", "Antenna C"),
            "RSPdx-R2 BNC Bias-T rejection");

    const auto failedProbe = SdrplayProfile::capabilitiesFromProbe(
        "sdrplay", "RSP1B", "SDRplay RSP1B", {}, {}, {}, {}, {});
    require(failedProbe.isSdrplay && !failedProbe.probeVerified &&
            failedProbe.antennas.empty() && failedProbe.gainElements.empty() &&
            failedProbe.settingKeys.empty(),
            "failed SDRplay probe does not invent runtime controls");
    require(SdrplayProfile::usesWebsocketApi("nRSP-ST") &&
            !SdrplayProfile::usesHardwareApi("nRSP-ST"),
            "nRSP-ST is kept off the USB Hardware API path");
}

} // namespace

int main() {
    testSatcomCursor();
    testAprs();
    testApt();
    testAdsb();
    testInmarsatFailClosed();
    testSdrplayProfile();

    if (failures != 0) {
        std::cerr << failures << " non-P25 smoke check(s) failed\n";
        return 1;
    }
    std::cout << "All non-P25 standalone smoke checks passed\n";
    return 0;
}
