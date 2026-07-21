#include <catch2/catch_all.hpp>

#include "P25Control.h"
#include "P25LiveDecoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
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
    REQUIRE(tsbk.size() == 12);
    static constexpr std::array<uint16_t, 80> checksums = {
        0x1bcb, 0x8de5, 0xc6f2, 0x6b69, 0xb5b4, 0x52ca, 0x2175, 0x90ba,
        0x404d, 0xa026, 0x5803, 0xac01, 0xd600, 0x6310, 0x3998, 0x14dc,
        0x027e, 0x092f, 0x8497, 0xc24b, 0xe125, 0xf092, 0x7059, 0xb82c,
        0x5406, 0x2213, 0x9109, 0xc884, 0x6c52, 0x3e39, 0x9f1c, 0x479e,
        0x2bdf, 0x95ef, 0xcaf7, 0xe57b, 0xf2bd, 0xf95e, 0x74bf, 0xba5f,
        0xdd2f, 0xee97, 0xf74b, 0xfba5, 0xfdd2, 0x76f9, 0xbb7c, 0x55ae,
        0x22c7, 0x9163, 0xc8b1, 0xe458, 0x7a3c, 0x350e, 0x1297, 0x894b,
        0xc4a5, 0xe252, 0x7939, 0xbc9c, 0x565e, 0x233f, 0x919f, 0xc8cf,
        0xe467, 0xf233, 0xf919, 0xfc8c, 0x7656, 0x333b, 0x999d, 0xccce,
        0x6e77, 0xb73b, 0xdb9d, 0xedce, 0x7ef7, 0xbf7b, 0xdfbd, 0xefde,
    };
    uint16_t crc = 0xffffu;
    for (size_t bit = 0; bit < 80; ++bit) {
        const size_t byteIndex = bit / 8;
        const int bitInByte = 7 - static_cast<int>(bit % 8);
        if (((tsbk[byteIndex] >> bitInByte) & 0x01u) != 0) {
            crc ^= checksums[bit];
        }
    }
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
    const size_t payload = P25LiveDecoder::Phase2FrameSyncDibits;
    dibits[payload + 0] = (raw >> 6) & 0x03;
    dibits[payload + 37] = (raw >> 4) & 0x03;
    dibits[payload + 122] = (raw >> 2) & 0x03;
    dibits[payload + 159] = raw & 0x03;

    for (const size_t start : {size_t{1}, size_t{38}, size_t{86}, size_t{123}}) {
        for (size_t i = 0; i < 36 && payload + start + i < dibits.size(); ++i) {
            dibits[payload + start + i] = static_cast<int>((i + start) & 0x03);
        }
    }
    return dibits;
}

std::array<int, P25LiveDecoder::Phase2FrameSyncDibits> phase2DibitsFromWordForTest(uint64_t word)
{
    std::array<int, P25LiveDecoder::Phase2FrameSyncDibits> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t shift = (out.size() - 1u - i) * 2u;
        out[i] = static_cast<int>((word >> shift) & 0x03ull);
    }
    return out;
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
        const size_t payload = base + P25LiveDecoder::Phase2FrameSyncDibits;
        dibits[payload + 0] = (raw >> 6) & 0x03;
        dibits[payload + 37] = (raw >> 4) & 0x03;
        dibits[payload + 122] = (raw >> 2) & 0x03;
        dibits[payload + 159] = raw & 0x03;

        for (const size_t start : {size_t{1}, size_t{38}, size_t{86}, size_t{123}}) {
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
    constexpr std::array<size_t, 4> duidDibits{0, 37, 122, 159};
    for (size_t slot = 0; slot < 12; ++slot) {
        const size_t base = slot * P25LiveDecoder::Phase2BurstDibits;
        for (size_t payloadDibit = 0; payloadDibit < 160; ++payloadDibit) {
            if (std::find(duidDibits.begin(), duidDibits.end(), payloadDibit) != duidDibits.end()) continue;
            dibits[base + P25LiveDecoder::Phase2FrameSyncDibits + payloadDibit] ^=
                mask[slot * P25LiveDecoder::Phase2BurstDibits + payloadDibit] & 0x03;
        }
    }
    return dibits;
}

uint8_t gf64MulForTest(uint8_t a, uint8_t b)
{
    a &= 0x3fu;
    b &= 0x3fu;
    uint16_t product = 0;
    for (int i = 0; i < 6; ++i) {
        if ((b >> i) & 1u) product ^= static_cast<uint16_t>(a) << i;
    }
    for (int i = 10; i >= 6; --i) {
        if ((product >> i) & 1u) product ^= static_cast<uint16_t>(0x43u) << (i - 6);
    }
    return static_cast<uint8_t>(product & 0x3fu);
}

uint8_t gf64PowForTest(uint8_t a, int power)
{
    uint8_t out = 1;
    while (power-- > 0) out = gf64MulForTest(out, a);
    return out;
}

uint8_t gf64InvForTest(uint8_t a)
{
    if ((a & 0x3fu) == 0) return 0;
    return gf64PowForTest(a, 62);
}

std::array<uint8_t, 28> rs63RemainderForTest(const std::array<uint8_t, 63>& codeword)
{
    struct Tables {
        std::array<int, 64> alphaTo{};
        std::array<int, 64> indexOf{};
    };
    static const Tables tables = [] {
        Tables t;
        t.indexOf.fill(-1);
        constexpr int MM = 6;
        constexpr int NN = 63;
        constexpr std::array<int, 7> generatorPolynomial{1, 1, 0, 0, 0, 0, 1};
        int mask = 1;
        t.alphaTo[MM] = 0;
        for (int i = 0; i < MM; ++i) {
            t.alphaTo[static_cast<size_t>(i)] = mask;
            t.indexOf[static_cast<size_t>(t.alphaTo[static_cast<size_t>(i)])] = i;
            if (generatorPolynomial[static_cast<size_t>(i)] != 0) t.alphaTo[MM] ^= mask;
            mask <<= 1;
        }
        t.indexOf[static_cast<size_t>(t.alphaTo[MM])] = MM;
        mask >>= 1;
        for (int i = MM + 1; i < NN; ++i) {
            if (t.alphaTo[static_cast<size_t>(i - 1)] >= mask) {
                t.alphaTo[static_cast<size_t>(i)] = t.alphaTo[MM] ^
                    ((t.alphaTo[static_cast<size_t>(i - 1)] ^ mask) << 1);
            } else {
                t.alphaTo[static_cast<size_t>(i)] = t.alphaTo[static_cast<size_t>(i - 1)] << 1;
            }
            t.indexOf[static_cast<size_t>(t.alphaTo[static_cast<size_t>(i)])] = i;
        }
        t.indexOf[0] = -1;
        return t;
    }();

    std::array<uint8_t, 28> rem{};
    for (int root = 1; root <= 28; ++root) {
        int syndrome = 0;
        for (int symbol = 0; symbol < 63; ++symbol) {
            const int value = static_cast<int>(codeword[static_cast<size_t>(symbol)] & 0x3fu);
            const int index = tables.indexOf[static_cast<size_t>(value)];
            if (index != -1) {
                syndrome ^= tables.alphaTo[static_cast<size_t>((index + root * symbol) % 63)];
            }
        }
        rem[static_cast<size_t>(root - 1)] = static_cast<uint8_t>(syndrome & 0x3f);
    }
    return rem;
}

std::array<uint8_t, 63> rs63SolveErasuresForTest(std::array<uint8_t, 63> symbols,
                                                 const std::vector<int>& erasures)
{
    REQUIRE(erasures.size() <= 28);

    for (int pos : erasures) {
        REQUIRE(pos >= 0);
        REQUIRE(pos < static_cast<int>(symbols.size()));
        symbols[static_cast<size_t>(pos)] = 0;
    }

    const auto base = rs63RemainderForTest(symbols);
    std::vector<std::vector<uint8_t>> matrix(28, std::vector<uint8_t>(erasures.size() + 1, 0));
    for (size_t c = 0; c < erasures.size(); ++c) {
        std::array<uint8_t, 63> unit{};
        unit[static_cast<size_t>(erasures[c])] = 1;
        const auto rem = rs63RemainderForTest(unit);
        for (size_t r = 0; r < rem.size(); ++r) matrix[r][c] = rem[r] & 0x3fu;
    }
    for (size_t r = 0; r < base.size(); ++r) matrix[r][erasures.size()] = base[r] & 0x3fu;

    size_t rank = 0;
    std::vector<size_t> pivotCol;
    for (size_t col = 0; col < erasures.size() && rank < matrix.size(); ++col) {
        size_t pivot = rank;
        while (pivot < matrix.size() && matrix[pivot][col] == 0) ++pivot;
        REQUIRE(pivot < matrix.size());
        if (pivot != rank) std::swap(matrix[pivot], matrix[rank]);

        const uint8_t inv = gf64InvForTest(matrix[rank][col]);
        REQUIRE(inv != 0);
        for (size_t j = col; j <= erasures.size(); ++j) {
            matrix[rank][j] = gf64MulForTest(matrix[rank][j], inv);
        }
        for (size_t r = 0; r < matrix.size(); ++r) {
            if (r == rank || matrix[r][col] == 0) continue;
            const uint8_t scale = matrix[r][col];
            for (size_t j = col; j <= erasures.size(); ++j) {
                matrix[r][j] ^= gf64MulForTest(scale, matrix[rank][j]);
            }
        }
        pivotCol.push_back(col);
        ++rank;
    }

    for (const auto& row : matrix) {
        bool anyCoeff = false;
        for (size_t c = 0; c < erasures.size(); ++c) anyCoeff = anyCoeff || row[c] != 0;
        REQUIRE((anyCoeff || row[erasures.size()] == 0));
    }
    REQUIRE(rank == erasures.size());

    for (size_t r = 0; r < pivotCol.size(); ++r) {
        const size_t c = pivotCol[r];
        symbols[static_cast<size_t>(erasures[c])] = matrix[r][erasures.size()] & 0x3fu;
    }
    const auto check = rs63RemainderForTest(symbols);
    for (uint8_t v : check) REQUIRE((v & 0x3fu) == 0);
    return symbols;
}

uint16_t p25Phase2Crc12ForTest(const std::vector<uint8_t>& bits, size_t len)
{
    static constexpr std::array<uint8_t, 13> poly{1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1};
    std::vector<uint8_t> work(len + 12, 0);
    for (size_t i = 0; i < len && i < bits.size(); ++i) work[i] = bits[i] ? 1u : 0u;
    for (size_t pos = 0; pos < len; ++pos) {
        if (!work[pos]) continue;
        for (size_t j = 0; j < poly.size(); ++j) work[pos + j] ^= poly[j];
    }
    uint16_t rem = 0;
    for (size_t i = 0; i < 12; ++i) rem = static_cast<uint16_t>((rem << 1) | (work[len + i] & 1u));
    return static_cast<uint16_t>((rem ^ 0x0fffu) & 0x0fffu);
}

uint16_t p25Phase2Crc16ForTest(const std::vector<uint8_t>& bits, size_t len)
{
    static constexpr std::array<uint8_t, 17> poly{1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<uint8_t> work(len + 16, 0);
    for (size_t i = 0; i < len && i < bits.size(); ++i) work[i] = bits[i] ? 1u : 0u;
    for (size_t pos = 0; pos < len; ++pos) {
        if (!work[pos]) continue;
        for (size_t j = 0; j < poly.size(); ++j) work[pos + j] ^= poly[j];
    }
    uint16_t rem = 0;
    for (size_t i = 0; i < 16; ++i) rem = static_cast<uint16_t>((rem << 1) | (work[len + i] & 1u));
    return static_cast<uint16_t>((rem ^ 0xffffu) & 0xffffu);
}

std::vector<uint8_t> bytesToBitsMsbForTest(const std::vector<uint8_t>& bytes, size_t bitCount)
{
    std::vector<uint8_t> bits;
    bits.reserve(bitCount);
    for (size_t i = 0; i < bitCount; ++i) {
        const size_t byte = i / 8;
        const int bit = 7 - static_cast<int>(i % 8);
        bits.push_back(byte < bytes.size() ? static_cast<uint8_t>((bytes[byte] >> bit) & 1u) : 0u);
    }
    return bits;
}

void appendSymbolBitsForTest(std::vector<uint8_t>& bits, uint8_t symbol)
{
    for (int bit = 5; bit >= 0; --bit) bits.push_back(static_cast<uint8_t>((symbol >> bit) & 1u));
}

void writeTimeslotBitsForTest(std::vector<int>& payload, size_t startBit, size_t countBits, const std::vector<uint8_t>& bits, size_t& cursor)
{
    for (size_t i = 0; i < countBits; ++i) {
        REQUIRE(cursor < bits.size());
        const size_t bitIndex = startBit + i;
        REQUIRE(bitIndex < payload.size() * 2);
        const size_t dibitIndex = bitIndex / 2;
        auto pair = P25LiveDecoder::bitsFromDibit(payload[dibitIndex] & 0x03);
        pair[bitIndex & 0x01u] = bits[cursor++];
        payload[dibitIndex] = P25LiveDecoder::dibitFromBits(pair[0], pair[1]);
    }
}

std::vector<int> makeSyntheticPhase2SlowAcchPayloadForTest(uint8_t duid, const std::vector<uint8_t>& bits)
{
    REQUIRE(bits.size() == 180);

    std::array<uint8_t, 63> symbols{};
    for (size_t i = 0, bit = 0; i < 30 && bit + 5 < bits.size(); ++i, bit += 6) {
        symbols[57u - i] = static_cast<uint8_t>((bits[bit] << 5) |
                                                (bits[bit + 1] << 4) |
                                                (bits[bit + 2] << 3) |
                                                (bits[bit + 3] << 2) |
                                                (bits[bit + 4] << 1) |
                                                bits[bit + 5]);
    }
    std::vector<int> parityPositions;
    parityPositions.reserve(28);
    for (int pos = 0; pos <= 27; ++pos) parityPositions.push_back(pos);
    symbols = rs63SolveErasuresForTest(symbols, parityPositions);

    std::vector<uint8_t> codedBits;
    codedBits.reserve(312);
    // Match sdrtrunk SacchTimeslot/LcchTimeslot exactly:
    // INFO_1..30 are output[x=57..28], PARITY_1..22 are input[x=27..6].
    for (int sym = 57; sym >= 28; --sym) appendSymbolBitsForTest(codedBits, symbols[static_cast<size_t>(sym)]);
    for (int sym = 27; sym >= 6; --sym) appendSymbolBitsForTest(codedBits, symbols[static_cast<size_t>(sym)]);
    REQUIRE(codedBits.size() == 312);

    std::vector<int> payload(160, 0);
    const uint8_t raw = encodePhase2DuidForTest(duid);
    payload[0] = (raw >> 6) & 0x03;
    payload[37] = (raw >> 4) & 0x03;
    payload[122] = (raw >> 2) & 0x03;
    payload[159] = raw & 0x03;

    size_t cursor = 0;
    writeTimeslotBitsForTest(payload, 2, 72, codedBits, cursor);
    writeTimeslotBitsForTest(payload, 76, 108, codedBits, cursor);
    writeTimeslotBitsForTest(payload, 184, 60, codedBits, cursor);
    writeTimeslotBitsForTest(payload, 246, 72, codedBits, cursor);
    REQUIRE(cursor == codedBits.size());
    return payload;
}

std::vector<int> makeSyntheticPhase2SacchPayloadForTest()
{
    std::vector<uint8_t> dataBytes(21, 0);
    dataBytes[0] = 0x20;  // MAC opcode 1, offset 0.
    dataBytes[10] = 0x80; // algId 0x80 means clear.
    auto bits = bytesToBitsMsbForTest(dataBytes, 168);
    const uint16_t crc = p25Phase2Crc12ForTest(bits, bits.size());
    for (int bit = 11; bit >= 0; --bit) bits.push_back(static_cast<uint8_t>((crc >> bit) & 1u));
    return makeSyntheticPhase2SlowAcchPayloadForTest(0x3, bits);
}

std::vector<int> makeSyntheticPhase2MacActiveGroupUserSacchPayloadForTest(uint16_t talkgroupId,
                                                                          bool encrypted,
                                                                          uint8_t pduType = 4)
{
    std::vector<uint8_t> dataBytes(21, 0);
    dataBytes[0] = static_cast<uint8_t>(((pduType & 0x07u) << 5) | (4u << 2)); // PDU type, offset 4.
    dataBytes[1] = 0x01; // Group voice channel user.
    dataBytes[2] = encrypted ? 0x44 : 0x04; // service options; bit 6 is encrypted.
    dataBytes[3] = static_cast<uint8_t>((talkgroupId >> 8) & 0xffu);
    dataBytes[4] = static_cast<uint8_t>(talkgroupId & 0xffu);
    dataBytes[5] = 0x12;
    dataBytes[6] = 0x34;
    dataBytes[7] = 0x56;
    auto bits = bytesToBitsMsbForTest(dataBytes, 168);
    const uint16_t crc = p25Phase2Crc12ForTest(bits, bits.size());
    for (int bit = 11; bit >= 0; --bit) bits.push_back(static_cast<uint8_t>((crc >> bit) & 1u));
    return makeSyntheticPhase2SlowAcchPayloadForTest(0x3, bits);
}

std::vector<int> makeSyntheticPhase2LcchPayloadForTest()
{
    std::vector<uint8_t> dataBytes(19, 0);
    dataBytes[0] = 0x00; // MAC_SIGNAL header, no additional structures.
    auto bits = bytesToBitsMsbForTest(dataBytes, 152);
    constexpr uint16_t nac = 0x2d2;
    for (int bit = 11; bit >= 0; --bit) bits.push_back(static_cast<uint8_t>((nac >> bit) & 1u));
    REQUIRE(bits.size() == 164);
    const uint16_t crc = p25Phase2Crc16ForTest(bits, bits.size());
    for (int bit = 15; bit >= 0; --bit) bits.push_back(static_cast<uint8_t>((crc >> bit) & 1u));
    return makeSyntheticPhase2SlowAcchPayloadForTest(0xD, bits);
}

std::vector<int> makeSyntheticPhase2SuperframeWithSacchForTest()
{
    auto dibits = makeSyntheticPhase2Superframe();
    const auto payload = makeSyntheticPhase2SacchPayloadForTest();
    const size_t base = 2 * P25LiveDecoder::Phase2BurstDibits + P25LiveDecoder::Phase2FrameSyncDibits;
    for (size_t i = 0; i < payload.size(); ++i) {
        dibits[base + i] = payload[i] & 0x03;
    }
    return dibits;
}

std::vector<int> makeSyntheticPhase2SuperframeWithMacActiveGroupUserForTest(uint16_t talkgroupId,
                                                                            bool encrypted,
                                                                            uint8_t pduType = 4)
{
    auto dibits = makeSyntheticPhase2Superframe();
    const auto payload = makeSyntheticPhase2MacActiveGroupUserSacchPayloadForTest(talkgroupId, encrypted, pduType);
    const size_t base = 2 * P25LiveDecoder::Phase2BurstDibits + P25LiveDecoder::Phase2FrameSyncDibits;
    for (size_t i = 0; i < payload.size(); ++i) {
        dibits[base + i] = payload[i] & 0x03;
    }
    return dibits;
}

std::vector<int> makeSyntheticPhase2SuperframeWithLcchForTest()
{
    auto dibits = makeSyntheticPhase2Superframe();
    const auto payload = makeSyntheticPhase2LcchPayloadForTest();
    const size_t base = 2 * P25LiveDecoder::Phase2BurstDibits + P25LiveDecoder::Phase2FrameSyncDibits;
    for (size_t i = 0; i < payload.size(); ++i) {
        dibits[base + i] = payload[i] & 0x03;
    }
    return dibits;
}

std::vector<int> maskSyntheticPhase2SuperframeForTest(std::vector<int> dibits,
                                                      uint16_t nac,
                                                      uint32_t wacn,
                                                      uint16_t systemId,
                                                      uint8_t maskPhase)
{
    const auto mask = P25LiveDecoder::phase2XorMaskDibits(nac, wacn, systemId);
    constexpr std::array<size_t, 4> duidDibits{0, 37, 122, 159};
    for (size_t slot = 0; slot < 12; ++slot) {
        const size_t base = slot * P25LiveDecoder::Phase2BurstDibits;
        const size_t maskSlot = (slot + static_cast<size_t>(maskPhase)) % 12u;
        for (size_t payloadDibit = 0; payloadDibit < 160; ++payloadDibit) {
            if (std::find(duidDibits.begin(), duidDibits.end(), payloadDibit) != duidDibits.end()) continue;
            dibits[base + P25LiveDecoder::Phase2FrameSyncDibits + payloadDibit] ^=
                mask[maskSlot * P25LiveDecoder::Phase2BurstDibits + payloadDibit] & 0x03;
        }
    }
    return dibits;
}

std::vector<int> shiftSyntheticPhase2BurstForTest(std::vector<int> dibits,
                                                  size_t slot,
                                                  int shiftDibits)
{
    const size_t base = slot * P25LiveDecoder::Phase2BurstDibits;
    REQUIRE(base + P25LiveDecoder::Phase2BurstDibits <= dibits.size());

    const auto signedBase = static_cast<long long>(base);
    const auto signedEnd = signedBase + static_cast<long long>(P25LiveDecoder::Phase2BurstDibits);
    const auto signedTarget = signedBase + static_cast<long long>(shiftDibits);
    REQUIRE(signedTarget >= 0);
    REQUIRE(signedTarget + static_cast<long long>(P25LiveDecoder::Phase2BurstDibits) <= static_cast<long long>(dibits.size()));
    REQUIRE(signedTarget != signedBase);

    const std::vector<int> burst(dibits.begin() + static_cast<std::ptrdiff_t>(signedBase),
                                 dibits.begin() + static_cast<std::ptrdiff_t>(signedEnd));
    std::fill(dibits.begin() + static_cast<std::ptrdiff_t>(signedBase),
              dibits.begin() + static_cast<std::ptrdiff_t>(signedEnd),
              0);
    std::copy(burst.begin(),
              burst.end(),
              dibits.begin() + static_cast<std::ptrdiff_t>(signedTarget));
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

size_t p25DataDeinterleaveIndexForTest(size_t interleavedBitIndex)
{
    const size_t nibble = interleavedBitIndex / 4;
    const size_t bit = interleavedBitIndex % 4;
    size_t row = 0;
    size_t column = 0;
    if (nibble < 13) {
        row = nibble;
        column = 0;
    } else if (nibble < 25) {
        row = nibble - 13;
        column = 1;
    } else if (nibble < 37) {
        row = nibble - 25;
        column = 2;
    } else {
        row = nibble - 37;
        column = 3;
    }
    return ((row * 4) + column) * 4 + bit;
}

std::vector<int> interleaveP25DataCodedDibits(const std::vector<int>& deinterleaved)
{
    REQUIRE(deinterleaved.size() == 98);
    std::array<uint8_t, 196> deinterleavedBits{};
    for (size_t i = 0; i < deinterleaved.size(); ++i) {
        const auto pair = P25LiveDecoder::bitsFromDibit(deinterleaved[i] & 0x03);
        deinterleavedBits[i * 2] = pair[0];
        deinterleavedBits[i * 2 + 1] = pair[1];
    }

    std::array<uint8_t, 196> interleavedBits{};
    for (size_t i = 0; i < interleavedBits.size(); ++i) {
        interleavedBits[i] = deinterleavedBits[p25DataDeinterleaveIndexForTest(i)];
    }

    std::vector<int> out;
    out.reserve(98);
    for (size_t i = 0; i + 1 < interleavedBits.size(); i += 2) {
        out.push_back(P25LiveDecoder::dibitFromBits(interleavedBits[i], interleavedBits[i + 1]));
    }
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

std::vector<uint8_t> makeSyntheticPduFrameBits(uint16_t nac,
                                               const std::vector<uint8_t>& header,
                                               const std::vector<std::vector<uint8_t>>& blocks)
{
    std::vector<uint8_t> bits;
    appendBitsMsb(bits, 0x2aau, 8);
    const auto sync = P25LiveDecoder::frameSyncBits();
    bits.insert(bits.end(), sync.begin(), sync.end());

    std::vector<int> payload = makeNidDibits(nac, P25DataUnitId::PDU);
    auto codedHeader = trellisEncodeHalfRate(bytesToDibits(header));
    REQUIRE(codedHeader.size() == 98);
    codedHeader = interleaveP25DataCodedDibits(codedHeader);
    payload.insert(payload.end(), codedHeader.begin(), codedHeader.end());
    for (const auto& block : blocks) {
        auto coded = trellisEncodeHalfRate(bytesToDibits(block));
        REQUIRE(coded.size() == 98);
        coded = interleaveP25DataCodedDibits(coded);
        payload.insert(payload.end(), coded.begin(), coded.end());
    }

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

TEST_CASE("P25 live decoder applies SDRTrunk-style CCITT80 single-bit TSBK correction")
{
    auto good = makeTsbk(0x3d);
    writeBitsMsb(good, 16, 4, 2);
    writeBitsMsb(good, 29, 9, 0x100u);
    writeBitsMsb(good, 38, 10, 100);
    writeBitsMsb(good, 48, 32, static_cast<uint32_t>(450000000.0 / 5.0));
    finishTsbkCrc(good);

    auto corrupted = good;
    corrupted[2] ^= 0x20u; // one post-Viterbi payload bit error, corrected by CCITT80 syndrome.

    P25LiveDecoder decoder;
    const auto bits = makeSyntheticTsduFrameBits(0x293, corrupted);
    const auto result = decoder.processHardBits(bits);

    REQUIRE(result.rawTsbkBlocks.size() == 1);
    const auto& block = result.rawTsbkBlocks.front();
    std::ostringstream blockHex;
    for (uint8_t b : block.bytes) {
        blockHex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    INFO("decoded block=" << blockHex.str());
    REQUIRE(block.fecDecoded);
    REQUIRE(block.crcValid);
    REQUIRE(block.crcCorrected);
    REQUIRE(block.crcCorrectedBits == 1);
    REQUIRE(block.bytes == good);
}

TEST_CASE("P25 live decoder decodes Phase 1 PDU AMBTC grant candidates")
{
    std::vector<uint8_t> header(12, 0);
    writeBitsMsb(header, 2, 1, 1);
    writeBitsMsb(header, 3, 5, 23);
    writeBitsMsb(header, 16, 8, 0x00);
    writeBitsMsb(header, 24, 24, 0x112233);
    writeBitsMsb(header, 49, 7, 1);
    writeBitsMsb(header, 58, 6, 0x00);
    writeBitsMsb(header, 64, 8, 0x00);
    finishTsbkCrc(header);

    std::vector<uint8_t> block0(12, 0);
    writeBitsMsb(block0, 16, 4, 7);
    writeBitsMsb(block0, 20, 12, 0x039);
    writeBitsMsb(block0, 32, 4, 7);
    writeBitsMsb(block0, 36, 12, 0x039);
    writeBitsMsb(block0, 48, 16, 11178);

    P25LiveDecoder decoder;
    const auto bits = makeSyntheticPduFrameBits(0x2d2, header, {block0});
    const auto result = decoder.processHardBits(bits);

    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().duid == P25DataUnitId::PDU);
    REQUIRE(result.stats.phase1PduHeaders == 1);
    REQUIRE(result.stats.phase1PduCrcValid == 1);
    REQUIRE(result.stats.phase1AmbtcPdus == 1);
    REQUIRE(result.phase1Pdus.size() == 1);
    const auto& pdu = result.phase1Pdus.front();
    REQUIRE(pdu.headerCrcValid);
    REQUIRE(pdu.format == 23);
    REQUIRE(pdu.vendor == 0x00);
    REQUIRE(pdu.opcode == 0x00);
    REQUIRE(pdu.blocksToFollow == 1);
    REQUIRE(pdu.dataBlocks.size() == 1);
    REQUIRE(pdu.dataBlocks.front().bytes == block0);

    P25ControlChannelAnalyzer analyzer;
    P25ChannelIdentifier identifier;
    identifier.valid = true;
    identifier.id = 7;
    identifier.channelType = 3;
    identifier.baseHz = 420000000.0;
    identifier.spacingHz = 12500.0;
    identifier.bandwidthHz = 12500.0;
    identifier.slotsPerCarrier = 2;
    identifier.phase2Capable = true;
    analyzer.setChannelIdentifier(identifier);
    const auto events = analyzer.ingestPhase1Pdu(
        pdu.format, pdu.vendor, pdu.opcode, pdu.headerBytes, {pdu.dataBlocks.front().bytes}, pdu.headerCrcValid);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(events.front().talkgroupId == 11178);
    REQUIRE(events.front().tdmaSlotKnown);
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
    REQUIRE(result.stats.softDecisionSymbols > 0);
    REQUIRE(result.stats.softDecisionQuality > 0.20);
    REQUIRE(result.stats.softBitLlrMean > 0.25);
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

TEST_CASE("P25 live decoder skips CQPSK fallback after trusted C4FM control payload")
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

    P25LiveDecoderConfig cfg;
    cfg.enableC4fmFixedPhaseSearch = true;
    cfg.maxC4fmFixedPhaseCandidates = 10;
    cfg.enableCqpskSearch = true;
    cfg.enablePhase2Decode = false;
    cfg.stopC4fmSearchOnHardLock = true;
    cfg.maxCqpskSearchCandidates = 32;
    P25LiveDecoder decoder(cfg);

    const auto result = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);

    REQUIRE(result.stats.c4fmHardLockSkippedCqpsk);
    REQUIRE(result.stats.cqpskCandidatesEvaluated == 0);
    REQUIRE(result.stats.demodPath.find("C4FM") != std::string::npos);
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
    REQUIRE(result.stats.softDecisionSymbols > 0);
    REQUIRE(result.stats.softDecisionQuality > 0.10);
    REQUIRE(result.stats.softBitLlrMean > 0.10);
    REQUIRE(std::abs(result.stats.cqpskResidualCarrierHz) < 120.0);
}

TEST_CASE("P25 live decoder keeps CQPSK fallback when C4FM has no trusted control payload")
{
    constexpr double sampleRate = 2048000.0;
    constexpr double centerHz = 419112500.0;
    constexpr double carrierOffsetHz = -7250.0;
    std::vector<std::complex<float>> iq(static_cast<size_t>(sampleRate * 0.025), {});
    double phase = 0.0;
    const double step = 2.0 * kTestPi * carrierOffsetHz / sampleRate;
    for (auto& sample : iq) {
        sample = std::complex<float>(
            static_cast<float>(0.25 * std::cos(phase)),
            static_cast<float>(0.25 * std::sin(phase)));
        phase += step;
    }

    P25LiveDecoderConfig cfg;
    cfg.enableC4fmFixedPhaseSearch = true;
    cfg.maxC4fmFixedPhaseCandidates = 10;
    cfg.enableCqpskSearch = true;
    cfg.enablePhase2Decode = false;
    cfg.stopC4fmSearchOnHardLock = true;
    cfg.maxCqpskSearchCandidates = 32;
    P25LiveDecoder decoder(cfg);

    const auto result = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);

    REQUIRE_FALSE(result.stats.c4fmHardLockSkippedCqpsk);
    REQUIRE(result.stats.cqpskCandidatesEvaluated > 0);
    REQUIRE(result.rawTsbkBlocks.empty());
}

TEST_CASE("P25 live decoder removes persistent front-end DC from C4FM IQ")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x461, tsbk);

    constexpr double sampleRate = 2048000.0;
    constexpr double centerHz = 419112500.0;
    constexpr double carrierOffsetHz = 9250.0;
    auto iq = makeC4fmIq(bits, sampleRate, carrierOffsetHz, 600.0, 0.21);
    for (auto& sample : iq) sample += std::complex<float>(0.30f, -0.24f);

    P25LiveDecoder decoder;
    const auto result = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);

    REQUIRE(result.stats.frontEndDcBlockApplied);
    REQUIRE(result.stats.frontEndDcEstimateMagnitude > 0.05);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().nac == 0x461);
    REQUIRE_FALSE(result.rawTsbkBlocks.empty());
    REQUIRE(result.rawTsbkBlocks.front().crcValid);
}

TEST_CASE("P25 live decoder removes persistent front-end DC from CQPSK LSM IQ")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x78a, tsbk);

    constexpr double sampleRate = 2048000.0;
    constexpr double centerHz = 419112500.0;
    constexpr double carrierOffsetHz = -7250.0;
    auto iq = makeCqpskIq(bits, sampleRate, carrierOffsetHz, 0.42);
    for (auto& sample : iq) sample += std::complex<float>(-0.26f, 0.22f);

    P25LiveDecoder decoder;
    const auto result = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);

    REQUIRE(result.stats.frontEndDcBlockApplied);
    REQUIRE(result.stats.frontEndDcEstimateMagnitude > 0.05);
    REQUIRE(result.stats.demodPath.find("CQPSK") != std::string::npos);
    REQUIRE(result.stats.cqpskCarrierLoopApplied);
    REQUIRE(result.stats.cqpskCarrierLoopSymbols > 80);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().nac == 0x78a);
    REQUIRE_FALSE(result.rawTsbkBlocks.empty());
    REQUIRE(result.rawTsbkBlocks.front().crcValid);
}

TEST_CASE("P25 live decoder reuses sticky CQPSK demod lock after validation")
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
    const auto first = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);
    REQUIRE(first.stats.demodPath.find("CQPSK") != std::string::npos);
    REQUIRE(first.stats.cqpskLockUpdated);

    const auto second = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz);
    REQUIRE(second.stats.demodPath.find("CQPSK") != std::string::npos);
    REQUIRE(second.stats.cqpskLockActive);
    REQUIRE(second.stats.cqpskLockUsed);
    REQUIRE(second.stats.cqpskLockTrustScore > 0);
    REQUIRE(second.stats.cqpskLockMisses == 0);
    REQUIRE(second.nids.front().fecValidated);
    REQUIRE_FALSE(second.rawTsbkBlocks.empty());
    REQUIRE(second.rawTsbkBlocks.front().crcValid);
}

TEST_CASE("P25 live decoder corrects residual CQPSK carrier offset")
{
    auto tsbk = makeTsbk(0x02);
    writeBitsMsb(tsbk, 16, 16, 0x1001);
    writeBitsMsb(tsbk, 32, 16, 2001);
    finishTsbkCrc(tsbk);
    const auto bits = makeSyntheticTsduFrameBits(0x55a, tsbk);

    constexpr double sampleRate = 2048000.0;
    constexpr double centerHz = 419112500.0;
    constexpr double carrierOffsetHz = -7250.0;
    constexpr double tuneErrorHz = 325.0;
    const auto iq = makeCqpskIq(bits, sampleRate, carrierOffsetHz, 0.42);

    P25LiveDecoder decoder;
    const auto result = decoder.processIq(iq, sampleRate, centerHz, centerHz + carrierOffsetHz + tuneErrorHz);

    REQUIRE(result.stats.demodPath.find("CQPSK") != std::string::npos);
    const double totalCorrectionHz = std::abs(result.stats.cqpskResidualCarrierHz) +
        std::abs(result.stats.cqpskCarrierLoopCorrectionHz);
    INFO("fine residual Hz=" << result.stats.cqpskResidualCarrierHz
         << " carrier loop Hz=" << result.stats.cqpskCarrierLoopCorrectionHz);
    REQUIRE((result.stats.cqpskFineCorrectionApplied || result.stats.cqpskCarrierLoopApplied));
    REQUIRE(totalCorrectionHz > 150.0);
    REQUIRE(totalCorrectionHz < 900.0);
    REQUIRE(result.stats.cqpskPhaseErrorRmsRad < 0.45);
    REQUIRE(result.stats.cqpskFineCorrectionSymbols > 80);
    REQUIRE_FALSE(result.nids.empty());
    REQUIRE(result.nids.front().fecValidated);
    REQUIRE(result.nids.front().nac == 0x55a);
    REQUIRE_FALSE(result.rawTsbkBlocks.empty());
    REQUIRE(result.rawTsbkBlocks.front().crcValid);
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

    P25LiveDecoder statsDecoder;
    const auto result = statsDecoder.processHardDibits(dibits);
    REQUIRE(result.stats.phase2Bursts == 1);
    REQUIRE(result.stats.phase2VoiceCodewords == 4);
    REQUIRE(result.stats.phase2SuperframeBursts == 0);
    REQUIRE(result.stats.bestPhase2SyncErrors == 0);
}

TEST_CASE("P25 Phase 2 hard framer rejects uncorrected rotated sync patterns", "[p25]")
{
    // SDRTrunk uses rotated Phase-2 sync words as constellation/PLL correction
    // evidence, not as valid message-frame starts.  The demod candidate layer
    // must correct the dibit mapping first; otherwise the timeslot payload is
    // still rotated and MAC/ESS/AMBE extraction becomes plausible-looking noise.
    constexpr uint64_t sdrtrunkPhase2Error180 = 0xA8A2A80800ull;
    auto dibits = makeSyntheticPhase2Burst(0x0);
    const auto rotated = phase2DibitsFromWordForTest(sdrtrunkPhase2Error180);
    std::copy(rotated.begin(), rotated.end(), dibits.begin());

    P25LiveDecoder decoder;
    const auto bursts = decoder.processPhase2HardDibits(dibits);
    REQUIRE(bursts.empty());

    const auto result = decoder.processHardDibits(dibits);
    REQUIRE(result.stats.phase2Bursts == 0);
    REQUIRE(result.stats.phase2VoiceCodewords == 0);
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

TEST_CASE("P25 live decoder does not extract AMBE voice from Phase 2 ACCH bursts")
{
    const std::array<std::pair<int, P25Phase2BurstKind>, 5> cases{{
        {0x3, P25Phase2BurstKind::SacchScrambled},
        {0x9, P25Phase2BurstKind::FacchScrambled},
        {0xC, P25Phase2BurstKind::SacchClear},
        {0xD, P25Phase2BurstKind::LcchClear},
        {0xF, P25Phase2BurstKind::FacchClear},
    }};

    for (const auto& [duid, expectedKind] : cases) {
        INFO("DUID " << duid);
        const auto dibits = makeSyntheticPhase2Burst(duid);

        P25LiveDecoder burstDecoder;
        const auto bursts = burstDecoder.processPhase2HardDibits(dibits);
        REQUIRE(bursts.size() == 1);
        REQUIRE(bursts.front().kind == expectedKind);
        REQUIRE(bursts.front().voiceCodewords.empty());

        P25LiveDecoder statsDecoder;
        const auto result = statsDecoder.processHardDibits(dibits);
        REQUIRE(result.stats.phase2Bursts == 1);
        REQUIRE(result.stats.phase2VoiceCodewords == 0);
    }
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
        REQUIRE(burst.superframeSyncScore >= 4);
        REQUIRE_FALSE(burst.phase2AudioLock);
        REQUIRE_FALSE(burst.sessionAudioRelease);
        REQUIRE(burst.superframeBurstIndexKnown);
        REQUIRE(burst.superframeBurstIndex == slot);
        REQUIRE(burst.grantSlotKnown);
        const uint8_t expectedGrantSlot = (slot == 10) ? 1 : (slot == 11) ? 0 : static_cast<uint8_t>(slot & 0x01u);
        REQUIRE(burst.grantSlot == expectedGrantSlot);
        REQUIRE(burst.duid == 0x0);
        REQUIRE(burst.kind == P25Phase2BurstKind::Voice4);
        REQUIRE(burst.voiceCodewords.size() == 4);
    }
    REQUIRE(bursts[0].syncErrors == -1);
    REQUIRE(bursts[2].syncErrors == 0);

    P25LiveDecoder statsDecoder;
    const auto result = statsDecoder.processHardDibits(dibits);
    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2SuperframeBursts == 12);
    REQUIRE(result.stats.phase2VoiceCodewords == 48);
    REQUIRE(result.stats.phase2MaskedBursts == 0);
    REQUIRE(result.stats.phase2IschSync == 6);
}

TEST_CASE("P25 live decoder preserves final-fragment slot order when I-ISCH is unavailable", "[p25]")
{
    P25LiveDecoder decoder;
    auto dibits = makeSyntheticPhase2Superframe();
    const auto sync = P25LiveDecoder::phase2FrameSyncDibits();

    // Force the final fragment's A/B words to S-ISCH-style sync so the decoder
    // cannot derive a reliable I-ISCH location. The fallback must stay on the
    // standards 0..11 superframe order, where final-fragment C/D are swapped.
    for (size_t slot : {size_t{8}, size_t{9}}) {
        const size_t base = slot * P25LiveDecoder::Phase2BurstDibits;
        std::copy(sync.begin(), sync.end(), dibits.begin() + static_cast<std::ptrdiff_t>(base));
    }

    const auto bursts = decoder.processPhase2HardDibits(dibits);
    REQUIRE(bursts.size() == 12);
    REQUIRE(bursts[10].grantSlotKnown);
    REQUIRE(bursts[11].grantSlotKnown);
    REQUIRE(bursts[10].grantSlot == 1);
    REQUIRE(bursts[11].grantSlot == 0);
}

TEST_CASE("P25 live decoder emits stable Phase 2 session codeword IDs")
{
    P25LiveDecoder decoder;
    const auto dibits = makeSyntheticPhase2Superframe();

    const auto first = decoder.processHardDibits(dibits);
    REQUIRE(first.phase2Bursts.size() == 12);
    REQUIRE(first.stats.phase2VoiceCodewords == 48);
    std::vector<uint64_t> ids;
    ids.reserve(first.stats.phase2VoiceCodewords);
    for (const auto& burst : first.phase2Bursts) {
        for (const auto& codeword : burst.voiceCodewords) {
            REQUIRE(codeword.sessionCodewordIdKnown);
            REQUIRE(codeword.streamDibitKnown);
            REQUIRE_FALSE(codeword.duplicateInSession);
            ids.push_back(codeword.sessionCodewordId);
        }
    }
    REQUIRE(ids.size() == 48);

    std::vector<int> nextWindow(P25LiveDecoder::Phase2BurstDibits, 0);
    nextWindow.insert(nextWindow.end(), dibits.begin(), dibits.end());
    const auto next = decoder.processHardDibits(nextWindow);
    REQUIRE(next.phase2Bursts.size() == 12);
    std::vector<uint64_t> nextIds;
    nextIds.reserve(next.stats.phase2VoiceCodewords);
    for (const auto& burst : next.phase2Bursts) {
        for (const auto& codeword : burst.voiceCodewords) {
            REQUIRE(codeword.sessionCodewordIdKnown);
            REQUIRE_FALSE(codeword.duplicateInSession);
            nextIds.push_back(codeword.sessionCodewordId);
        }
    }
    REQUIRE(nextIds.size() == ids.size());

    const auto repeated = decoder.processHardDibits(nextWindow);
    REQUIRE(repeated.phase2Bursts.empty());
    REQUIRE(repeated.stats.phase2VoiceCodewords == 0);
}

TEST_CASE("P25 Phase 2 XOR mask generation matches known local-system vector")
{
    const auto mask = P25LiveDecoder::phase2XorMaskDibits(0x2d2, 0xbee00, 0x2d1);
    REQUIRE(mask.size() == P25LiveDecoder::Phase2BurstDibits * 12);
    const std::array<int, 40> expected{
        0, 2, 3, 1, 0, 1, 0, 2, 3, 1,
        0, 2, 2, 0, 2, 3, 2, 1, 3, 3,
        1, 0, 0, 2, 1, 2, 1, 1, 2, 1,
        1, 0, 2, 2, 0, 2, 1, 2, 3, 2,
    };
    for (size_t i = 0; i < expected.size(); ++i) REQUIRE(mask[i] == expected[i]);
}

TEST_CASE("P25 live decoder applies Phase 2 XOR mask before extracting voice codewords")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    const auto clear = makeSyntheticPhase2SuperframeWithSacchForTest();
    const auto masked = maskSyntheticPhase2SuperframeForTest(clear, nac, wacn, systemId, 0);

    P25LiveDecoder clearDecoder;
    const auto clearBursts = clearDecoder.processPhase2HardDibits(clear);
    REQUIRE(clearBursts.size() == 12);

    P25LiveDecoder maskedDecoder;
    maskedDecoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto maskedResult = maskedDecoder.processHardDibits(masked);
    REQUIRE(maskedResult.phase2Bursts.size() == 12);
    REQUIRE(maskedResult.stats.phase2MaskedBursts == 12);
    REQUIRE(maskedResult.stats.phase2VoiceCodewords == 44);
    REQUIRE(maskedResult.stats.phase2EssKnown);
    REQUIRE_FALSE(maskedResult.stats.phase2EssEncrypted);
    for (size_t slot = 0; slot < maskedResult.phase2Bursts.size(); ++slot) {
        const auto& burst = maskedResult.phase2Bursts[slot];
        REQUIRE(burst.xorMaskApplied);
        REQUIRE(burst.voiceCodewords.size() == clearBursts[slot].voiceCodewords.size());
        if (burst.voiceCodewords.empty()) continue;
        REQUIRE(burst.voiceCodewords.front().bits == clearBursts[slot].voiceCodewords.front().bits);
    }
}

TEST_CASE("P25 live decoder searches Phase 2 XOR mask phase using MAC CRC evidence")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    constexpr uint8_t maskPhase = 5;

    const auto clear = makeSyntheticPhase2SuperframeWithSacchForTest();
    const auto masked = maskSyntheticPhase2SuperframeForTest(clear, nac, wacn, systemId, maskPhase);

    P25LiveDecoder decoder;
    decoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto result = decoder.processHardDibits(masked);

    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2MaskPhaseKnown);
    REQUIRE(result.stats.phase2MaskPhase == maskPhase);
    REQUIRE(result.stats.phase2MaskPhaseMacCrcValid >= 1);
    REQUIRE(result.stats.phase2MacCrcValid >= 1);
    REQUIRE(result.stats.phase2EssKnown);
    REQUIRE_FALSE(result.stats.phase2EssEncrypted);

    const auto burst = std::find_if(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& b) {
        return b.kind == P25Phase2BurstKind::SacchScrambled;
    });
    REQUIRE(burst != result.phase2Bursts.end());
    REQUIRE(burst->xorMaskApplied);
    REQUIRE(burst->xorMaskPhaseKnown);
    REQUIRE(burst->xorMaskPhase == maskPhase);
    REQUIRE(burst->macCrcValid);
    REQUIRE(burst->essKnown);
    REQUIRE_FALSE(burst->encrypted);

    REQUIRE(burst->grantSlotKnown);
    const uint8_t pttGrantSlot = burst->grantSlot;
    // XOR mask phase selects the scrambling segment, not the traffic timeslot.
    // SDRTrunk keeps A/B/C/D ownership bound to the physical fragment position,
    // so the grant slot must not rotate just because maskPhase is non-zero.
    REQUIRE(pttGrantSlot == 0);
    size_t releasedVoiceBursts = 0;
    bool releasedWithoutLocalMac = false;
    for (const auto& voiceBurst : result.phase2Bursts) {
        if (voiceBurst.voiceCodewords.empty()) continue;
        REQUIRE(voiceBurst.grantSlotKnown);
        if (voiceBurst.grantSlot != pttGrantSlot) {
            REQUIRE_FALSE(voiceBurst.sessionAudioRelease);
            continue;
        }
        if (voiceBurst.sessionAudioRelease) {
            REQUIRE((voiceBurst.superframeLock || voiceBurst.macCrcLock));
            REQUIRE((voiceBurst.maskPhaseLock || voiceBurst.macCrcLock));
            REQUIRE(voiceBurst.essKnown);
            REQUIRE_FALSE(voiceBurst.encrypted);
            ++releasedVoiceBursts;
            releasedWithoutLocalMac = releasedWithoutLocalMac || !voiceBurst.macCrcValid;
        }
    }
    REQUIRE(releasedVoiceBursts > 0);
    REQUIRE(releasedWithoutLocalMac);
}

TEST_CASE("P25 live decoder tracks clear MAC_ACTIVE group user without releasing Phase 2 audio")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    constexpr uint8_t maskPhase = 5;
    constexpr uint16_t talkgroupId = 30302;

    const auto clear = makeSyntheticPhase2SuperframeWithMacActiveGroupUserForTest(talkgroupId, false);
    const auto masked = maskSyntheticPhase2SuperframeForTest(clear, nac, wacn, systemId, maskPhase);

    P25LiveDecoder decoder;
    decoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto result = decoder.processHardDibits(masked);

    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2MaskPhaseKnown);
    REQUIRE(result.stats.phase2MaskPhase == maskPhase);
    REQUIRE(result.stats.phase2MacCrcValid >= 1);

    const auto macBurst = std::find_if(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& b) {
        return b.macCrcValid && b.macActiveSeen && b.trafficSecurityKnown;
    });
    REQUIRE(macBurst != result.phase2Bursts.end());
    REQUIRE(macBurst->trafficTalkgroupKnown);
    REQUIRE(macBurst->trafficTalkgroupId == talkgroupId);
    REQUIRE_FALSE(macBurst->trafficEncrypted);
    REQUIRE_FALSE(macBurst->encrypted);
    REQUIRE_FALSE(macBurst->sessionAudioRelease);
    REQUIRE(macBurst->grantSlotKnown);

    size_t trackedVoiceBursts = 0;
    for (const auto& voiceBurst : result.phase2Bursts) {
        if (voiceBurst.voiceCodewords.empty()) continue;
        if (voiceBurst.dibitOffset <= macBurst->dibitOffset) continue;
        if (!voiceBurst.grantSlotKnown || voiceBurst.grantSlot != macBurst->grantSlot) continue;
        REQUIRE(voiceBurst.trafficSecurityKnown);
        REQUIRE(voiceBurst.trafficTalkgroupKnown);
        REQUIRE(voiceBurst.trafficTalkgroupId == talkgroupId);
        REQUIRE_FALSE(voiceBurst.trafficEncrypted);
        REQUIRE_FALSE(voiceBurst.encrypted);
        REQUIRE_FALSE(voiceBurst.sessionAudioRelease);
        ++trackedVoiceBursts;
    }
    REQUIRE(trackedVoiceBursts > 0);
}

TEST_CASE("P25 live decoder keeps Phase 2 MAC_ACTIVE encrypted group user muted")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    constexpr uint8_t maskPhase = 5;
    constexpr uint16_t talkgroupId = 30302;

    const auto clear = makeSyntheticPhase2SuperframeWithMacActiveGroupUserForTest(talkgroupId, true);
    const auto masked = maskSyntheticPhase2SuperframeForTest(clear, nac, wacn, systemId, maskPhase);

    P25LiveDecoder decoder;
    decoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto result = decoder.processHardDibits(masked);

    const auto macBurst = std::find_if(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& b) {
        return b.macCrcValid && b.macActiveSeen && b.trafficSecurityKnown;
    });
    REQUIRE(macBurst != result.phase2Bursts.end());
    REQUIRE(macBurst->trafficTalkgroupKnown);
    REQUIRE(macBurst->trafficTalkgroupId == talkgroupId);
    REQUIRE(macBurst->trafficEncrypted);
    REQUIRE(macBurst->encrypted);
    REQUIRE_FALSE(macBurst->sessionAudioRelease);

    for (const auto& voiceBurst : result.phase2Bursts) {
        if (voiceBurst.voiceCodewords.empty()) continue;
        if (voiceBurst.dibitOffset <= macBurst->dibitOffset) continue;
        if (!voiceBurst.grantSlotKnown || voiceBurst.grantSlot != macBurst->grantSlot) continue;
        if (voiceBurst.trafficSecurityKnown) {
            REQUIRE(voiceBurst.trafficEncrypted);
            REQUIRE(voiceBurst.encrypted);
        }
        REQUIRE_FALSE(voiceBurst.sessionAudioRelease);
    }
}

TEST_CASE("P25 live decoder treats Phase 2 MAC_HANGTIME encrypted group user as target encrypted")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    constexpr uint8_t maskPhase = 5;
    constexpr uint16_t talkgroupId = 12068;

    const auto clear = makeSyntheticPhase2SuperframeWithMacActiveGroupUserForTest(talkgroupId, true, 6);
    const auto masked = maskSyntheticPhase2SuperframeForTest(clear, nac, wacn, systemId, maskPhase);

    P25LiveDecoder decoder;
    decoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto result = decoder.processHardDibits(masked);

    const auto macBurst = std::find_if(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& b) {
        return b.macCrcValid && b.macHangtimeSeen && b.trafficSecurityKnown;
    });
    REQUIRE(macBurst != result.phase2Bursts.end());
    REQUIRE(macBurst->trafficTalkgroupKnown);
    REQUIRE(macBurst->trafficTalkgroupId == talkgroupId);
    REQUIRE(macBurst->trafficEncrypted);
    REQUIRE(macBurst->encrypted);
    REQUIRE_FALSE(macBurst->sessionAudioRelease);

    for (const auto& voiceBurst : result.phase2Bursts) {
        if (voiceBurst.voiceCodewords.empty()) continue;
        if (voiceBurst.dibitOffset <= macBurst->dibitOffset) continue;
        if (!voiceBurst.grantSlotKnown || voiceBurst.grantSlot != macBurst->grantSlot) continue;
        if (voiceBurst.trafficSecurityKnown) {
            REQUIRE(voiceBurst.trafficTalkgroupKnown);
            REQUIRE(voiceBurst.trafficTalkgroupId == talkgroupId);
            REQUIRE(voiceBurst.trafficEncrypted);
            REQUIRE(voiceBurst.encrypted);
        }
        REQUIRE_FALSE(voiceBurst.sessionAudioRelease);
    }
}

TEST_CASE("P25 live decoder keeps sticky Phase 2 superframe lock through drifted single-burst windows")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    constexpr uint8_t maskPhase = 5;
    constexpr size_t driftDibits = 8;

    const auto clear = makeSyntheticPhase2SuperframeWithSacchForTest();
    const auto masked = maskSyntheticPhase2SuperframeForTest(clear, nac, wacn, systemId, maskPhase);

    P25LiveDecoder decoder;
    decoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto locked = decoder.processHardDibits(masked);
    REQUIRE(locked.stats.phase2MaskPhaseKnown);
    REQUIRE(locked.stats.phase2MaskPhase == maskPhase);
    REQUIRE(locked.stats.phase2MaskedBursts == 12);

    std::vector<int> nextWindow(driftDibits, 0);
    nextWindow.insert(nextWindow.end(),
                      masked.begin(),
                      masked.begin() + static_cast<std::ptrdiff_t>(P25LiveDecoder::Phase2BurstDibits * 3));

    const auto sticky = decoder.processHardDibits(nextWindow);

    REQUIRE(sticky.stats.phase2Bursts >= 1);
    REQUIRE(sticky.stats.phase2SuperframeBursts >= 1);
    REQUIRE(sticky.stats.phase2MaskedBursts >= 1);
    REQUIRE(sticky.stats.phase2SyncOffsetCorrections >= 1);

    const auto burst = std::find_if(sticky.phase2Bursts.begin(), sticky.phase2Bursts.end(), [](const P25Phase2Burst& b) {
        return b.stickySuperframe && b.superframeBurstIndexKnown && b.superframeBurstIndex == 2;
    });
    REQUIRE(burst != sticky.phase2Bursts.end());
    REQUIRE(burst->kind == P25Phase2BurstKind::SacchScrambled);
    REQUIRE(burst->voiceCodewords.empty());
    REQUIRE(burst->xorMaskApplied);
    REQUIRE(burst->xorMaskPhaseKnown);
    REQUIRE(burst->xorMaskPhase == maskPhase);
    REQUIRE(burst->grantSlotKnown);
    REQUIRE(burst->syncOffsetAdjusted);
    REQUIRE(std::abs(burst->syncOffsetDibits) == static_cast<int>(driftDibits));
}

TEST_CASE("P25 live decoder aligns masked Phase 2 MAC decode to near sync hits")
{
    constexpr uint16_t nac = 0x2d2;
    constexpr uint32_t wacn = 0xbee00;
    constexpr uint16_t systemId = 0x2d1;
    constexpr uint8_t maskPhase = 5;

    const auto clear = makeSyntheticPhase2SuperframeWithSacchForTest();
    auto masked = maskSyntheticPhase2SuperframeForTest(clear, nac, wacn, systemId, maskPhase);
    masked = shiftSyntheticPhase2BurstForTest(std::move(masked), 2, -1);

    P25LiveDecoder decoder;
    decoder.setPhase2MaskParameters(nac, wacn, systemId);
    const auto result = decoder.processHardDibits(masked);

    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2SyncOffsetCorrections >= 1);
    REQUIRE(result.stats.phase2SyncOffsetCorrectionDibits < 0);
    REQUIRE(result.stats.phase2MaskPhaseKnown);
    REQUIRE(result.stats.phase2MaskPhase == maskPhase);
    REQUIRE(result.stats.phase2MaskPhaseMacCrcValid >= 1);
    REQUIRE(result.stats.phase2MacCrcValid >= 1);
    REQUIRE(result.stats.phase2EssKnown);
    REQUIRE_FALSE(result.stats.phase2EssEncrypted);

    const auto burst = std::find_if(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& b) {
        return b.kind == P25Phase2BurstKind::SacchScrambled && b.macCrcValid;
    });
    REQUIRE(burst != result.phase2Bursts.end());
    REQUIRE(burst->xorMaskApplied);
    REQUIRE(burst->xorMaskPhaseKnown);
    REQUIRE(burst->xorMaskPhase == maskPhase);
    REQUIRE(burst->syncOffsetAdjusted);
    REQUIRE(burst->syncOffsetDibits == -1);
    REQUIRE(burst->essKnown);
    REQUIRE_FALSE(burst->encrypted);
}

TEST_CASE("P25 Phase 2 timeslot payload remains contiguous without status-symbol strip", "[p25]")
{
    // Phase 1 FDMA interleaves status symbols; Phase 2 TDMA timeslots are a
    // contiguous 160-dibit payload after the 20-dibit ISCH (SDRTrunk
    // SuperFrameFragment).  Keep this as a lightweight source/API regression;
    // full MAC CRC on multi-burst windows is covered by the XOR mask-phase tests.
    const auto clear = makeSyntheticPhase2SuperframeWithSacchForTest();
    REQUIRE(clear.size() == P25LiveDecoder::Phase2BurstDibits * 12);

    P25LiveDecoder decoder;
    const auto bursts = decoder.processPhase2HardDibits(clear);
    REQUIRE(bursts.size() == 12);

    const auto burst = std::find_if(bursts.begin(), bursts.end(), [](const P25Phase2Burst& b) {
        return b.kind == P25Phase2BurstKind::SacchClear || b.kind == P25Phase2BurstKind::SacchScrambled;
    });
    REQUIRE(burst != bursts.end());
    REQUIRE(burst->rawPayloadDibits.size() == 160);
    REQUIRE(burst->macCrcValid);
}

TEST_CASE("P25 live decoder decodes clear Phase 2 LCCH without XOR mask")
{
    const auto dibits = makeSyntheticPhase2SuperframeWithLcchForTest();

    P25LiveDecoder decoder;
    const auto result = decoder.processHardDibits(dibits);

    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2MaskedBursts == 0);
    REQUIRE(result.stats.phase2MacCrcValid >= 1);
    REQUIRE(result.stats.phase2MacNominalCrcValid >= 1);
    REQUIRE(result.stats.phase2MacAltKindCrcValid == 0);
    REQUIRE(result.stats.phase2MacBitSwapCrcValid == 0);
    REQUIRE(result.stats.phase2MacSlipCrcValid == 0);
    REQUIRE(result.stats.phase2MacInvertCrcValid == 0);

    const auto pdu = std::find_if(result.phase2MacPdus.begin(), result.phase2MacPdus.end(), [](const P25Phase2MacPdu& p) {
        return p.source == P25Phase2BurstKind::LcchClear && p.crcValid;
    });
    REQUIRE(pdu != result.phase2MacPdus.end());
    REQUIRE(pdu->opcode == 0);
    REQUIRE(pdu->bytes.size() == 19);
    REQUIRE(pdu->acchHypothesisKnown);
    REQUIRE_FALSE(pdu->acchBitOrderSwapped);
    REQUIRE_FALSE(pdu->acchDibitInverted);
    REQUIRE(pdu->acchSlipDibits == 0);
    REQUIRE(pdu->detectedKind == pdu->source);
}

TEST_CASE("P25 live decoder repairs SDRTrunk-layout Phase 2 ACCH RS symbol errors", "[p25]")
{
    auto dibits = makeSyntheticPhase2SuperframeWithLcchForTest();
    const size_t payloadBase =
        2 * P25LiveDecoder::Phase2BurstDibits + P25LiveDecoder::Phase2FrameSyncDibits;
    REQUIRE(payloadBase + 1 < dibits.size());
    dibits[payloadBase + 1] ^= 0x02; // One INFO_1 symbol bit error; DUID bits stay intact.

    P25LiveDecoder decoder;
    const auto result = decoder.processHardDibits(dibits);

    REQUIRE(result.stats.phase2Bursts == 12);
    REQUIRE(result.stats.phase2MacCrcValid >= 1);
    REQUIRE(result.stats.phase2MacRsDecoded >= 1);

    const auto pdu = std::find_if(result.phase2MacPdus.begin(), result.phase2MacPdus.end(), [](const P25Phase2MacPdu& p) {
        return p.source == P25Phase2BurstKind::LcchClear && p.crcValid;
    });
    REQUIRE(pdu != result.phase2MacPdus.end());
    REQUIRE(pdu->rsDecoded);
    REQUIRE_FALSE(pdu->directCrcOk);
    REQUIRE(pdu->opcode == 0);
    REQUIRE(pdu->bytes.size() == 19);
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
    // Mirrors OP25 p25p2_vf::extract_vcw/interleave_vcw exactly.  Each one-hot
    // 72-bit TDMA voice frame bit must land in the mbelib C0/C1/C2/C3 matrix
    // cell that OP25 assigns to c0/c1/c2/c3.
    static constexpr std::array<size_t, 72> expected = {
        0 * 24 + 23, 0 * 24 + 5, 1 * 24 + 10, 2 * 24 + 3,
        0 * 24 + 22, 0 * 24 + 4, 1 * 24 + 9, 2 * 24 + 2,
        0 * 24 + 21, 0 * 24 + 3, 1 * 24 + 8, 2 * 24 + 1,
        0 * 24 + 20, 0 * 24 + 2, 1 * 24 + 7, 2 * 24 + 0,
        0 * 24 + 19, 0 * 24 + 1, 1 * 24 + 6, 3 * 24 + 13,
        0 * 24 + 18, 0 * 24 + 0, 1 * 24 + 5, 3 * 24 + 12,
        0 * 24 + 17, 1 * 24 + 22, 1 * 24 + 4, 3 * 24 + 11,
        0 * 24 + 16, 1 * 24 + 21, 1 * 24 + 3, 3 * 24 + 10,
        0 * 24 + 15, 1 * 24 + 20, 1 * 24 + 2, 3 * 24 + 9,
        0 * 24 + 14, 1 * 24 + 19, 1 * 24 + 1, 3 * 24 + 8,
        0 * 24 + 13, 1 * 24 + 18, 1 * 24 + 0, 3 * 24 + 7,
        0 * 24 + 12, 1 * 24 + 17, 2 * 24 + 10, 3 * 24 + 6,
        0 * 24 + 11, 1 * 24 + 16, 2 * 24 + 9, 3 * 24 + 5,
        0 * 24 + 10, 1 * 24 + 15, 2 * 24 + 8, 3 * 24 + 4,
        0 * 24 + 9, 1 * 24 + 14, 2 * 24 + 7, 3 * 24 + 3,
        0 * 24 + 8, 1 * 24 + 13, 2 * 24 + 6, 3 * 24 + 2,
        0 * 24 + 7, 1 * 24 + 12, 2 * 24 + 5, 3 * 24 + 1,
        0 * 24 + 6, 1 * 24 + 11, 2 * 24 + 4, 3 * 24 + 0,
    };

    for (size_t bit = 0; bit < expected.size(); ++bit) {
        P25Phase2VoiceCodeword codeword;
        codeword.bits[bit] = 1;
        const auto ambe = p25Phase2VoiceCodewordToAmbe3600x2450Frame(codeword);

        INFO("TDMA AMBE bit " << bit);
        REQUIRE(ambe[expected[bit]] == 1);
        REQUIRE(std::count(ambe.begin(), ambe.end(), static_cast<uint8_t>(1)) == 1);
    }
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

TEST_CASE("P25 live decoder aligns absolute dibit cursor on traffic chunks")
{
    auto chunk = [](int dibit) {
        return std::vector<int>(180, dibit);
    };

    P25LiveDecoder decoder;
    decoder.alignPhase2AbsoluteDibitCursor(1000, 180);
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 1000);
    decoder.processHardDibits(chunk(0));
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 1180);

    decoder.alignPhase2AbsoluteDibitCursor(1180, 180);
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 1180);
    decoder.processHardDibits(chunk(1));
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 1360);

    // Large gap should drop sticky tail without crashing.
    decoder.alignPhase2AbsoluteDibitCursor(5000, 180);
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 5000);
    decoder.processHardDibits(chunk(2));
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 5180);

    // Rewind should force stream discontinuity without crashing.
    decoder.alignPhase2AbsoluteDibitCursor(500, 180);
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 500);
    decoder.processHardDibits(chunk(3));
    REQUIRE(decoder.phase2StreamDibitCursorForDiagnostics() == 680);
}

TEST_CASE("P25 live decoder independent probe copy starts fresh without parent session state")
{
    P25LiveDecoder parent;
    parent.setPhase2MaskParameters(0x293, 0xBEE00, 0x12C);
    parent.processHardDibits(std::vector<int>(256, 0));

    P25LiveDecoder probe = parent.createIndependentProbeCopy(true);
    REQUIRE(probe.phase2MaskParametersKnown());
    REQUIRE(probe.phase2MaskParametersMatch(0x293, 0xBEE00, 0x12C));

    P25LiveDecoder bare = parent.createIndependentProbeCopy(false);
    REQUIRE_FALSE(bare.phase2MaskParametersKnown());
}

TEST_CASE("P25 Phase 2 AMBE variant transforms produce distinct frame payloads", "[p25][ambe]")
{
    P25Phase2VoiceCodeword cw{};
    for (size_t i = 0; i < 72; ++i) {
        cw.bits[i] = static_cast<uint8_t>((i % 3) != 0);
    }
    const auto canonical = p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant(cw, 0);
    const auto swapped = p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant(cw, 1);
    REQUIRE(canonical != swapped);
    REQUIRE(p25Phase2AmbeFrameVariantCount() == 4);
}

TEST_CASE("P25 Phase 2 CQPSK traffic config matches SDRTrunk HDQPSK (6000 baud, LPF-only)", "[p25][cqpsk]")
{
    P25LiveDecoderConfig cfg;
    cfg.symbolRate = 6000.0;
    cfg.phase2CqpskTrafficDemod = true;
    cfg.cqpskUseMatchedRrcFilter = false;
    cfg.cqpskRrcAlpha = 0.20;
    cfg.cqpskCarrierLoopBandwidth = (2.0 * 3.14159265358979323846) / 300.0;
    cfg.cqpskCarrierLoopMaxCorrectionHz = 3000.0;
    REQUIRE(cfg.symbolRate == Catch::Approx(6000.0));
    REQUIRE(cfg.phase2CqpskTrafficDemod);
    REQUIRE_FALSE(cfg.cqpskUseMatchedRrcFilter);
    REQUIRE(cfg.cqpskCarrierLoopBandwidth == Catch::Approx((2.0 * 3.14159265358979323846) / 300.0));
    REQUIRE(cfg.cqpskCarrierLoopMaxCorrectionHz == Catch::Approx(3000.0));
}

TEST_CASE("P25 Phase 2 6000 baud CQPSK processIq stays inside realtime budget on noise", "[p25][cqpsk][budget]")
{
    P25LiveDecoderConfig cfg;
    cfg.symbolRate = 6000.0;
    cfg.workSampleRate = 48000.0;
    cfg.channelBandwidthHz = 12500.0;
    cfg.phase2CqpskTrafficDemod = true;
    cfg.enablePhase2Decode = true;
    cfg.enablePhase1Decode = false;
    cfg.enableCqpskSearch = true;
    cfg.cqpskUseMatchedRrcFilter = false;
    cfg.cqpskCarrierLoopBandwidth = (2.0 * 3.14159265358979323846) / 300.0;
    cfg.cqpskCarrierLoopMaxCorrectionHz = 3000.0;
    cfg.realtimeVoiceSearch = true;
    cfg.realtimeDecodeBudgetMs = 110;
    cfg.maxCqpskSearchCandidates = 36;
    cfg.maxPhase2SyncHits = 96;
    cfg.maxPhase2SuperframeLocks = 4;
    cfg.stopCqpskSearchOnHardLock = true;
    P25LiveDecoder decoder(cfg);
    decoder.setPhase2MaskParameters(0x2dc, 0xbee00, 0x2d1);

    constexpr double sampleRate = 2048000.0;
    constexpr double windowSec = 0.360;
    const size_t n = static_cast<size_t>(sampleRate * windowSec);
    std::vector<std::complex<float>> iq(n);
    uint32_t rng = 0xC0FFEEu;
    for (size_t i = 0; i < n; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float iPart = static_cast<float>((rng & 0xffff) / 32768.0f - 1.0f) * 0.05f;
        rng = rng * 1664525u + 1013904223u;
        const float qPart = static_cast<float>((rng & 0xffff) / 32768.0f - 1.0f) * 0.05f;
        iq[i] = {iPart, qPart};
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto result = decoder.processIq(iq, sampleRate, 421.975e6, 421.975e6);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    REQUIRE(result.stats.symbolRate == Catch::Approx(6000.0));
    // Hard ceiling: realtime budget is 110ms but channelize+search overhead may
    // exceed it slightly.  Minutes-long hangs must never return.
    REQUIRE(ms < 2500);
    (void)result;
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
