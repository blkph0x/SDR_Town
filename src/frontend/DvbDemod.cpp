#include "frontend/DvbDemod.h"

#include "frontend/DvbSurvey.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
// EN 302 307-1 5.5.2.1. 18D2E82 hex, 26 bits, MSB first, leading 0.
constexpr int kSofBits[26] = {
    0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0
};

// Figure 13b, six rows. b1 (MODCOD MSB) uses the first row. b6 is the last row.
constexpr char kGenerator[6][33] = {
    "11111111111111111111111111111111",
    "00000000000000001111111111111111",
    "00000000111111110000000011111111",
    "00001111000011110000111100001111",
    "00110011001100110011001100110011",
    "01010101010101010101010101010101"
};

// Section 5.5.2.4, 64-bit scrambler, MSB first.
constexpr char kScrambler[] =
    "0111000110011101100000111100100101010011010000100010110111111010";
static_assert(sizeof(kScrambler) == 65);

std::complex<float> pi2Bpsk(int bit, std::size_t index) {
    const float amp = 0.70710678118f * (bit ? -1.0f : 1.0f);
    if ((index % 2) == 0) return {amp, amp};
    return {-amp, amp};
}

int hardBit(std::complex<float> symbol, std::size_t index) {
    if ((index % 2) == 0) return (symbol.real() + symbol.imag()) < 0.0f ? 1 : 0;
    return symbol.real() > symbol.imag() ? 1 : 0;
}

void encodePls(int modcod, bool shortFrame, bool pilots, int out[64]) {
    const int info[6] = {
        (modcod >> 4) & 1,
        (modcod >> 3) & 1,
        (modcod >> 2) & 1,
        (modcod >> 1) & 1,
        modcod & 1,
        shortFrame ? 1 : 0
    };
    int coded[32] = {};
    for (int column = 0; column < 32; ++column) {
        int bit = 0;
        for (int row = 0; row < 6; ++row)
            if (info[row] && kGenerator[row][column] == '1') bit ^= 1;
        coded[column] = bit;
    }
    const int repeat = pilots ? 1 : 0;
    for (int column = 0; column < 32; ++column) {
        out[2 * column] = coded[column] ^ (kScrambler[2 * column] - '0');
        out[2 * column + 1] = (coded[column] ^ repeat) ^ (kScrambler[2 * column + 1] - '0');
    }
}

void decodePls(const int* bits, PlHeaderHit& hit) {
    int best = 65;
    int bestCount = 0;
    int modcod = -1;
    bool shortFrame = false;
    bool pilots = false;
    for (int candidate = 0; candidate < 32; ++candidate) {
        for (int type = 0; type < 4; ++type) {
            int code[64];
            encodePls(candidate, (type & 2) != 0, (type & 1) != 0, code);
            int errors = 0;
            for (int i = 0; i < 64; ++i)
                if (code[i] != bits[i]) ++errors;
            if (errors < best) {
                best = errors;
                bestCount = 1;
                modcod = candidate;
                shortFrame = (type & 2) != 0;
                pilots = (type & 1) != 0;
            } else if (errors == best) {
                ++bestCount;
            }
        }
    }
    hit.plsErrors = best;
    if (bestCount == 1 && best <= 15) {
        hit.plsDecoded = true;
        hit.modcod = modcod;
        hit.shortFrame = shortFrame;
        hit.pilots = pilots;
    }
}
}

std::vector<std::complex<float>> modulateDvbs2Sof() {
    std::vector<std::complex<float>> out;
    out.reserve(26);
    for (int bit : kSofBits) out.push_back(pi2Bpsk(bit, out.size()));
    return out;
}

std::vector<std::complex<float>> modulateDvbs2PlHeader(int modcod, bool shortFrame, bool pilots) {
    std::vector<std::complex<float>> out;
    if (modcod < 0 || modcod > 31) return out;
    int pls[64];
    encodePls(modcod, shortFrame, pilots, pls);
    out.reserve(90);
    for (int bit : kSofBits) out.push_back(pi2Bpsk(bit, out.size()));
    for (int bit : pls) out.push_back(pi2Bpsk(bit, out.size()));
    return out;
}

PlDataSymbols extractDvbs2DataSymbols(const std::complex<float>* symbols, std::size_t count, std::size_t dataSymbols) {
    PlDataSymbols out;
    out.header.note = "PL header sync only. LDPC payload demod is not linked.";
    if (!symbols || count < 26) return out;
    PlHeaderHit& hit = out.header;
    std::complex<float> expected[26];
    std::complex<float> step[25];
    for (int i = 0; i < 26; ++i)
        expected[i] = pi2Bpsk(kSofBits[i], static_cast<std::size_t>(i));
    for (int i = 1; i < 26; ++i)
        step[i - 1] = expected[i] * std::conj(expected[i - 1]);

    float bestScore = 0.0f;
    std::complex<float> bestResidual{};
    std::size_t at = 0;
    for (std::size_t start = 0; start + 26 <= count; ++start) {
        std::complex<float> residual{};
        for (int i = 1; i < 26; ++i) {
            const auto got = symbols[start + static_cast<std::size_t>(i)]
                * std::conj(symbols[start + static_cast<std::size_t>(i - 1)]);
            residual += got * std::conj(step[i - 1]);
        }
        const float score = std::abs(residual);
        if (score > bestScore) {
            bestScore = score;
            bestResidual = residual;
            at = start;
        }
    }
    if (bestScore < 10.0f) return out;
    float omega = std::arg(bestResidual);
    hit.frequencyRadPerSymbol = omega;
    auto wiped = [&](int index) {
        return symbols[at + static_cast<std::size_t>(index)]
            * std::polar(1.0f, -omega * static_cast<float>(index));
    };

    std::complex<float> acc{};
    for (int i = 0; i < 26; ++i)
        acc += wiped(i) * std::conj(expected[i]);
    const float magnitude = std::abs(acc);
    if (magnitude < 1.0f) return out;
    auto derotate = std::conj(acc) / magnitude;

    int errors = 0;
    for (int i = 0; i < 26; ++i) {
        const int bit = hardBit(wiped(i) * derotate, static_cast<std::size_t>(i));
        if (bit != kSofBits[i]) ++errors;
    }
    hit.sofErrors = errors;
    hit.symbolIndex = at;
    hit.found = errors <= 4;
    if (!hit.found) return out;
    hit.note = "SOF frequency and phase estimated. LDPC payload demod is not linked.";
    if (at + 90 > count) return out;
    int bits[64];
    for (int i = 0; i < 64; ++i)
        bits[i] = hardBit(wiped(26 + i) * derotate, static_cast<std::size_t>(26 + i));
    decodePls(bits, hit);
    if (hit.plsDecoded)
        hit.note = "PLS MODCOD decoded. Frequency refit across the 90 header symbols.";
    if (hit.plsDecoded && at + 90 <= count) {
        const auto expectedHeader = modulateDvbs2PlHeader(hit.modcod, hit.shortFrame, hit.pilots);
        if (expectedHeader.size() == 90) {
            double phase[90];
            for (int i = 0; i < 90; ++i) {
                phase[i] = std::arg(wiped(i) * std::conj(expectedHeader[static_cast<std::size_t>(i)]));
                if (i > 0) {
                    while (phase[i] - phase[i - 1] > 3.14159265) phase[i] -= 6.28318531;
                    while (phase[i - 1] - phase[i] > 3.14159265) phase[i] += 6.28318531;
                }
            }
            double sumX = 0.0;
            double sumY = 0.0;
            double sumXX = 0.0;
            double sumXY = 0.0;
            for (int i = 0; i < 90; ++i) {
                const double x = static_cast<double>(i);
                sumX += x;
                sumY += phase[i];
                sumXX += x * x;
                sumXY += x * phase[i];
            }
            const double denom = 90.0 * sumXX - sumX * sumX;
            if (std::fabs(denom) > 1.0) {
                omega += static_cast<float>((90.0 * sumXY - sumX * sumY) / denom);
                hit.frequencyRadPerSymbol = omega;
                std::complex<float> aligned{};
                for (int i = 0; i < 90; ++i)
                    aligned += wiped(i) * std::conj(expectedHeader[static_cast<std::size_t>(i)]);
                const float alignedMagnitude = std::abs(aligned);
                if (alignedMagnitude > 1.0f) derotate = std::conj(aligned) / alignedMagnitude;
            }
        }
    }
    if (dataSymbols == 0 || at + 90 + dataSymbols > count) return out;
    out.data.resize(dataSymbols);
    for (std::size_t i = 0; i < dataSymbols; ++i)
        out.data[i] = wiped(90 + static_cast<int>(i)) * derotate;
    return out;
}

PlHeaderHit detectDvbs2PlHeader(const std::complex<float>* symbols, std::size_t count) {
    return extractDvbs2DataSymbols(symbols, count, 0).header;
}

namespace {
struct BbPrbs {
    int cell[15] = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    int next() {
        const int out = cell[14];
        const int feedback = cell[13] ^ cell[14];
        for (int i = 14; i > 0; --i) cell[i] = cell[i - 1];
        cell[0] = feedback;
        return out;
    }
};

std::uint8_t crc8Dvb(const std::uint8_t* data, std::size_t size) {
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            const int mix = ((crc >> 7) & 1) ^ ((data[i] >> bit) & 1);
            crc = static_cast<std::uint8_t>(crc << 1);
            if (mix) crc ^= 0xD5;
        }
    }
    return crc;
}

int readBe16(const std::uint8_t* data) {
    return (static_cast<int>(data[0]) << 8) | data[1];
}

void fillGold(std::vector<std::uint8_t>& x, std::vector<std::uint8_t>& y, int span) {
    x.assign(static_cast<std::size_t>(span), 0);
    y.assign(static_cast<std::size_t>(span), 1);
    x[0] = 1;
    for (int i = 0; i + 18 < span; ++i) {
        x[static_cast<std::size_t>(i + 18)] = x[static_cast<std::size_t>(i + 7)] ^ x[static_cast<std::size_t>(i)];
        y[static_cast<std::size_t>(i + 18)] = y[static_cast<std::size_t>(i + 10)] ^ y[static_cast<std::size_t>(i + 7)]
            ^ y[static_cast<std::size_t>(i + 5)] ^ y[static_cast<std::size_t>(i)];
    }
}

std::complex<float> gold0Symbol(const std::vector<std::uint8_t>& x, const std::vector<std::uint8_t>& y, int index) {
    constexpr int kPeriod = 262143;
    const int late = (index + 131072) % kPeriod;
    const int zn = x[static_cast<std::size_t>(index)] ^ y[static_cast<std::size_t>(index)];
    const int znLate = x[static_cast<std::size_t>(late)] ^ y[static_cast<std::size_t>(late)];
    const int rn = 2 * znLate + zn;
    if (rn == 0) return {1.0f, 0.0f};
    if (rn == 1) return {0.0f, 1.0f};
    if (rn == 2) return {-1.0f, 0.0f};
    return {0.0f, -1.0f};
}
}

std::vector<std::uint8_t> scrambleBbFrame(const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> out(size);
    if (!data || size == 0) return out;
    BbPrbs prbs;
    for (std::size_t i = 0; i < size; ++i) {
        int mask = 0;
        for (int bit = 0; bit < 8; ++bit) mask = (mask << 1) | prbs.next();
        out[i] = static_cast<std::uint8_t>(data[i] ^ mask);
    }
    return out;
}

void finishBb(BbFrameTs& result) {
    if (!result.clearTs.empty())
        result.reject.clear();
    else if (result.scrambledPackets > 0)
        result.reject = "Scrambled transport packets are not emitted";
    else if (result.packets == 0)
        result.reject = "No clear transport packet in the baseband frame";
}

void emitPacket(BbFrameTs& result, const std::uint8_t* packet, int payloadBytes, int sync, int dnp) {
    ++result.packets;
    const bool crcOk = crc8Dvb(packet + 1, static_cast<std::size_t>(payloadBytes - 1)) == packet[0];
    if (!crcOk) {
        result.reject = "User packet CRC failed";
        return;
    }
    const int scrambling = packet[3] >> 6;
    if (scrambling != 0) {
        ++result.scrambledPackets;
        return;
    }
    result.clearTs.push_back(static_cast<std::uint8_t>(sync));
    result.clearTs.insert(result.clearTs.end(), packet + 1, packet + payloadBytes);
    for (int n = 0; n < dnp; ++n) {
        result.clearTs.push_back(0x47);
        result.clearTs.push_back(0x1F);
        result.clearTs.push_back(0xFF);
        result.clearTs.push_back(0x10);
        result.clearTs.insert(result.clearTs.end(), 184, 0);
    }
}

BbFrameTs readBbFrame(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>* carry) {
    BbFrameTs result;
    result.reject = "LDPC is not applied. The baseband bytes must already be in hand.";
    if (!data || size < 10) {
        result.reject = "BBFRAME is shorter than the header";
        return result;
    }
    const auto clear = scrambleBbFrame(data, size);
    result.headerCrcOk = crc8Dvb(clear.data(), 9) == clear[9];
    if (!result.headerCrcOk) {
        result.reject = "BBHEADER CRC failed";
        return result;
    }
    const int tsGs = (clear[0] >> 6) & 3;
    const bool issyi = ((clear[0] >> 3) & 1) != 0;
    const bool npd = ((clear[0] >> 2) & 1) != 0;
    const int upl = readBe16(clear.data() + 2);
    const int dfl = readBe16(clear.data() + 4);
    const int sync = clear[6];
    const int syncd = readBe16(clear.data() + 7);
    if (tsGs != 3) {
        result.reject = "Not a single MPEG transport-stream baseband frame";
        return result;
    }
    if (issyi) {
        result.reject = "ISSY is not stripped";
        return result;
    }
    const int packetBytes = npd ? 189 : 188;
    if (upl != packetBytes * 8) {
        result.reject = "Not a single MPEG transport-stream baseband frame";
        return result;
    }
    if ((dfl % 8) != 0 || (syncd != 65535 && (syncd % 8) != 0)) {
        result.reject = "Baseband field is not byte aligned";
        return result;
    }
    const int dataBytes = dfl / 8;
    if (10 + dataBytes > static_cast<int>(clear.size())) {
        result.reject = "DATA FIELD is truncated";
        return result;
    }
    std::vector<std::uint8_t> local;
    std::vector<std::uint8_t>& pending = carry ? *carry : local;
    const std::uint8_t* field = clear.data() + 10;
    if (syncd == 65535) {
        pending.insert(pending.end(), field, field + dataBytes);
    } else {
        const int offset = syncd / 8;
        if (offset > dataBytes) {
            result.reject = "DATA FIELD is truncated";
            return result;
        }
        pending.insert(pending.end(), field, field + offset);
        int cursor = offset;
        auto take = [&](std::vector<std::uint8_t>& buf) {
            while (static_cast<int>(buf.size()) >= packetBytes) {
                const int dnp = npd ? buf[188] : 0;
                emitPacket(result, buf.data(), 188, sync, dnp);
                buf.erase(buf.begin(), buf.begin() + packetBytes);
            }
        };
        take(pending);
        std::vector<std::uint8_t> aligned(field + cursor, field + dataBytes);
        take(aligned);
        pending.insert(pending.end(), aligned.begin(), aligned.end());
    }
    while (static_cast<int>(pending.size()) >= packetBytes) {
        const int dnp = npd ? pending[188] : 0;
        emitPacket(result, pending.data(), 188, sync, dnp);
        pending.erase(pending.begin(), pending.begin() + packetBytes);
    }
    if (!carry) pending.clear();
    finishBb(result);
    return result;
}

BbFrameTs extractClearTsFromBbFrame(const std::uint8_t* data, std::size_t size) {
    return readBbFrame(data, size, nullptr);
}

BbFrameTs appendClearTsFromBbFrame(BbCarry& carry, const std::uint8_t* data, std::size_t size) {
    return readBbFrame(data, size, &carry.tail);
}

QpskHardBits sliceQpskAfterPlDescramble(const std::complex<float>* symbols, std::size_t count, int modcod) {
    QpskHardBits out;
    out.note = "Pre-FEC QPSK hard bits. LDPC is not applied.";
    if (modcod < 1 || modcod > 11) {
        out.note = "Not a QPSK MODCOD. LDPC is not applied.";
        return out;
    }
    if (!symbols || count == 0) return out;
    const int span = static_cast<int>(count) + 131072 + 20;
    std::vector<std::uint8_t> x;
    std::vector<std::uint8_t> y;
    fillGold(x, y, span);
    out.bits.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        const auto symbol = symbols[i] * std::conj(gold0Symbol(x, y, static_cast<int>(i)));
        out.bits.push_back(symbol.real() < 0.0f ? 1 : 0);
        out.bits.push_back(symbol.imag() < 0.0f ? 1 : 0);
    }
    out.sliced = true;
    return out;
}

std::vector<std::complex<float>> modulateQpsk(const int* bits, std::size_t bitCount) {
    std::vector<std::complex<float>> out;
    if (!bits || (bitCount % 2) != 0) return out;
    out.reserve(bitCount / 2);
    for (std::size_t i = 0; i < bitCount; i += 2) {
        const float inPhase = bits[i] ? -0.70710678118f : 0.70710678118f;
        const float quadrature = bits[i + 1] ? -0.70710678118f : 0.70710678118f;
        out.push_back({inPhase, quadrature});
    }
    return out;
}

std::vector<std::complex<float>> scramblePlSymbols(const std::complex<float>* symbols, std::size_t count) {
    std::vector<std::complex<float>> out;
    if (!symbols || count == 0) return out;
    const int span = static_cast<int>(count) + 131072 + 20;
    std::vector<std::uint8_t> x;
    std::vector<std::uint8_t> y;
    fillGold(x, y, span);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        out.push_back(symbols[i] * gold0Symbol(x, y, static_cast<int>(i)));
    return out;
}

ClearTsPlayback writeClearTsForPlayback(const std::uint8_t* data, std::size_t size, const std::string& path) {
    ClearTsPlayback out;
    const auto inventory = inventoryClearTransportStream(data, size);
    if (!inventory.aligned || inventory.packets < 1) {
        out.reject = "No MPEG-TS to play";
        return out;
    }
    if (inventory.scrambledPackets > 0) {
        out.reject = "Scrambled transport packets are not played";
        return out;
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        out.reject = "Could not write the clear transport stream";
        return out;
    }
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file) {
        out.reject = "Could not finish the clear transport stream";
        return out;
    }
    out.accepted = true;
    return out;
}

namespace {

struct ConstellationPoint {
    int bits = 0;
    float radius = 1.0f;
    float angleDeg = 0.0f;
};

float wrapDeg(float angle) {
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

float angleDelta(float left, float right) {
    const float delta = std::fabs(wrapDeg(left) - wrapDeg(right));
    return delta > 180.0f ? 360.0f - delta : delta;
}

std::complex<float> fromPolar(float radius, float angleDeg) {
    const float radians = angleDeg * 0.01745329251f;
    return {radius * std::cos(radians), radius * std::sin(radians)};
}

// Figure 10. Points sit on the unit circle at multiples of 45 degrees.
const ConstellationPoint k8psk[] = {
    {0b001, 1.0f, 0.0f}, {0b000, 1.0f, 45.0f}, {0b100, 1.0f, 90.0f}, {0b110, 1.0f, 135.0f},
    {0b010, 1.0f, 180.0f}, {0b011, 1.0f, 225.0f}, {0b111, 1.0f, 270.0f}, {0b101, 1.0f, 315.0f},
};

// Figure 11. Inner ring at 45 degrees plus multiples of 90. Outer ring at 15 degrees plus multiples of 30.
const ConstellationPoint k16Inner[] = {
    {0b1100, 1.0f, 45.0f}, {0b1110, 1.0f, 135.0f}, {0b1111, 1.0f, 225.0f}, {0b1101, 1.0f, 315.0f},
};
const ConstellationPoint k16Outer[] = {
    {0b0100, 1.0f, 15.0f}, {0b0000, 1.0f, 45.0f}, {0b1000, 1.0f, 75.0f}, {0b1010, 1.0f, 105.0f},
    {0b0010, 1.0f, 135.0f}, {0b0110, 1.0f, 165.0f}, {0b0111, 1.0f, 195.0f}, {0b0011, 1.0f, 225.0f},
    {0b1011, 1.0f, 255.0f}, {0b1001, 1.0f, 285.0f}, {0b0001, 1.0f, 315.0f}, {0b0101, 1.0f, 345.0f},
};

// Figure 12. Inner at 45+90k, middle at 15+30k, outer at 22.5+22.5k.
const ConstellationPoint k32Inner[] = {
    {0b10001, 1.0f, 45.0f}, {0b10101, 1.0f, 135.0f}, {0b10111, 1.0f, 225.0f}, {0b10011, 1.0f, 315.0f},
};
const ConstellationPoint k32Middle[] = {
    {0b10000, 1.0f, 15.0f}, {0b00000, 1.0f, 45.0f}, {0b00001, 1.0f, 75.0f}, {0b00101, 1.0f, 105.0f},
    {0b00100, 1.0f, 135.0f}, {0b10100, 1.0f, 165.0f}, {0b10110, 1.0f, 195.0f}, {0b00110, 1.0f, 225.0f},
    {0b00111, 1.0f, 255.0f}, {0b00011, 1.0f, 285.0f}, {0b00010, 1.0f, 315.0f}, {0b10010, 1.0f, 345.0f},
};
const ConstellationPoint k32Outer[] = {
    {0b11000, 1.0f, 0.0f}, {0b01000, 1.0f, 22.5f}, {0b11001, 1.0f, 45.0f}, {0b01001, 1.0f, 67.5f},
    {0b01101, 1.0f, 90.0f}, {0b11101, 1.0f, 112.5f}, {0b01100, 1.0f, 135.0f}, {0b11100, 1.0f, 157.5f},
    {0b11110, 1.0f, 180.0f}, {0b01110, 1.0f, 202.5f}, {0b11111, 1.0f, 225.0f}, {0b01111, 1.0f, 247.5f},
    {0b01011, 1.0f, 270.0f}, {0b11011, 1.0f, 292.5f}, {0b01010, 1.0f, 315.0f}, {0b11010, 1.0f, 337.5f},
};

int nearestBits(float angleDeg, const ConstellationPoint* points, int count) {
    int bestBits = points[0].bits;
    float best = 1.0e9f;
    for (int i = 0; i < count; ++i) {
        const float delta = angleDelta(angleDeg, points[i].angleDeg);
        if (delta < best) {
            best = delta;
            bestBits = points[i].bits;
        }
    }
    return bestBits;
}

float gamma16(int modcod) {
    switch (modcod) {
    case 18: return 3.15f;
    case 19: return 2.85f;
    case 20: return 2.75f;
    case 21: return 2.70f;
    case 22: return 2.60f;
    case 23: return 2.57f;
    default: return 0.0f;
    }
}

void gamma32(int modcod, float& gamma1, float& gamma2) {
    gamma1 = 0.0f;
    gamma2 = 0.0f;
    switch (modcod) {
    case 24: gamma1 = 2.84f; gamma2 = 5.27f; break;
    case 25: gamma1 = 2.72f; gamma2 = 4.87f; break;
    case 26: gamma1 = 2.64f; gamma2 = 4.64f; break;
    case 27: gamma1 = 2.54f; gamma2 = 4.33f; break;
    case 28: gamma1 = 2.53f; gamma2 = 4.30f; break;
    default: break;
    }
}

std::complex<float> mapSymbol(int modcod, int bits) {
    if (modcod >= 1 && modcod <= 11) {
        const float inPhase = (bits & 2) ? -0.70710678118f : 0.70710678118f;
        const float quadrature = (bits & 1) ? -0.70710678118f : 0.70710678118f;
        return {inPhase, quadrature};
    }
    if (modcod >= 12 && modcod <= 17) {
        for (const auto& point : k8psk)
            if (point.bits == bits) return fromPolar(1.0f, point.angleDeg);
    }
    if (modcod >= 18 && modcod <= 23) {
        const float gamma = gamma16(modcod);
        const float inner = 2.0f / std::sqrt(1.0f + 3.0f * gamma * gamma);
        const float outer = gamma * inner;
        for (const auto& point : k16Inner)
            if (point.bits == bits) return fromPolar(inner, point.angleDeg);
        for (const auto& point : k16Outer)
            if (point.bits == bits) return fromPolar(outer, point.angleDeg);
    }
    if (modcod >= 24 && modcod <= 28) {
        float gamma1 = 0.0f;
        float gamma2 = 0.0f;
        gamma32(modcod, gamma1, gamma2);
        const float inner = std::sqrt(8.0f / (1.0f + 3.0f * gamma1 * gamma1 + 4.0f * gamma2 * gamma2));
        const float middle = gamma1 * inner;
        const float outer = gamma2 * inner;
        for (const auto& point : k32Inner)
            if (point.bits == bits) return fromPolar(inner, point.angleDeg);
        for (const auto& point : k32Middle)
            if (point.bits == bits) return fromPolar(middle, point.angleDeg);
        for (const auto& point : k32Outer)
            if (point.bits == bits) return fromPolar(outer, point.angleDeg);
    }
    return {};
}

int sliceSymbol(int modcod, std::complex<float> symbol) {
    const float angle = wrapDeg(std::atan2(symbol.imag(), symbol.real()) * 57.2957795f);
    const float radius = std::abs(symbol);
    if (modcod >= 1 && modcod <= 11) {
        const int inPhase = symbol.real() < 0.0f ? 1 : 0;
        const int quadrature = symbol.imag() < 0.0f ? 1 : 0;
        return (inPhase << 1) | quadrature;
    }
    if (modcod >= 12 && modcod <= 17)
        return nearestBits(angle, k8psk, 8);
    if (modcod >= 18 && modcod <= 23) {
        const float gamma = gamma16(modcod);
        const float inner = 2.0f / std::sqrt(1.0f + 3.0f * gamma * gamma);
        const float outer = gamma * inner;
        const auto* ring = radius < 0.5f * (inner + outer) ? k16Inner : k16Outer;
        const int count = radius < 0.5f * (inner + outer) ? 4 : 12;
        return nearestBits(angle, ring, count);
    }
    float gamma1 = 0.0f;
    float gamma2 = 0.0f;
    gamma32(modcod, gamma1, gamma2);
    const float inner = std::sqrt(8.0f / (1.0f + 3.0f * gamma1 * gamma1 + 4.0f * gamma2 * gamma2));
    const float middle = gamma1 * inner;
    const float outer = gamma2 * inner;
    const float low = 0.5f * (inner + middle);
    const float high = 0.5f * (middle + outer);
    if (radius < low) return nearestBits(angle, k32Inner, 4);
    if (radius < high) return nearestBits(angle, k32Middle, 12);
    return nearestBits(angle, k32Outer, 16);
}

std::vector<int> interleaveBits(const std::vector<int>& bits, int columns, bool readMsbLast) {
    const int rows = static_cast<int>(bits.size()) / columns;
    std::vector<int> matrix(bits.size());
    int cursor = 0;
    for (int column = 0; column < columns; ++column)
        for (int row = 0; row < rows; ++row)
            matrix[static_cast<std::size_t>(row * columns + column)] = bits[static_cast<std::size_t>(cursor++)];
    std::vector<int> out(bits.size());
    cursor = 0;
    for (int row = 0; row < rows; ++row) {
        for (int place = 0; place < columns; ++place) {
            const int column = readMsbLast ? (columns - 1 - place) : place;
            out[static_cast<std::size_t>(cursor++)] = matrix[static_cast<std::size_t>(row * columns + column)];
        }
    }
    return out;
}

std::vector<int> deinterleaveBits(const std::vector<int>& bits, int columns, bool readMsbLast) {
    const int rows = static_cast<int>(bits.size()) / columns;
    std::vector<int> matrix(bits.size());
    int cursor = 0;
    for (int row = 0; row < rows; ++row) {
        for (int place = 0; place < columns; ++place) {
            const int column = readMsbLast ? (columns - 1 - place) : place;
            matrix[static_cast<std::size_t>(row * columns + column)] = bits[static_cast<std::size_t>(cursor++)];
        }
    }
    std::vector<int> out(bits.size());
    cursor = 0;
    for (int column = 0; column < columns; ++column)
        for (int row = 0; row < rows; ++row)
            out[static_cast<std::size_t>(cursor++)] = matrix[static_cast<std::size_t>(row * columns + column)];
    return out;
}

void pushBits(std::vector<int>& out, int bits, int width) {
    for (int shift = width - 1; shift >= 0; --shift)
        out.push_back((bits >> shift) & 1);
}

int takeBits(const int* bits, int width) {
    int value = 0;
    for (int i = 0; i < width; ++i) value = (value << 1) | (bits[i] & 1);
    return value;
}

float rrcTap(float time, double rollOff) {
    const float alpha = static_cast<float>(rollOff);
    if (std::fabs(time) < 1.0e-6f)
        return (1.0f - alpha) + 4.0f * alpha / 3.14159265f;
    const float boundary = 1.0f / (4.0f * alpha);
    if (std::fabs(std::fabs(time) - boundary) < 1.0e-4f) {
        return alpha / 1.41421356f
            * ((1.0f + 2.0f / 3.14159265f) * std::sin(3.14159265f * boundary)
                + (1.0f - 2.0f / 3.14159265f) * std::cos(3.14159265f * boundary));
    }
    const float piTime = 3.14159265f * time;
    const float denom = piTime * (1.0f - std::pow(4.0f * alpha * time, 2.0f));
    return (std::sin(piTime * (1.0f - alpha)) + 4.0f * alpha * time * std::cos(piTime * (1.0f + alpha))) / denom;
}

}

Dvbs2Modcod dvbs2Modcod(int modcod) {
    Dvbs2Modcod out;
    if (modcod >= 1 && modcod <= 11) {
        out.known = true;
        out.bitsPerSymbol = 2;
        out.rateModcod = modcod;
        return out;
    }
    const int higher[][3] = {
        {12, 3, 5}, {13, 3, 6}, {14, 3, 7}, {15, 3, 9}, {16, 3, 10}, {17, 3, 11},
        {18, 4, 6}, {19, 4, 7}, {20, 4, 8}, {21, 4, 9}, {22, 4, 10}, {23, 4, 11},
        {24, 5, 7}, {25, 5, 8}, {26, 5, 9}, {27, 5, 10}, {28, 5, 11},
    };
    for (const auto& row : higher) {
        if (row[0] != modcod) continue;
        out.known = true;
        out.bitsPerSymbol = row[1];
        out.rateModcod = row[2];
        out.readMsbLast = modcod == 12;
        return out;
    }
    return out;
}

int dvbs2PayloadSymbols(int modcod, bool shortFrame, bool pilots) {
    const auto info = dvbs2Modcod(modcod);
    if (!info.known) return 0;
    if (shortFrame && info.rateModcod == 11) return 0;
    const int data = (shortFrame ? 16200 : 64800) / info.bitsPerSymbol;
    if (!pilots) return data;
    const int slots = data / 90;
    return data + 36 * ((slots - 1) / 16);
}

std::vector<std::complex<float>> modulateDvbs2Data(const int* fecBits, std::size_t bitCount,
    int modcod, bool shortFrame, bool pilots) {
    std::vector<std::complex<float>> out;
    const auto info = dvbs2Modcod(modcod);
    const int nLdpc = shortFrame ? 16200 : 64800;
    if (!fecBits || !info.known || static_cast<int>(bitCount) != nLdpc) return out;
    if (shortFrame && info.rateModcod == 11) return out;
    std::vector<int> serial(fecBits, fecBits + bitCount);
    if (info.bitsPerSymbol > 2)
        serial = interleaveBits(serial, info.bitsPerSymbol, info.readMsbLast);
    const int dataSymbols = nLdpc / info.bitsPerSymbol;
    std::vector<std::complex<float>> data;
    data.reserve(static_cast<std::size_t>(dataSymbols));
    for (int i = 0; i < dataSymbols; ++i)
        data.push_back(mapSymbol(modcod, takeBits(serial.data() + static_cast<std::size_t>(i * info.bitsPerSymbol), info.bitsPerSymbol)));
    if (!pilots) return data;
    const std::complex<float> pilot{0.70710678118f, 0.70710678118f};
    int produced = 0;
    int slot = 0;
    while (produced < dataSymbols) {
        const int take = std::min(90, dataSymbols - produced);
        out.insert(out.end(), data.begin() + produced, data.begin() + produced + take);
        produced += take;
        ++slot;
        if (slot % 16 == 0 && produced < dataSymbols)
            out.insert(out.end(), 36, pilot);
    }
    return out;
}

Dvbs2Slice sliceDvbs2FecBits(const std::complex<float>* symbols, std::size_t count,
    int modcod, bool shortFrame, bool pilots) {
    Dvbs2Slice out;
    out.note = "Pre-FEC hard bits. LDPC is not applied.";
    const auto info = dvbs2Modcod(modcod);
    const int expected = dvbs2PayloadSymbols(modcod, shortFrame, pilots);
    if (!info.known) {
        out.note = "That MODCOD is not decoded.";
        return out;
    }
    if (shortFrame && info.rateModcod == 11) {
        out.note = "Short rate 9/10 does not exist.";
        return out;
    }
    if (!symbols || expected <= 0 || static_cast<int>(count) != expected) {
        out.note = "Payload length does not match this MODCOD.";
        return out;
    }
    const int span = expected + 131072 + 20;
    std::vector<std::uint8_t> xReg;
    std::vector<std::uint8_t> yReg;
    fillGold(xReg, yReg, span);
    const int dataSymbols = (shortFrame ? 16200 : 64800) / info.bitsPerSymbol;
    std::vector<std::complex<float>> data;
    data.reserve(static_cast<std::size_t>(dataSymbols));
    const std::complex<float> pilotPoint{0.70710678118f, 0.70710678118f};
    std::complex<float> correction{1.0f, 0.0f};
    int index = 0;
    int slot = 0;
    while (static_cast<int>(data.size()) < dataSymbols) {
        const int take = std::min(90, dataSymbols - static_cast<int>(data.size()));
        for (int n = 0; n < take; ++n) {
            const int at = index++;
            const auto descrambled = symbols[at] * std::conj(gold0Symbol(xReg, yReg, at));
            data.push_back(descrambled * correction);
        }
        ++slot;
        if (pilots && slot % 16 == 0 && static_cast<int>(data.size()) < dataSymbols) {
            std::complex<float> acc{};
            for (int n = 0; n < 36; ++n) {
                const int at = index++;
                const auto descrambled = symbols[at] * std::conj(gold0Symbol(xReg, yReg, at));
                acc += descrambled * std::conj(pilotPoint);
            }
            const float magnitude = std::abs(acc);
            if (magnitude > 1.0e-3f) correction = std::conj(acc) / magnitude;
        }
    }
    double power = 0.0;
    for (const auto& symbol : data) power += std::norm(symbol);
    const float scale = power > 0.0 ? static_cast<float>(1.0 / std::sqrt(power / static_cast<double>(data.size()))) : 1.0f;
    std::vector<int> serial;
    serial.reserve(static_cast<std::size_t>(dataSymbols * info.bitsPerSymbol));
    for (const auto& symbol : data)
        pushBits(serial, sliceSymbol(modcod, symbol * scale), info.bitsPerSymbol);
    out.fecBits = info.bitsPerSymbol > 2
        ? deinterleaveBits(serial, info.bitsPerSymbol, info.readMsbLast)
        : serial;
    out.sliced = static_cast<int>(out.fecBits.size()) == (shortFrame ? 16200 : 64800);
    if (!out.sliced) out.note = "Slicer did not produce a FECFRAME.";
    return out;
}

std::vector<std::complex<float>> shapeDvbs2Rrc(const std::complex<float>* symbols, std::size_t count,
    int samplesPerSymbol, double rollOff) {
    std::vector<std::complex<float>> out;
    if (!symbols || count == 0 || samplesPerSymbol < 2 || samplesPerSymbol > 8) return out;
    if (std::fabs(rollOff - 0.20) > 0.001 && std::fabs(rollOff - 0.25) > 0.001 && std::fabs(rollOff - 0.35) > 0.001)
        return out;
    constexpr int kSpan = 8;
    const int taps = kSpan * samplesPerSymbol * 2 + 1;
    std::vector<float> filter(static_cast<std::size_t>(taps));
    float energy = 0.0f;
    for (int i = 0; i < taps; ++i) {
        const float time = static_cast<float>(i - kSpan * samplesPerSymbol) / static_cast<float>(samplesPerSymbol);
        filter[static_cast<std::size_t>(i)] = rrcTap(time, rollOff);
        energy += filter[static_cast<std::size_t>(i)] * filter[static_cast<std::size_t>(i)];
    }
    const float norm = energy > 0.0f ? 1.0f / std::sqrt(energy) : 1.0f;
    const int length = static_cast<int>(count) * samplesPerSymbol + taps;
    out.assign(static_cast<std::size_t>(length), {});
    for (std::size_t symbol = 0; symbol < count; ++symbol) {
        const int origin = static_cast<int>(symbol) * samplesPerSymbol;
        for (int tap = 0; tap < taps; ++tap)
            out[static_cast<std::size_t>(origin + tap)] += symbols[symbol] * (filter[static_cast<std::size_t>(tap)] * norm);
    }
    return out;
}

TimedSymbols recoverDvbs2Symbols(const std::complex<float>* samples, std::size_t count,
    double sampleRateHz, double symbolRateHz, double rollOff) {
    TimedSymbols out;
    out.note = "Symbol timing did not find a PL header.";
    if (!samples || count < 64 || sampleRateHz <= 0.0 || symbolRateHz <= 0.0) return out;
    const double ratio = sampleRateHz / symbolRateHz;
    const int samplesPerSymbol = static_cast<int>(std::lround(ratio));
    if (samplesPerSymbol < 2 || samplesPerSymbol > 8 || std::fabs(ratio - samplesPerSymbol) > 0.02) {
        out.note = "Samples per symbol must be an integer from 2 to 8.";
        return out;
    }
    if (std::fabs(rollOff - 0.20) > 0.001 && std::fabs(rollOff - 0.25) > 0.001 && std::fabs(rollOff - 0.35) > 0.001) {
        out.note = "Roll-off must be 0.20, 0.25, or 0.35.";
        return out;
    }
    constexpr int kSpan = 8;
    const int taps = kSpan * samplesPerSymbol * 2 + 1;
    std::vector<float> filter(static_cast<std::size_t>(taps));
    float energy = 0.0f;
    for (int i = 0; i < taps; ++i) {
        const float time = static_cast<float>(i - kSpan * samplesPerSymbol) / static_cast<float>(samplesPerSymbol);
        filter[static_cast<std::size_t>(i)] = rrcTap(time, rollOff);
        energy += filter[static_cast<std::size_t>(i)] * filter[static_cast<std::size_t>(i)];
    }
    const float norm = energy > 0.0f ? 1.0f / std::sqrt(energy) : 1.0f;
    std::vector<std::complex<float>> matched(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::complex<float> acc{};
        const int start = static_cast<int>(i) - taps + 1;
        for (int tap = 0; tap < taps; ++tap) {
            const int at = start + tap;
            if (at < 0 || at >= static_cast<int>(count)) continue;
            acc += samples[at] * (filter[static_cast<std::size_t>(taps - 1 - tap)] * norm);
        }
        matched[i] = acc;
    }
    PlHeaderHit bestHeader;
    float bestEnergy = -1.0f;
    std::vector<std::complex<float>> best;
    for (int phase = 0; phase < 16; ++phase) {
        const float delay = static_cast<float>(phase) / 16.0f * static_cast<float>(samplesPerSymbol);
        std::vector<std::complex<float>> symbols;
        for (float cursor = delay; cursor + 1.0f < static_cast<float>(matched.size()); cursor += static_cast<float>(samplesPerSymbol)) {
            const int left = static_cast<int>(cursor);
            const float fraction = cursor - static_cast<float>(left);
            const auto sample = matched[static_cast<std::size_t>(left)] * (1.0f - fraction)
                + matched[static_cast<std::size_t>(left + 1)] * fraction;
            symbols.push_back(sample);
        }
        if (symbols.size() < 90) continue;
        const auto header = detectDvbs2PlHeader(symbols.data(), symbols.size());
        if (!header.plsDecoded || header.sofErrors > 1) continue;
        float eye = 0.0f;
        const std::size_t from = header.symbolIndex;
        const std::size_t to = std::min(symbols.size(), from + 90);
        for (std::size_t i = from; i < to; ++i)
            eye += std::min(std::fabs(symbols[i].real()), std::fabs(symbols[i].imag()));
        if (eye > bestEnergy) {
            bestHeader = header;
            bestEnergy = eye;
            best = std::move(symbols);
        }
    }
    if (!bestHeader.plsDecoded) return out;
    out.locked = true;
    out.samplesPerSymbol = samplesPerSymbol;
    out.symbols = std::move(best);
    out.note = "Symbol timing locked on the PL header.";
    return out;
}

double estimateDvbs2SymbolRateHz(const std::complex<float>* samples, std::size_t count, double sampleRateHz) {
    if (!samples || count < 1024 || sampleRateHz <= 0.0) return 0.0;
    const int length = static_cast<int>(std::min<std::size_t>(count, 4096));
    double mean = 0.0;
    std::vector<double> power(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) {
        power[static_cast<std::size_t>(i)] = std::norm(samples[i]);
        mean += power[static_cast<std::size_t>(i)];
    }
    mean /= length;
    double best = 0.0;
    int bestBin = 0;
    double floor = 0.0;
    const int first = length / 16;
    const int last = length / 2;
    for (int bin = first; bin < last; ++bin) {
        double real = 0.0;
        double imag = 0.0;
        const double step = -2.0 * 3.141592653589793 * static_cast<double>(bin) / static_cast<double>(length);
        for (int n = 0; n < length; ++n) {
            const double angle = step * static_cast<double>(n);
            const double value = power[static_cast<std::size_t>(n)] - mean;
            real += value * std::cos(angle);
            imag += value * std::sin(angle);
        }
        const double magnitude = std::sqrt(real * real + imag * imag);
        floor += magnitude;
        if (magnitude > best) {
            best = magnitude;
            bestBin = bin;
        }
    }
    const double average = floor / static_cast<double>(last - first);
    if (bestBin == 0 || best < average * 4.0) return 0.0;
    return sampleRateHz * static_cast<double>(bestBin) / static_cast<double>(length);
}
