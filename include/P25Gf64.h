#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace p25fec {

// GF(2^6), polynomial x^6 + x + 1, matching the P25 RS decoder.
inline const std::array<std::array<uint8_t, 64>, 64>& gf64Products()
{
    static const auto table = [] {
        std::array<std::array<uint8_t, 64>, 64> values{};
        for (size_t a = 0; a < 64; ++a) {
            for (size_t b = 0; b < 64; ++b) {
                uint16_t product = 0;
                for (int i = 0; i < 6; ++i) {
                    if ((b >> i) & 1u) product ^= static_cast<uint16_t>(a << i);
                }
                for (int i = 10; i >= 6; --i) {
                    if ((product >> i) & 1u) product ^= static_cast<uint16_t>(0x43u << (i - 6));
                }
                values[a][b] = static_cast<uint8_t>(product & 0x3fu);
            }
        }
        return values;
    }();
    return table;
}

inline uint8_t gf64Multiply(uint8_t a, uint8_t b)
{
    return gf64Products()[a & 0x3fu][b & 0x3fu];
}

inline uint8_t gf64Inverse(uint8_t a)
{
    static const auto inverses = [] {
        std::array<uint8_t, 64> values{};
        const auto& products = gf64Products();
        for (size_t a = 1; a < 64; ++a) {
            for (size_t b = 1; b < 64; ++b) {
                if (products[a][b] == 1) {
                    values[a] = static_cast<uint8_t>(b);
                    break;
                }
            }
        }
        return values;
    }();
    return inverses[a & 0x3fu];
}

} // namespace p25fec
