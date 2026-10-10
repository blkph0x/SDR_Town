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

PlHeaderHit detectDvbs2PlHeader(const std::complex<float>* symbols, std::size_t count) {
    PlHeaderHit hit;
    hit.note = "PL header sync only. LDPC payload demod is not linked.";
    if (!symbols || count < 26) return hit;
    std::complex<float> expected[26];
    std::complex<float> step[25];
    for (int i = 0; i < 26; ++i)
        expected[i] = pi2Bpsk(kSofBits[i], static_cast<std::size_t>(i));
    for (int i = 1; i < 26; ++i)
        step[i - 1] = expected[i] * std::conj(expected[i - 1]);

    float bestScore = 0.0f;
    std::size_t at = 0;
    for (std::size_t start = 0; start + 26 <= count; ++start) {
        float score = 0.0f;
        for (int i = 1; i < 26; ++i) {
            const auto got = symbols[start + static_cast<std::size_t>(i)]
                * std::conj(symbols[start + static_cast<std::size_t>(i - 1)]);
            score += std::real(got * std::conj(step[i - 1]));
        }
        if (score > bestScore) {
            bestScore = score;
            at = start;
        }
    }
    if (bestScore < 10.0f) return hit;

    std::complex<float> acc{};
    for (int i = 0; i < 26; ++i)
        acc += symbols[at + static_cast<std::size_t>(i)] * std::conj(expected[i]);
    const float magnitude = std::abs(acc);
    if (magnitude < 1.0f) return hit;
    const auto derotate = std::conj(acc) / magnitude;

    int errors = 0;
    for (int i = 0; i < 26; ++i) {
        const int bit = hardBit(symbols[at + static_cast<std::size_t>(i)] * derotate, static_cast<std::size_t>(i));
        if (bit != kSofBits[i]) ++errors;
    }
    hit.sofErrors = errors;
    hit.symbolIndex = at;
    hit.found = errors <= 4;
    if (!hit.found) return hit;
    hit.note = "SOF phase estimated. LDPC payload demod is not linked.";
    if (at + 90 > count) return hit;
    int bits[64];
    for (int i = 0; i < 64; ++i)
        bits[i] = hardBit(symbols[at + 26 + static_cast<std::size_t>(i)] * derotate, static_cast<std::size_t>(26 + i));
    decodePls(bits, hit);
    if (hit.plsDecoded)
        hit.note = "PLS MODCOD decoded after one SOF phase estimate. LDPC payload demod is not linked.";
    return hit;
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
