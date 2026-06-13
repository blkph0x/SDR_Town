#include <catch2/catch_all.hpp>

#include "P25Control.h"
#include "P25LiveDecoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr double kTestPi = 3.14159265358979323846;

void appendBitsMsb(std::vector<uint8_t>& bits, uint64_t value, int count)
{
    for (int i = count - 1; i >= 0; --i) {
        bits.push_back(static_cast<uint8_t>((value >> i) & 1ull));
    }
}

void writeBitsMsb(std::vector<uint8_t>& bytes, int startBit, int count, uint64_t value)
{
    for (int i = 0; i < count; ++i) {
        const int bit = startBit + count - 1 - i;
        const int byteIndex = bit / 8;
        const int bitInByte = 7 - (bit % 8);
        const uint8_t mask = static_cast<uint8_t>(1u << bitInByte);
        if (value & (1ull << i)) bytes[static_cast<size_t>(byteIndex)] |= mask;
        else bytes[static_cast<size_t>(byteIndex)] &= static_cast<uint8_t>(~mask);
    }
}

std::vector<uint8_t> makeTsbk(uint8_t opcode, uint8_t mfid = 0)
{
    std::vector<uint8_t> b(12, 0);
    writeBitsMsb(b, 0, 1, 0);
    writeBitsMsb(b, 1, 1, 0);
    writeBitsMsb(b, 2, 6, opcode);
    writeBitsMsb(b, 8, 8, mfid);
    return b;
}

uint32_t degree64(uint64_t x)
{
    uint32_t degree = 0;
    while (x >>= 1u) ++degree;
    return degree;
}

void crcDivide(uint64_t& word)
{
    constexpr uint64_t gen = 0b10001000000100001ull;
    while (word != 0) {
        const int diff = static_cast<int>(degree64(word)) - static_cast<int>(degree64(gen));
        if (diff < 0) break;
        word ^= gen << diff;
    }
}

uint16_t p25Crc16(const std::vector<uint8_t>& bytes)
{
    uint64_t word = 0;
    for (size_t i = 0; i < std::min<size_t>(10, bytes.size()); ++i) {
        word = (word << 8) | bytes[i];
        crcDivide(word);
    }
    for (int i = 0; i < 16; ++i) {
        word <<= 1;
        crcDivide(word);
    }
    return static_cast<uint16_t>((word ^ 0xffffu) & 0xffffu);
}

void finishTsbkCrc(std::vector<uint8_t>& tsbk)
{
    const uint16_t crc = p25Crc16(tsbk);
    tsbk[10] = static_cast<uint8_t>((crc >> 8) & 0xffu);
    tsbk[11] = static_cast<uint8_t>(crc & 0xffu);
}

std::vector<int> bytesToDibits(const std::vector<uint8_t>& bytes)
{
    std::vector<int> dibits;
    dibits.reserve(bytes.size() * 4);
    for (uint8_t byte : bytes) {
        dibits.push_back((byte >> 6) & 0x03);
        dibits.push_back((byte >> 4) & 0x03);
        dibits.push_back((byte >> 2) & 0x03);
        dibits.push_back(byte & 0x03);
    }
    return dibits;
}

uint8_t encodePhase2DuidForTest(int duid)
{
    static constexpr uint8_t g[4][8] = {
        {1, 0, 0, 0, 1, 1, 0, 1},
        {0, 1, 0, 0, 1, 0, 1, 1},
        {0, 0, 1, 0, 1, 1, 1, 0},
        {0, 0, 0, 1, 0, 1, 1, 1},
    };
    const uint8_t d[4] = {
        static_cast<uint8_t>((duid >> 3) & 1),
        static_cast<uint8_t>((duid >> 2) & 1),
        static_cast<uint8_t>((duid >> 1) & 1),
        static_cast<uint8_t>(duid & 1),
    };
    uint8_t out = 0;
    for (int col = 0; col < 8; ++col) {
        uint8_t bit = 0;
        for (int row = 0; row < 4; ++row) bit ^= static_cast<uint8_t>(d[row] & g[row][col]);
        out = static_cast<uint8_t>((out << 1) | bit);
    }
    return out;
}

std::vector<int> makeSyntheticPhase2Burst(int duid, bool flipDuidBit = false)
{
    std::vector<int> dibits(P25LiveDecoder::Phase2BurstDibits, 0);
    const auto sync = P25LiveDecoder::phase2FrameSyncDibits();
    std::copy(sync.begin(), sync.end(), dibits.begin());

    uint8_t raw = encodePhase2DuidForTest(duid);
    if (flipDuidBit) raw ^= 0x01u;
    const size_t payload = 10;
    dibits[payload + 10] = (raw >> 6) & 0x03;
    dibits[payload + 47] = (raw >> 4) & 0x03;
    dibits[payload + 132] = (raw >> 2) & 0x03;
    dibits[payload + 169] = raw & 0x03;

    for (const size_t start : {size_t{11}, size_t{48}, size_t{96}, size_t{133}}) {
        for (size_t i = 0; i < 36 && payload + start + i < dibits.size(); ++i) {
            dibits[payload + start + i] = static_cast<int>((i + start) & 0x03);
        }
    }
    return dibits;
}

std::vector<int> makeSyntheticPhase2Superframe()
{
    std::vector<int> dibits(P25LiveDecoder::Phase2BurstDibits * 12, 0);
    const auto sync = P25LiveDecoder::phase2FrameSyncDibits();
    constexpr std::array<size_t, 6> syncSlots{2, 3, 6, 7, 10, 11};
    for (size_t slot = 0; slot < 12; ++slot) {
        const size_t base = slot * P25LiveDecoder::Phase2BurstDibits;
        if (std::find(syncSlots.begin(), syncSlots.end(), slot) != syncSlots.end()) {
            std::copy(sync.begin(), sync.end(), dibits.begin() + static_cast<std::ptrdiff_t>(base));
        }

        const uint8_t raw = encodePhase2DuidForTest(0x0);
        const size_t payload = base + 10;
        dibits[payload + 10] = (raw >> 6) & 0x03;
        dibits[payload + 47] = (raw >> 4) & 0x03;
        dibits[payload + 132] = (raw >> 2) & 0x03;
        dibits[payload + 169] = raw & 0x03;

        for (const size_t start : {size_t{11}, size_t{48}, size_t{96}, size_t{133}}) {
            for (size_t i = 0; i < 36 && payload + start + i < dibits.size(); ++i) {
                dibits[payload + start + i] = static_cast<int>((slot + i + start) & 0x03);
            }
        }
    }
    return dibits;
}

std::vector<int> makeSyntheticMaskedPhase2Superframe(uint16_t nac, uint32_t wacn, uint16_t systemId)
{
    auto dibits = makeSyntheticPhase2Superframe();
    const auto mask = P25LiveDecoder::phase2XorMaskDibits(nac, wacn, systemId);
    constexpr std::array<size_t, 4> duidDibits{10, 47, 132, 169};
    for (size_t slot = 0; slot < 12; ++slot) {
        const size_t base = slot * P25LiveDecoder::Phase2BurstDibits;
        for (size_t i = 0; i < 170; ++i) {
            if (i < 10) continue; // The local burst model overlaps these dibits with the sync pattern.
            if (std::find(duidDibits.begin(), duidDibits.end(), i) != duidDibits.end()) continue;
            dibits[base + 10 + i] ^= mask[slot * P25LiveDecoder::Phase2BurstDibits + i] & 0x03;
        }
    }
    return dibits;
}

std::array<int, P25LiveDecoder::Phase2FrameSyncDibits> makeSyntheticPhase2Isch(uint8_t channel,
                                                                               uint8_t location,
                                                                               bool freeAccess,
                                                                               uint8_t ultraframeCounter)
{
    static constexpr uint64_t kOffset = 0x184229d461ull;
    static constexpr std::array<uint64_t, 9> kRows{
        0x8816ce36d7ull, 0x201dfd4f64ull, 0x100f4b1758ull,
        0x0c00ded18eull, 0x020807f7ffull, 0x09048d9b72ull,
        0x009da3a171ull, 0x0058cbaa4eull, 0x00343d8597ull,
    };

    const uint8_t info = static_cast<uint8_t>(((channel & 0x03u) << 5) |
                                              ((location & 0x03u) << 3) |
                                              ((freeAccess ? 1u : 0u) << 2) |
                                              (ultraframeCounter & 0x03u));
    uint64_t word = kOffset;
    for (size_t row = 0; row < kRows.size(); ++row) {
        if ((info >> (8 - row)) & 1u) word ^= kRows[row];
    }

    std::array<int, P25LiveDecoder::Phase2FrameSyncDibits> dibits{};
    for (size_t i = 0; i < dibits.size(); ++i) {
        const size_t shift = (dibits.size() - 1 - i) * 2;
        dibits[i] = static_cast<int>((word >> shift) & 0x03ull);
    }
    return dibits;
}

std::array<int, 2> p25TrellisPair(int cur, int next)
{
    static constexpr int pairIndexes[4][4] = {
        {0, 15, 12, 3},
        {4, 11, 8, 7},
        {13, 2, 1, 14},
        {9, 6, 5, 10},
    };
    static constexpr int pairs[16][2] = {
        {0b00, 0b10}, {0b10, 0b10}, {0b01, 0b11}, {0b11, 0b11},
        {0b11, 0b10}, {0b01, 0b10}, {0b10, 0b11}, {0b00, 0b11},
        {0b11, 0b01}, {0b01, 0b01}, {0b10, 0b00}, {0b00, 0b00},
        {0b00, 0b01}, {0b10, 0b01}, {0b01, 0b00}, {0b11, 0b00},
    };
    const int idx = pairIndexes[cur & 0x03][next & 0x03];
    return {pairs[idx][0], pairs[idx][1]};
}

std::vector<int> trellisEncodeHalfRate(const std::vector<int>& decodedDibits)
{
    std::vector<int> encoded;
    encoded.reserve((decodedDibits.size() + 1) * 2);
    int state = 0;
    for (int d : decodedDibits) {
        const auto pair = p25TrellisPair(state, d & 0x03);
        encoded.push_back(pair[0]);
        encoded.push_back(pair[1]);
        state = d & 0x03;
    }
    const auto finish = p25TrellisPair(state, 0);
    encoded.push_back(finish[0]);
    encoded.push_back(finish[1]);
    return encoded;
}

std::vector<int> interleaveTsbkCodedDibits(const std::vector<int>& deinterleaved)
{
    static constexpr std::array<size_t, 98> redirects = {
        0, 1, 8, 9, 16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57,
        64, 65, 72, 73, 80, 81, 88, 89, 96, 97, 2, 3, 10, 11, 18, 19,
        26, 27, 34, 35, 42, 43, 50, 51, 58, 59, 66, 67, 74, 75, 82, 83,
        90, 91, 4, 5, 12, 13, 20, 21, 28, 29, 36, 37, 44, 45, 52, 53,
        60, 61, 68, 69, 76, 77, 84, 85, 92, 93, 6, 7, 14, 15, 22, 23,
        30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 70, 71, 78, 79, 86, 87,
        94, 95
    };
    std::vector<int> out;
    out.reserve(redirects.size());
    for (size_t idx : redirects) out.push_back(deinterleaved[idx] & 0x03);
    return out;
}

void appendDibitBits(std::vector<uint8_t>& bits, int dibit)
{
    const auto pair = P25LiveDecoder::bitsFromDibit(dibit);
    bits.push_back(pair[0]);
    bits.push_back(pair[1]);
}

std::vector<int> makeNidDibits(uint16_t nac, P25DataUnitId duid)
{
    std::vector<int> dibits;
    dibits.reserve(32);
    const uint64_t word = p25EncodeNidBch(nac, duid);
    for (int i = 31; i >= 0; --i) {
        dibits.push_back(static_cast<int>((word >> (i * 2)) & 0x03u));
    }
    return dibits;
}

void appendInvertedPolarityFalseSync(std::vector<uint8_t>& bits)
{
    const auto sync = P25LiveDecoder::frameSyncBits();
    for (size_t i = 0; i + 1 < sync.size(); i += 2) {
        const int wantedInvertedDibit = P25LiveDecoder::dibitFromBits(sync[i], sync[i + 1]);
        appendDibitBits(bits, wantedInvertedDibit ^ 0x02);
    }

    for (int i = 0; i < 40; ++i) {
        appendDibitBits(bits, (i % 3) + 1);
    }
}

std::vector<int> statusInterleaveAfterSync(const std::vector<int>& dataDibits)
{
    std::vector<int> out;
    int pos = 24;
    size_t data = 0;
    while (data < dataDibits.size()) {
        pos = (pos + 1) % 36;
        if (pos == 0) out.push_back(0b11);
        else out.push_back(dataDibits[data++] & 0x03);
    }
    return out;
}

class TestP25PseudoRandom {
public:
    explicit TestP25PseudoRandom(uint16_t seed)
        : state(static_cast<uint16_t>((seed & 0x0fffu) << 4))
    {
    }

    uint32_t nextBits(int bits)
    {
        uint32_t out = 0;
        for (int i = 0; i < bits; ++i) {
            state = static_cast<uint16_t>(state * 173u + 13849u);
            out = (out << 1) | static_cast<uint32_t>((state >> 15) & 1u);
        }
        return out;
    }

private:
    uint16_t state = 0;
};

struct TestVoiceZigZag {
    int start = 0;
    int count = 0;
    bool high = true;
};

std::vector<std::pair<int, bool>> voiceWordPositions(int wordIndex)
{
    static const std::array<std::vector<TestVoiceZigZag>, 8> paths = {
        std::vector<TestVoiceZigZag>{{0, 23, true}},
        std::vector<TestVoiceZigZag>{{69, 1, false}, {0, 22, false}},
        std::vector<TestVoiceZigZag>{{66, 2, false}, {1, 21, true}},
        std::vector<TestVoiceZigZag>{{64, 3, false}, {1, 20, false}},
        std::vector<TestVoiceZigZag>{{61, 4, false}, {2, 11, true}},
        std::vector<TestVoiceZigZag>{{35, 13, false}, {2, 2, false}},
        std::vector<TestVoiceZigZag>{{8, 15, false}},
        std::vector<TestVoiceZigZag>{{53, 7, true}},
    };

    std::vector<std::pair<int, bool>> out;
    for (const auto& segment : paths[static_cast<size_t>(wordIndex)]) {
        int index = segment.start;
        bool high = segment.high;
        for (int i = 0; i < segment.count; ++i) {
            out.emplace_back(index, high);
            index += 3;
            high = !high;
        }
    }
    return out;
}

void writeVoiceWord(std::vector<int>& dibits, int wordIndex, uint32_t word)
{
    const auto positions = voiceWordPositions(wordIndex);
    for (size_t i = 0; i < positions.size(); ++i) {
        const int bit = static_cast<int>((word >> (positions.size() - 1 - i)) & 1u);
        const auto [index, high] = positions[i];
        if (high) dibits[static_cast<size_t>(index)] = (dibits[static_cast<size_t>(index)] & 0x01) | (bit << 1);
        else dibits[static_cast<size_t>(index)] = (dibits[static_cast<size_t>(index)] & 0x02) | bit;
    }
}

std::vector<int> makeZeroImbeVoiceFrameDibits()
{
    std::vector<int> dibits(72, 0);
    writeVoiceWord(dibits, 0, 0);
    TestP25PseudoRandom prng(0);
    for (int i = 1; i <= 3; ++i) writeVoiceWord(dibits, i, prng.nextBits(23));
    for (int i = 4; i <= 6; ++i) writeVoiceWord(dibits, i, prng.nextBits(15));
    writeVoiceWord(dibits, 7, 0);
    return dibits;
}

std::vector<uint8_t> makeSyntheticLduFrameBits(uint16_t nac, P25DataUnitId duid)
{
    std::vector<uint8_t> bits;
    appendBitsMsb(bits, 0x55u, 8);
    const auto sync = P25LiveDecoder::frameSyncBits();
    bits.insert(bits.end(), sync.begin(), sync.end());

    std::vector<int> payload = makeNidDibits(nac, duid);
    const auto voice = makeZeroImbeVoiceFrameDibits();
    for (int frame = 0; frame < 9; ++frame) {
        payload.insert(payload.end(), voice.begin(), voice.end());
        if (frame >= 1 && frame <= 6) payload.insert(payload.end(), 20, 0);
        else if (frame == 7) payload.insert(payload.end(), 8, 0);
    }

    for (int dibit : statusInterleaveAfterSync(payload)) appendDibitBits(bits, dibit);
    return bits;
}

std::vector<uint8_t> makeSyntheticTsduFrameBits(uint16_t nac, const std::vector<uint8_t>& tsbk)
{
    std::vector<uint8_t> bits;
    appendBitsMsb(bits, 0x2aau, 8); // leading junk before sync
    const auto sync = P25LiveDecoder::frameSyncBits();
    bits.insert(bits.end(), sync.begin(), sync.end());

    std::vector<int> payload = makeNidDibits(nac, P25DataUnitId::TSDU);
    auto codedTsbk = trellisEncodeHalfRate(bytesToDibits(tsbk));
    REQUIRE(codedTsbk.size() == 98);
    codedTsbk = interleaveTsbkCodedDibits(codedTsbk);
    payload.insert(payload.end(), codedTsbk.begin(), codedTsbk.end());

    for (int dibit : statusInterleaveAfterSync(payload)) appendDibitBits(bits, dibit);
    return bits;
}

std::vector<float> makeC4fmDiscriminator(const std::vector<uint8_t>& bits, double innerDeviationHz)
{
    std::vector<float> fm;
    constexpr int samplesPerSymbol = 10;
    fm.reserve((bits.size() / 2) * samplesPerSymbol);
    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        const int dibit = P25LiveDecoder::dibitFromBits(bits[i], bits[i + 1]);
        const double level = P25LiveDecoder::nominalC4fmLevelForDibit(dibit);
        for (int s = 0; s < samplesPerSymbol; ++s) {
            const double deterministicNoise = ((s % 5) - 2) * 2.0;
            fm.push_back(static_cast<float>(35.0 + level * innerDeviationHz + deterministicNoise));
        }
    }
    return fm;
}

std::vector<float> makeC4fmDiscriminatorTransformed(const std::vector<uint8_t>& bits,
                                                    double innerDeviationHz,
                                                    bool reverseBitOrder)
{
    std::vector<float> fm;
    constexpr int samplesPerSymbol = 10;
    fm.reserve((bits.size() / 2) * samplesPerSymbol);
    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        int dibit = P25LiveDecoder::dibitFromBits(bits[i], bits[i + 1]);
        if (reverseBitOrder) dibit = ((dibit & 0x01) << 1) | ((dibit >> 1) & 0x01);
        const double level = P25LiveDecoder::nominalC4fmLevelForDibit(dibit);
        for (int s = 0; s < samplesPerSymbol; ++s) {
            const double deterministicNoise = ((s % 7) - 3) * 1.7;
            fm.push_back(static_cast<float>(20.0 + level * innerDeviationHz + deterministicNoise));
        }
    }
    return fm;
}

std::vector<std::complex<float>> makeC4fmIq(const std::vector<uint8_t>& bits,
                                            double sampleRate,
                                            double carrierOffsetHz,
                                            double innerDeviationHz,
                                            double fractionalSymbolOffset)
{
    std::vector<double> levels;
    levels.reserve(bits.size() / 2);
    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        const int dibit = P25LiveDecoder::dibitFromBits(bits[i], bits[i + 1]);
        levels.push_back(P25LiveDecoder::nominalC4fmLevelForDibit(dibit));
    }

    const double samplesPerSymbol = sampleRate / P25LiveDecoder::SymbolRate;
    const size_t sampleCount = static_cast<size_t>(std::ceil((static_cast<double>(levels.size()) + 8.0) * samplesPerSymbol));
    std::vector<std::complex<float>> iq;
    iq.reserve(sampleCount);
    double phase = 0.0;
    for (size_t n = 0; n < sampleCount; ++n) {
        const double symPos = static_cast<double>(n) / samplesPerSymbol - fractionalSymbolOffset;
        const int sym = static_cast<int>(std::floor(symPos));
        const double level = (sym >= 0 && static_cast<size_t>(sym) < levels.size())
            ? levels[static_cast<size_t>(sym)]
            : 0.0;
        const double wobble = 12.0 * std::sin(2.0 * kTestPi * static_cast<double>(n) / (samplesPerSymbol * 23.0));
        const double freqHz = carrierOffsetHz + level * innerDeviationHz + wobble;
        phase += 2.0 * kTestPi * freqHz / sampleRate;
        const double amp = 0.82 + 0.08 * std::sin(2.0 * kTestPi * static_cast<double>(n) / (samplesPerSymbol * 17.0));
        iq.emplace_back(static_cast<float>(amp * std::cos(phase)), static_cast<float>(amp * std::sin(phase)));
    }
    return iq;
}

std::vector<std::complex<float>> makeCqpskIq(const std::vector<uint8_t>& bits,
                                             double sampleRate,
                                             double carrierOffsetHz,
                                             double fractionalSymbolOffset)
{
    std::vector<int> dibits;
    dibits.reserve(bits.size() / 2);
    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        dibits.push_back(P25LiveDecoder::dibitFromBits(bits[i], bits[i + 1]));
    }

    static constexpr std::array<double, 4> kPhaseStepByDibit{
        kTestPi * 0.25,   // 00
        kTestPi * 0.75,   // 01
        -kTestPi * 0.25,  // 10
        -kTestPi * 0.75,  // 11
    };
    const double samplesPerSymbol = sampleRate / P25LiveDecoder::SymbolRate;
    const size_t sampleCount = static_cast<size_t>(std::ceil((static_cast<double>(dibits.size()) + 8.0) * samplesPerSymbol));
    std::vector<std::complex<float>> iq;
    iq.reserve(sampleCount);

    double carrierPhase = 0.0;
    double dataPhase = 0.0;
    int currentSym = -1;
    for (size_t n = 0; n < sampleCount; ++n) {
        const double symPos = static_cast<double>(n) / samplesPerSymbol - fractionalSymbolOffset;
        const int sym = static_cast<int>(std::floor(symPos));
        if (sym != currentSym) {
            currentSym = sym;
            if (sym >= 0 && static_cast<size_t>(sym) < dibits.size()) {
                dataPhase += kPhaseStepByDibit[static_cast<size_t>(dibits[static_cast<size_t>(sym)] & 0x03)];
            }
        }
        carrierPhase += 2.0 * kTestPi * carrierOffsetHz / sampleRate;
        const double amp = 0.86 + 0.05 * std::sin(2.0 * kTestPi * static_cast<double>(n) / (samplesPerSymbol * 29.0));
        const double phase = carrierPhase + dataPhase;
        iq.emplace_back(static_cast<float>(amp * std::cos(phase)), static_cast<float>(amp * std::sin(phase)));
    }
    return iq;
}

} // namespace

TEST_CASE("P25 live decoder finds frame sync, NID, and raw TSDU block candidates")
{
    auto iden = makeTsbk(0x3d);
    writeBitsMsb(iden, 16, 4, 2);
    writeBitsMsb(iden, 29, 9, 0x100u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(450000000.0 / 5.0));
    finishTsbkCrc(iden);

    P25LiveDecoder decoder;
    const auto bits = makeSyntheticTsduFrameBits(0x293, iden);
    const auto result = decoder.processHardBits(bits);

    REQUIRE_FALSE(result.syncs.empty());
    REQUIRE(result.syncs.front().bitOffset == 8);
    REQUIRE_FALSE(result.syncs.front().inverted);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().valid);
    REQUIRE(result.nids.front().nac == 0x293);
    REQUIRE(result.nids.front().duid == P25DataUnitId::TSDU);
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().correctedBitErrors == 0);
    REQUIRE(result.rawTsbkBlocks.size() == 1);
    REQUIRE(result.rawTsbkBlocks.front().bytes == iden);
    REQUIRE(result.rawTsbkBlocks.front().fecDecoded);
    REQUIRE(result.rawTsbkBlocks.front().crcValid);

    P25ControlChannelAnalyzer analyzer;
    const auto events = analyzer.ingestTsbk(result.rawTsbkBlocks.front().bytes);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().type == P25ControlEventType::IdentifierUpdate);
}

TEST_CASE("P25 live decoder locks synthetic C4FM discriminator symbols")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x123, tsbk);
    const auto fm = makeC4fmDiscriminator(bits, 600.0);

    P25LiveDecoder decoder;
    const auto result = decoder.processFmDiscriminator(fm, 48000.0);

    REQUIRE_FALSE(result.syncs.empty());
    REQUIRE(result.stats.symbols >= bits.size() / 2);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().nac == 0x123);
    REQUIRE(result.nids.front().duid == P25DataUnitId::TSDU);
}

TEST_CASE("P25 live decoder handles reversed dibit bit order discriminator symbols")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x321, tsbk);
    const auto fm = makeC4fmDiscriminatorTransformed(bits, 600.0, true);

    P25LiveDecoder decoder;
    const auto result = decoder.processFmDiscriminator(fm, 48000.0);

    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().nac == 0x321);
    REQUIRE_FALSE(result.rawTsbkBlocks.empty());
    REQUIRE(result.rawTsbkBlocks.front().crcValid);
}

TEST_CASE("P25 live decoder locks offset RTL-rate synthetic C4FM IQ")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x456, tsbk);

    constexpr double sampleRate = 2048000.0;
    constexpr double centerHz = 419112500.0;
    constexpr double carrierOffsetHz = 13250.0;
    const auto iq = makeC4fmIq(bits, sampleRate, carrierOffsetHz, 600.0, 0.37);

    P25LiveDecoder decoder;
    const auto result = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);

    REQUIRE(result.stats.sampleRate == Catch::Approx(48000.0).margin(1.0));
    REQUIRE(result.stats.symbols > 120);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().nac == 0x456);
    REQUIRE_FALSE(result.rawTsbkBlocks.empty());
    REQUIRE(result.rawTsbkBlocks.front().crcValid);
}

TEST_CASE("P25 live decoder locks offset RTL-rate synthetic CQPSK LSM IQ")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x789, tsbk);

    constexpr double sampleRate = 2048000.0;
    constexpr double centerHz = 419112500.0;
    constexpr double carrierOffsetHz = -7250.0;
    const auto iq = makeCqpskIq(bits, sampleRate, carrierOffsetHz, 0.42);

    P25LiveDecoder decoder;
    const auto result = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);

    REQUIRE(result.stats.sampleRate == Catch::Approx(48000.0).margin(1.0));
    REQUIRE(result.stats.symbols > 120);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().nac == 0x789);
    REQUIRE_FALSE(result.rawTsbkBlocks.empty());
    REQUIRE(result.rawTsbkBlocks.front().crcValid);
    REQUIRE(result.stats.demodPath.find("CQPSK") != std::string::npos);
}

TEST_CASE("P25 live decoder prefers validated NID branch over more false syncs")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);

    std::vector<uint8_t> bits;
    appendInvertedPolarityFalseSync(bits);
    const auto validFrame = makeSyntheticTsduFrameBits(0x123, tsbk);
    bits.insert(bits.end(), validFrame.begin(), validFrame.end());

    const auto fm = makeC4fmDiscriminator(bits, 600.0);
    P25LiveDecoder decoder;
    const auto result = decoder.processFmDiscriminator(fm, 48000.0);

    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().nac == 0x123);
    REQUIRE_FALSE(result.rawTsbkBlocks.empty());
    REQUIRE(result.rawTsbkBlocks.front().crcValid);
}

TEST_CASE("P25 live decoder marks inverted hard-bit frame syncs")
{
    auto tsbk = makeTsbk(0x3d);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x456, tsbk);
    std::vector<uint8_t> inverted = bits;
    for (auto& bit : inverted) bit ^= 1u;

    P25LiveDecoder decoder;
    const auto result = decoder.processHardBits(inverted);

    REQUIRE_FALSE(result.syncs.empty());
    REQUIRE(result.syncs.front().inverted);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().nac == 0x456);
}

TEST_CASE("P25 live decoder corrects BCH-protected NID bit errors")
{
    auto tsbk = makeTsbk(0x3d);
    finishTsbkCrc(tsbk);
    auto bits = makeSyntheticTsduFrameBits(0x293, tsbk);

    const auto sync = P25LiveDecoder::frameSyncBits();
    const auto it = std::search(bits.begin(), bits.end(), sync.begin(), sync.end());
    REQUIRE(it != bits.end());
    const size_t nidBit = static_cast<size_t>(std::distance(bits.begin(), it)) +
        P25LiveDecoder::FrameSyncBits + 7;
    REQUIRE(nidBit < bits.size());
    bits[nidBit] ^= 1u;

    P25LiveDecoder decoder;
    const auto result = decoder.processHardBits(bits);

    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().valid);
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().correctedBitErrors == 1);
    REQUIRE(result.nids.front().nac == 0x293);
    REQUIRE(result.nids.front().duid == P25DataUnitId::TSDU);
    REQUIRE(result.rawTsbkBlocks.size() == 1);
}

TEST_CASE("P25 live decoder extracts Phase 2 4V burst voice codewords")
{
    P25LiveDecoder decoder;
    const auto dibits = makeSyntheticPhase2Burst(0x0);

    const auto bursts = decoder.processPhase2HardDibits(dibits);
    REQUIRE(bursts.size() == 1);
    const auto& burst = bursts.front();
    REQUIRE(burst.valid);
    REQUIRE(burst.syncErrors == 0);
    REQUIRE(burst.duid == 0x0);
    REQUIRE(burst.duidErrors == 0);
    REQUIRE(burst.kind == P25Phase2BurstKind::Voice4);
    REQUIRE(burst.voiceCodewords.size() == 4);
    REQUIRE(burst.voiceCodewords.front().dibitOffset == 21);

    const auto result = decoder.processHardDibits(dibits);
    REQUIRE(result.stats.phase2Bursts == 1);
    REQUIRE(result.stats.phase2VoiceCodewords == 4);
    REQUIRE(result.stats.phase2SuperframeBursts == 0);
    REQUIRE(result.stats.bestPhase2SyncErrors == 0);
}

TEST_CASE("P25 live decoder corrects Phase 2 DUID and extracts 2V bursts")
{
    P25LiveDecoder decoder;
    const auto dibits = makeSyntheticPhase2Burst(0x6, true);

    const auto bursts = decoder.processPhase2HardDibits(dibits);
    REQUIRE(bursts.size() == 1);
    const auto& burst = bursts.front();
    REQUIRE(burst.duid == 0x6);
    REQUIRE(burst.duidErrors == 1);
    REQUIRE(burst.kind == P25Phase2BurstKind::Voice2);
    REQUIRE(burst.voiceCodewords.size() == 2);
}

TEST_CASE("P25 live decoder locks Phase 2 superframes and annotates all TDMA bursts")
{
    P25LiveDecoder decoder;
    const auto dibits = makeSyntheticPhase2Superframe();

    const auto bursts = decoder.processPhase2HardDibits(dibits);
    REQUIRE(bursts.size() == 12);
    for (size_t slot = 0; slot < bursts.size(); ++slot) {
        const auto& burst = bursts[slot];
        REQUIRE(burst.valid);
        REQUIRE(burst.superframeLocked);
        REQUIRE(burst.superframeDibitOffset == 0);
        REQUIRE(burst.tdmaSlotKnown);
        REQUIRE(burst.tdmaSlotId == slot);
        REQUIRE(burst.duid == 0x0);
        REQUIRE(burst.kind == P25Phase2BurstKind::Voice4);
        REQUIRE(burst.voiceCodewords.size() == 4);
    }
    REQUIRE(bursts[0].syncErrors == -1);
    REQUIRE(bursts[2].syncErrors == 0);

    const auto result = decoder.processHardDibits(dibits);
    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2SuperframeBursts == 12);
    REQUIRE(result.stats.phase2VoiceCodewords == 48);
    REQUIRE(result.stats.phase2MaskedBursts == 0);
    REQUIRE(result.stats.phase2IschSync == 6);
}

TEST_CASE("P25 Phase 2 XOR mask generation matches known local-system vector")
{
    const auto mask = P25LiveDecoder::phase2XorMaskDibits(0x2d2, 0xbee00, 0x2d1);
    REQUIRE(mask.size() == P25LiveDecoder::Phase2BurstDibits * 12);
    const std::array<int, 24> expected{
        2, 3, 3, 2, 3, 2, 0, 0, 0, 0, 0, 2,
        3, 1, 0, 1, 0, 2, 3, 1, 0, 2, 2, 0,
    };
    for (size_t i = 0; i < expected.size(); ++i) REQUIRE(mask[i] == expected[i]);
}

TEST_CASE("P25 live decoder applies Phase 2 XOR mask before extracting voice codewords")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    const auto clear = makeSyntheticPhase2Superframe();
    const auto masked = makeSyntheticMaskedPhase2Superframe(nac, wacn, systemId);

    P25LiveDecoder clearDecoder;
    const auto clearBursts = clearDecoder.processPhase2HardDibits(clear);
    REQUIRE(clearBursts.size() == 12);

    P25LiveDecoder maskedDecoder;
    maskedDecoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto maskedResult = maskedDecoder.processHardDibits(masked);
    REQUIRE(maskedResult.phase2Bursts.size() == 12);
    REQUIRE(maskedResult.stats.phase2MaskedBursts == 12);
    REQUIRE(maskedResult.stats.phase2VoiceCodewords == 48);
    REQUIRE_FALSE(maskedResult.stats.phase2EssKnown);
    for (size_t slot = 0; slot < maskedResult.phase2Bursts.size(); ++slot) {
        const auto& burst = maskedResult.phase2Bursts[slot];
        REQUIRE(burst.xorMaskApplied);
        REQUIRE(burst.voiceCodewords.size() == clearBursts[slot].voiceCodewords.size());
        REQUIRE(burst.voiceCodewords.front().bits == clearBursts[slot].voiceCodewords.front().bits);
    }
}

TEST_CASE("P25 live decoder decodes informational Phase 2 ISCH fields")
{
    auto dibits = makeSyntheticPhase2Superframe();
    const auto isch = makeSyntheticPhase2Isch(2, 1, true, 3);
    std::copy(isch.begin(), isch.end(), dibits.begin());

    P25LiveDecoder decoder;
    const auto result = decoder.processHardDibits(dibits);

    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2IschDecoded >= 7);
    REQUIRE(result.stats.phase2IschSync == 6);
    REQUIRE_FALSE(result.phase2Bursts.empty());
    const auto& burst = result.phase2Bursts.front();
    REQUIRE(burst.isch.valid);
    REQUIRE_FALSE(burst.isch.sync);
    REQUIRE(burst.isch.errors == 0);
    REQUIRE(burst.isch.channel == 2);
    REQUIRE(burst.isch.location == 1);
    REQUIRE(burst.isch.freeAccess);
    REQUIRE(burst.isch.ultraframeCounter == 3);
}

TEST_CASE("P25 Phase 2 voice codewords map into AMBE frame layout")
{
    P25Phase2VoiceCodeword codeword;
    codeword.bits[0] = 1;
    codeword.bits[1] = 1;
    codeword.bits[2] = 1;
    codeword.bits[3] = 1;

    const auto ambe = p25Phase2VoiceCodewordToAmbe3600x2450Frame(codeword);
    REQUIRE(ambe[23] == 1);
    REQUIRE(ambe[5] == 1);
    REQUIRE(ambe[24 + 10] == 1);
    REQUIRE(ambe[48 + 3] == 1);
    REQUIRE(std::count(ambe.begin(), ambe.end(), static_cast<uint8_t>(1)) == 4);
}

TEST_CASE("P25 live decoder extracts valid IMBE frames from synthetic LDU")
{
    const auto bits = makeSyntheticLduFrameBits(0x293, P25DataUnitId::LDU1);
    P25LiveDecoder decoder;
    const auto result = decoder.processHardBits(bits);

    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().duid == P25DataUnitId::LDU1);
    REQUIRE(result.imbeFrames.size() == 9);
    for (const auto& frame : result.imbeFrames) {
        REQUIRE(frame.valid);
        REQUIRE(frame.correctedErrors == 0);
        for (uint8_t byte : frame.imbe88) REQUIRE(byte == 0);
    }

    P25ImbeVoiceDecoder voice;
    REQUIRE(voice.backendAvailable());
    const auto audio = voice.decodeImbe4400Frame(result.imbeFrames.front().imbe88);
    REQUIRE(audio.status == P25VoiceDecodeStatus::Decoded);
    REQUIRE(audio.pcm.size() == 160);
}

TEST_CASE("P25 IMBE voice decoder reports backend availability explicitly")
{
    P25ImbeVoiceDecoder voice;
    std::array<uint8_t, 11> emptyFrame{};
    const auto result = voice.decodeImbe4400Frame(emptyFrame);
    if (voice.backendAvailable()) {
        REQUIRE(result.status == P25VoiceDecodeStatus::Decoded);
        REQUIRE(result.sampleRate == Catch::Approx(8000.0));
        REQUIRE_FALSE(result.pcm.empty());
    } else {
        REQUIRE(result.status == P25VoiceDecodeStatus::BackendUnavailable);
        REQUIRE(result.pcm.empty());
    }
}

TEST_CASE("P25 AMBE Phase 2 voice decoder reports backend availability explicitly")
{
    P25AmbeVoiceDecoder voice;
    std::array<uint8_t, 96> emptyFrame{};
    const auto result = voice.decodeAmbe3600x2450Frame(emptyFrame);
    if (voice.backendAvailable()) {
        REQUIRE(result.status == P25VoiceDecodeStatus::Decoded);
        REQUIRE(result.sampleRate == Catch::Approx(8000.0));
        REQUIRE(result.pcm.size() == 160);
    } else {
        REQUIRE(result.status == P25VoiceDecodeStatus::BackendUnavailable);
        REQUIRE(result.pcm.empty());
    }
}
