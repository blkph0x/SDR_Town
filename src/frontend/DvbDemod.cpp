#include "frontend/DvbDemod.h"

#include "frontend/DvbSurvey.h"

#include <cmath>
#include <fstream>
#include <vector>

namespace {
// EN 302 307-1 5.5.2.1, 26 bits, hex 18D2E82 in the low 25 bits with a leading 0.
constexpr int kSofBits[26] = {
    0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0
};

int sofBitAt(const std::complex<float>* symbols, std::size_t index) {
    const auto step = symbols[index] * std::conj(symbols[index - 1]);
    return step.imag() < 0.0f ? 1 : 0;
}
}

std::vector<std::complex<float>> modulateDvbs2Sof() {
    std::vector<std::complex<float>> out;
    out.reserve(26);
    std::complex<float> prev{1.0f, 0.0f};
    out.push_back(prev);
    for (int i = 1; i < 26; ++i) {
        std::complex<float> turned{-prev.imag(), prev.real()};
        if (kSofBits[i]) turned = -turned;
        prev = turned;
        out.push_back(prev);
    }
    return out;
}

PlHeaderHit detectDvbs2PlHeader(const std::complex<float>* symbols, std::size_t count) {
    PlHeaderHit hit;
    hit.note = "PL header sync only. LDPC payload demod is not linked.";
    if (!symbols || count < 26) return hit;
    int best = 26;
    std::size_t at = 0;
    for (std::size_t start = 0; start + 26 <= count; ++start) {
        int errors = 0;
        for (int i = 1; i < 26; ++i)
            if (sofBitAt(symbols, start + static_cast<std::size_t>(i)) != kSofBits[i]) ++errors;
        if (errors < best) {
            best = errors;
            at = start;
        }
    }
    hit.sofErrors = best;
    hit.symbolIndex = at;
    hit.found = best <= 4;
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
