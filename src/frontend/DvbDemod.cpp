#include "frontend/DvbDemod.h"

#include "frontend/DvbSurvey.h"

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
    const float omega = std::arg(bestResidual);
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
    const auto derotate = std::conj(acc) / magnitude;

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
        hit.note = "PLS MODCOD decoded after one SOF frequency estimate. LDPC payload demod is not linked.";
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

BbFrameTs extractClearTsFromBbFrame(const std::uint8_t* data, std::size_t size) {
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
    const int upl = readBe16(clear.data() + 2);
    const int dfl = readBe16(clear.data() + 4);
    const int sync = clear[6];
    const int syncd = readBe16(clear.data() + 7);
    if (tsGs != 3 || upl != 188 * 8) {
        result.reject = "Not a single MPEG transport-stream baseband frame";
        return result;
    }
    if (upl == 0 || (dfl % 8) != 0 || (syncd % 8) != 0) {
        result.reject = "Baseband field is not byte aligned";
        return result;
    }
    const int dataBytes = dfl / 8;
    const int offset = syncd / 8;
    if (10 + dataBytes > static_cast<int>(clear.size()) || offset > dataBytes) {
        result.reject = "DATA FIELD is truncated";
        return result;
    }
    const int packetBytes = upl / 8;
    int cursor = offset;
    while (cursor + packetBytes <= dataBytes) {
        const std::uint8_t* packet = clear.data() + 10 + cursor;
        ++result.packets;
        if (crc8Dvb(packet + 1, static_cast<std::size_t>(packetBytes - 1)) != packet[0]) {
            result.reject = "User packet CRC failed";
            cursor += packetBytes;
            continue;
        }
        const int scrambling = packet[3] >> 6;
        if (scrambling != 0) {
            ++result.scrambledPackets;
            cursor += packetBytes;
            continue;
        }
        result.clearTs.push_back(static_cast<std::uint8_t>(sync));
        result.clearTs.insert(result.clearTs.end(), packet + 1, packet + packetBytes);
        cursor += packetBytes;
    }
    if (!result.clearTs.empty())
        result.reject.clear();
    else if (result.scrambledPackets > 0)
        result.reject = "Scrambled transport packets are not emitted";
    else if (result.packets == 0)
        result.reject = "No clear transport packet in the baseband frame";
    return result;
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
