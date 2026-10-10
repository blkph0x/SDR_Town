#include "frontend/DvbFec.h"

#include "frontend/DvbDemod.h"

#include <array>
#include <cstdint>
#include <memory>

namespace {

#include "Dvbs2LdpcTables.inc"

struct Code {
    bool normal = false;
    int modcod = 0;
    int kMessage = 0;
    int kBch = 0;
    int nLdpc = 0;
    int q = 0;
    int t = 0;
    const int* packed = nullptr;
    int packedCount = 0;
};

const Code kCodes[] = {
    {true, 1, 16008, 16200, 64800, 135, 12, kLdpcN14, kLdpcN14Count},
    {true, 2, 21408, 21600, 64800, 120, 12, kLdpcN13, kLdpcN13Count},
    {true, 3, 25728, 25920, 64800, 108, 12, kLdpcN25, kLdpcN25Count},
    {true, 4, 32208, 32400, 64800, 90, 12, kLdpcN12, kLdpcN12Count},
    {true, 5, 38688, 38880, 64800, 72, 12, kLdpcN35, kLdpcN35Count},
    {true, 6, 43040, 43200, 64800, 60, 10, kLdpcN23, kLdpcN23Count},
    {true, 7, 48408, 48600, 64800, 45, 12, kLdpcN34, kLdpcN34Count},
    {true, 8, 51648, 51840, 64800, 36, 12, kLdpcN45, kLdpcN45Count},
    {true, 9, 53840, 54000, 64800, 30, 10, kLdpcN56, kLdpcN56Count},
    {true, 10, 57472, 57600, 64800, 20, 8, kLdpcN89, kLdpcN89Count},
    {true, 11, 58192, 58320, 64800, 18, 8, kLdpcN910, kLdpcN910Count},
    {false, 1, 3072, 3240, 16200, 36, 12, kLdpcS14, kLdpcS14Count},
    {false, 2, 5232, 5400, 16200, 30, 12, kLdpcS13, kLdpcS13Count},
    {false, 3, 6312, 6480, 16200, 27, 12, kLdpcS25, kLdpcS25Count},
    {false, 4, 7032, 7200, 16200, 25, 12, kLdpcS12, kLdpcS12Count},
    {false, 5, 9552, 9720, 16200, 18, 12, kLdpcS35, kLdpcS35Count},
    {false, 6, 10632, 10800, 16200, 15, 12, kLdpcS23, kLdpcS23Count},
    {false, 7, 11712, 11880, 16200, 12, 12, kLdpcS34, kLdpcS34Count},
    {false, 8, 12432, 12600, 16200, 10, 12, kLdpcS45, kLdpcS45Count},
    {false, 9, 13152, 13320, 16200, 8, 12, kLdpcS56, kLdpcS56Count},
    {false, 10, 14232, 14400, 16200, 5, 12, kLdpcS89, kLdpcS89Count},
};

const Code* codeFor(bool normal, int modcod) {
    for (const Code& code : kCodes)
        if (code.normal == normal && code.modcod == modcod) return &code;
    return nullptr;
}

template<int M, int Reduction>
struct Field {
    static constexpr int Order = (1 << M) - 1;
    static constexpr int Mask = (1 << M) - 1;
    std::uint16_t exp[1 << M]{};
    std::uint16_t log[1 << M]{};
    Field() {
        std::uint32_t value = 1;
        for (int i = 0; i < Order; ++i) {
            exp[i] = static_cast<std::uint16_t>(value);
            log[value] = static_cast<std::uint16_t>(i);
            value <<= 1;
            if (value & (1u << M)) value ^= static_cast<std::uint32_t>(Reduction);
            value &= static_cast<std::uint32_t>(Mask);
        }
    }
    std::uint16_t mul(std::uint16_t a, std::uint16_t b) const {
        if (a == 0 || b == 0) return 0;
        return exp[(static_cast<int>(log[a]) + log[b]) % Order];
    }
    std::uint16_t inv(std::uint16_t a) const {
        return exp[(Order - log[a]) % Order];
    }
    std::uint16_t pow(int exponent) const {
        int wrapped = exponent % Order;
        if (wrapped < 0) wrapped += Order;
        return exp[wrapped];
    }
};

using ShortField = Field<14, 0x2B>;
using NormalField = Field<16, 0x2D>;

const ShortField& shortField() {
    static const ShortField instance;
    return instance;
}

const NormalField& normalField() {
    static const NormalField instance;
    return instance;
}

struct Big {
    std::uint64_t word[4]{};
};

int bitOf(const Big& value, int index) {
    return static_cast<int>((value.word[index / 64] >> (index % 64)) & 1ull);
}

void setBit(Big& value, int index) {
    value.word[index / 64] |= 1ull << (index % 64);
}

void clearFrom(Big& value, int index) {
    for (int i = index; i < 256; ++i)
        value.word[i / 64] &= ~(1ull << (i % 64));
}

Big multiply(Big left, Big right) {
    Big out;
    for (int i = 0; i < 256; ++i) {
        if (!bitOf(right, i)) continue;
        for (int j = 0; j < 256 - i; ++j) {
            if (!bitOf(left, j)) continue;
            const int at = i + j;
            out.word[at / 64] ^= 1ull << (at % 64);
        }
    }
    return out;
}

Big maskFrom(const int* exponents, int count) {
    Big out;
    for (int i = 0; i < count; ++i) setBit(out, exponents[i]);
    return out;
}

Big productOf(const int polys[][14], int count) {
    Big product;
    setBit(product, 0);
    for (int row = 0; row < count; ++row) {
        int terms = 0;
        while (terms < 14 && polys[row][terms] >= 0) ++terms;
        product = multiply(product, maskFrom(polys[row], terms));
    }
    return product;
}

const Big& shortGenerator() {
    static const Big value = [] {
        const int polys[][14] = {
            {0, 1, 3, 5, 14, -1},
            {0, 6, 8, 11, 14, -1},
            {0, 1, 2, 6, 9, 10, 14, -1},
            {0, 4, 7, 8, 10, 12, 14, -1},
            {0, 2, 4, 6, 8, 9, 11, 13, 14, -1},
            {0, 3, 7, 8, 9, 13, 14, -1},
            {0, 2, 5, 6, 7, 10, 11, 13, 14, -1},
            {0, 5, 8, 9, 10, 11, 14, -1},
            {0, 1, 2, 3, 9, 10, 14, -1},
            {0, 3, 6, 9, 11, 12, 14, -1},
            {0, 4, 11, 12, 14, -1},
            {0, 1, 2, 3, 5, 6, 7, 8, 10, 13, 14, -1},
        };
        return productOf(polys, 12);
    }();
    return value;
}

const Big& normalGenerator(int t) {
    static const Big all = [] {
        const int polys[][14] = {
            {0, 2, 3, 5, 16, -1},
            {0, 1, 4, 5, 6, 8, 16, -1},
            {0, 2, 3, 4, 5, 7, 8, 9, 10, 11, 16, -1},
            {0, 2, 4, 6, 9, 11, 12, 14, 16, -1},
            {0, 1, 2, 3, 5, 8, 9, 10, 11, 12, 16, -1},
            {0, 2, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, -1},
            {0, 2, 5, 6, 8, 9, 10, 11, 13, 15, 16, -1},
            {0, 1, 2, 5, 6, 8, 9, 12, 13, 14, 16, -1},
            {0, 5, 7, 9, 10, 11, 16, -1},
            {0, 1, 2, 5, 7, 8, 10, 12, 13, 14, 16, -1},
            {0, 2, 3, 5, 9, 11, 12, 13, 16, -1},
            {0, 1, 5, 6, 7, 9, 11, 12, 16, -1},
        };
        return productOf(polys, 12);
    }();
    static const Big t10 = [] {
        const int polys[][14] = {
            {0, 2, 3, 5, 16, -1},
            {0, 1, 4, 5, 6, 8, 16, -1},
            {0, 2, 3, 4, 5, 7, 8, 9, 10, 11, 16, -1},
            {0, 2, 4, 6, 9, 11, 12, 14, 16, -1},
            {0, 1, 2, 3, 5, 8, 9, 10, 11, 12, 16, -1},
            {0, 2, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, -1},
            {0, 2, 5, 6, 8, 9, 10, 11, 13, 15, 16, -1},
            {0, 1, 2, 5, 6, 8, 9, 12, 13, 14, 16, -1},
            {0, 5, 7, 9, 10, 11, 16, -1},
            {0, 1, 2, 5, 7, 8, 10, 12, 13, 14, 16, -1},
        };
        return productOf(polys, 10);
    }();
    static const Big t8 = [] {
        const int polys[][14] = {
            {0, 2, 3, 5, 16, -1},
            {0, 1, 4, 5, 6, 8, 16, -1},
            {0, 2, 3, 4, 5, 7, 8, 9, 10, 11, 16, -1},
            {0, 2, 4, 6, 9, 11, 12, 14, 16, -1},
            {0, 1, 2, 3, 5, 8, 9, 10, 11, 12, 16, -1},
            {0, 2, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, -1},
            {0, 2, 5, 6, 8, 9, 10, 11, 13, 15, 16, -1},
            {0, 1, 2, 5, 6, 8, 9, 12, 13, 14, 16, -1},
        };
        return productOf(polys, 8);
    }();
    if (t == 8) return t8;
    if (t == 10) return t10;
    return all;
}

const Big& generatorFor(const Code& code) {
    return code.normal ? normalGenerator(code.t) : shortGenerator();
}

bool bchEncode(const Code& code, const std::vector<int>& message, std::vector<int>& coded) {
    if (static_cast<int>(message.size()) != code.kMessage) return false;
    const int parity = code.kBch - code.kMessage;
    Big poly = generatorFor(code);
    clearFrom(poly, parity);
    std::uint64_t reg[4] = {};
    const int words = (parity + 63) / 64;
    const int used = parity % 64;
    for (int bit : message) {
        const int top = parity - 1;
        const int feedback = static_cast<int>((reg[top / 64] >> (top % 64)) & 1ull) ^ (bit & 1);
        int carry = 0;
        for (int i = 0; i < words; ++i) {
            const int next = static_cast<int>(reg[i] >> 63);
            reg[i] = (reg[i] << 1) | static_cast<std::uint64_t>(carry);
            carry = next;
        }
        if (used != 0) reg[words - 1] &= (1ull << used) - 1;
        if (feedback) {
            for (int i = 0; i < words; ++i) reg[i] ^= poly.word[i];
            if (used != 0) reg[words - 1] &= (1ull << used) - 1;
        }
    }
    coded = message;
    coded.resize(static_cast<std::size_t>(code.kBch));
    for (int i = 0; i < parity; ++i) {
        const int index = parity - 1 - i;
        coded[static_cast<std::size_t>(code.kMessage + i)] =
            static_cast<int>((reg[index / 64] >> (index % 64)) & 1ull);
    }
    return true;
}

template<typename Gf>
std::array<std::uint16_t, 25> syndromes(const Gf& gf, const std::vector<int>& coded, int t) {
    std::array<std::uint16_t, 25> out{};
    for (int power = 1; power <= 2 * t; ++power) {
        std::uint16_t acc = 0;
        const std::uint16_t step = gf.pow(power);
        for (int bit : coded) acc = static_cast<std::uint16_t>(gf.mul(acc, step) ^ (bit & 1));
        out[static_cast<std::size_t>(power)] = acc;
    }
    return out;
}

bool syndromesClear(const std::array<std::uint16_t, 25>& value, int t) {
    for (int i = 1; i <= 2 * t; ++i)
        if (value[static_cast<std::size_t>(i)] != 0) return false;
    return true;
}

template<typename Gf>
bool bchCorrect(const Gf& gf, const Code& code, std::vector<int>& coded) {
    if (static_cast<int>(coded.size()) != code.kBch) return false;
    const int t = code.t;
    const auto syn = syndromes(gf, coded, t);
    if (syndromesClear(syn, t)) return true;
    std::vector<std::uint16_t> locator{1};
    std::vector<std::uint16_t> previous{1};
    int degree = 0;
    int shift = 1;
    std::uint16_t discrepancyScale = 1;
    for (int step = 0; step < 2 * t; ++step) {
        std::uint16_t delta = syn[static_cast<std::size_t>(step + 1)];
        for (int i = 1; i <= degree && i < static_cast<int>(locator.size()); ++i)
            delta = static_cast<std::uint16_t>(
                delta ^ gf.mul(locator[static_cast<std::size_t>(i)], syn[static_cast<std::size_t>(step + 1 - i)]));
        if (delta == 0) {
            ++shift;
            continue;
        }
        const std::uint16_t coef = gf.mul(delta, gf.inv(discrepancyScale));
        std::vector<std::uint16_t> saved = locator;
        std::vector<std::uint16_t> moved(static_cast<std::size_t>(shift), 0);
        moved.insert(moved.end(), previous.begin(), previous.end());
        if (moved.size() > locator.size()) locator.resize(moved.size());
        for (std::size_t i = 0; i < moved.size(); ++i)
            locator[i] = static_cast<std::uint16_t>(locator[i] ^ gf.mul(coef, moved[i]));
        while (locator.size() > 1 && locator.back() == 0) locator.pop_back();
        if (2 * degree <= step) {
            degree = step + 1 - degree;
            previous = saved;
            discrepancyScale = delta;
            shift = 1;
        } else {
            ++shift;
        }
    }
    const int errors = static_cast<int>(locator.size()) - 1;
    if (errors <= 0 || errors > t) return false;
    std::vector<int> positions;
    for (int i = 0; i < code.kBch; ++i) {
        const std::uint16_t point = gf.pow(-(code.kBch - 1 - i));
        std::uint16_t acc = 0;
        std::uint16_t power = 1;
        for (std::uint16_t coeff : locator) {
            acc = static_cast<std::uint16_t>(acc ^ gf.mul(coeff, power));
            power = gf.mul(power, point);
        }
        if (acc == 0) positions.push_back(i);
    }
    if (static_cast<int>(positions.size()) != errors) return false;
    for (int position : positions) coded[static_cast<std::size_t>(position)] ^= 1;
    return syndromesClear(syndromes(gf, coded, t), t);
}

template<typename Fn>
void eachAddress(const Code& code, Fn&& fn) {
    const int* cursor = code.packed;
    const int* end = cursor + code.packedCount;
    int row = 0;
    while (cursor < end) {
        const int count = *cursor++;
        fn(row, cursor, count);
        cursor += count;
        ++row;
    }
}

void ldpcEncodeInto(const Code& code, const std::vector<int>& info, std::vector<int>& codeword) {
    const int parityLength = code.nLdpc - code.kBch;
    std::vector<int> parity(static_cast<std::size_t>(parityLength), 0);
    eachAddress(code, [&](int row, const int* addresses, int count) {
        for (int bit = 0; bit < 360; ++bit) {
            if (!info[static_cast<std::size_t>(row * 360 + bit)]) continue;
            for (int n = 0; n < count; ++n) {
                const int at = (addresses[n] + bit * code.q) % parityLength;
                parity[static_cast<std::size_t>(at)] ^= 1;
            }
        }
    });
    for (int i = 1; i < parityLength; ++i)
        parity[static_cast<std::size_t>(i)] ^= parity[static_cast<std::size_t>(i - 1)];
    codeword = info;
    codeword.insert(codeword.end(), parity.begin(), parity.end());
}

int failedChecks(const Code& code, const std::vector<int>& bits) {
    const int parityLength = code.nLdpc - code.kBch;
    std::vector<int> acc(static_cast<std::size_t>(parityLength), 0);
    eachAddress(code, [&](int row, const int* addresses, int count) {
        for (int bit = 0; bit < 360; ++bit) {
            if (!bits[static_cast<std::size_t>(row * 360 + bit)]) continue;
            for (int n = 0; n < count; ++n) {
                const int at = (addresses[n] + bit * code.q) % parityLength;
                acc[static_cast<std::size_t>(at)] ^= 1;
            }
        }
    });
    if (bits[static_cast<std::size_t>(code.kBch)]) acc[0] ^= 1;
    for (int j = 1; j < parityLength; ++j) {
        if (bits[static_cast<std::size_t>(code.kBch + j)] ^ bits[static_cast<std::size_t>(code.kBch + j - 1)])
            acc[static_cast<std::size_t>(j)] ^= 1;
    }
    int bad = 0;
    for (int bit : acc) bad += bit;
    return bad;
}

struct Graph {
    std::vector<std::pair<int, int>> edges;
    std::vector<std::vector<int>> atCheck;
    std::vector<std::vector<int>> atVar;
    explicit Graph(const Code& code)
        : atCheck(static_cast<std::size_t>(code.nLdpc - code.kBch)),
          atVar(static_cast<std::size_t>(code.nLdpc)) {
        const int parityLength = code.nLdpc - code.kBch;
        auto add = [&](int var, int check) {
            atVar[static_cast<std::size_t>(var)].push_back(static_cast<int>(edges.size()));
            atCheck[static_cast<std::size_t>(check)].push_back(static_cast<int>(edges.size()));
            edges.emplace_back(var, check);
        };
        eachAddress(code, [&](int row, const int* addresses, int count) {
            for (int bit = 0; bit < 360; ++bit) {
                for (int n = 0; n < count; ++n)
                    add(row * 360 + bit, (addresses[n] + bit * code.q) % parityLength);
            }
        });
        add(code.kBch, 0);
        for (int j = 1; j < parityLength; ++j) {
            add(code.kBch + j, j);
            add(code.kBch + j - 1, j);
        }
    }
};

const Graph& graphFor(const Code& code) {
    static std::array<std::unique_ptr<Graph>, 21> cache;
    const int index = static_cast<int>(&code - kCodes);
    if (!cache[static_cast<std::size_t>(index)])
        cache[static_cast<std::size_t>(index)] = std::make_unique<Graph>(code);
    return *cache[static_cast<std::size_t>(index)];
}

bool correctBch(const Code& code, std::vector<int>& coded, std::vector<int>& message) {
    const bool ok = code.normal
        ? bchCorrect(normalField(), code, coded)
        : bchCorrect(shortField(), code, coded);
    if (!ok) return false;
    message.assign(coded.begin(), coded.begin() + code.kMessage);
    return true;
}

ShortHalfFec decodeCode(const Code& code, const std::vector<int>& hardBits) {
    ShortHalfFec out;
    out.note = "QPSK codeword length does not match this rate.";
    if (static_cast<int>(hardBits.size()) != code.nLdpc) return out;
    std::vector<int> hard = hardBits;
    if (failedChecks(code, hard) == 0) {
        out.ldpcConverged = true;
    } else {
        const Graph& net = graphFor(code);
        const int parityLength = code.nLdpc - code.kBch;
        std::vector<float> llr(static_cast<std::size_t>(code.nLdpc));
        for (int i = 0; i < code.nLdpc; ++i)
            llr[static_cast<std::size_t>(i)] = hardBits[static_cast<std::size_t>(i)] ? -8.0f : 8.0f;
        std::vector<float> checkToVar(net.edges.size(), 0.0f);
        for (int iteration = 1; iteration <= 30; ++iteration) {
            for (int check = 0; check < parityLength; ++check) {
                const auto& links = net.atCheck[static_cast<std::size_t>(check)];
                std::vector<float> incoming(links.size());
                for (std::size_t n = 0; n < links.size(); ++n) {
                    const int edge = links[n];
                    incoming[n] = llr[static_cast<std::size_t>(net.edges[static_cast<std::size_t>(edge)].first)]
                        - checkToVar[static_cast<std::size_t>(edge)];
                }
                for (std::size_t n = 0; n < links.size(); ++n) {
                    int sign = 1;
                    float magnitude = 1.0e9f;
                    for (std::size_t other = 0; other < incoming.size(); ++other) {
                        if (other == n) continue;
                        if (incoming[other] < 0.0f) sign = -sign;
                        const float absolute = incoming[other] < 0.0f ? -incoming[other] : incoming[other];
                        if (absolute < magnitude) magnitude = absolute;
                    }
                    if (magnitude > 1.0e8f) magnitude = 0.0f;
                    checkToVar[static_cast<std::size_t>(links[n])] = 0.75f * static_cast<float>(sign) * magnitude;
                }
            }
            for (int var = 0; var < code.nLdpc; ++var) {
                float total = llr[static_cast<std::size_t>(var)];
                for (int edge : net.atVar[static_cast<std::size_t>(var)])
                    total += checkToVar[static_cast<std::size_t>(edge)];
                hard[static_cast<std::size_t>(var)] = total >= 0.0f ? 0 : 1;
            }
            out.iterations = iteration;
            if (failedChecks(code, hard) == 0) {
                out.ldpcConverged = true;
                break;
            }
        }
    }
    if (!out.ldpcConverged) {
        out.note = "LDPC did not converge.";
        return out;
    }
    std::vector<int> bch(hard.begin(), hard.begin() + code.kBch);
    if (!correctBch(code, bch, out.messageBits)) {
        out.note = "BCH rejected the LDPC output.";
        return out;
    }
    out.bchOk = true;
    out.note = "QPSK FECFRAME corrected. 8PSK and APSK are not implemented. Commercial decrypt is not performed.";
    return out;
}

}  // namespace

bool dvbs2QpskInfoBits(bool normalFrame, int modcod, int& messageBits, int& codewordBits) {
    const Code* code = codeFor(normalFrame, modcod);
    if (!code) return false;
    messageBits = code->kMessage;
    codewordBits = code->nLdpc;
    return true;
}

bool encodeDvbs2Qpsk(bool normalFrame, int modcod,
    const std::vector<int>& message, std::vector<int>& codeword) {
    const Code* code = codeFor(normalFrame, modcod);
    if (!code) return false;
    std::vector<int> bch;
    if (!bchEncode(*code, message, bch)) return false;
    ldpcEncodeInto(*code, bch, codeword);
    return static_cast<int>(codeword.size()) == code->nLdpc;
}

ShortHalfFec decodeDvbs2Qpsk(bool normalFrame, int modcod, const std::vector<int>& hardBits) {
    const Code* code = codeFor(normalFrame, modcod);
    if (!code) {
        ShortHalfFec out;
        out.note = normalFrame
            ? "That QPSK rate is not available."
            : "Short frames have no rate 9/10.";
        return out;
    }
    return decodeCode(*code, hardBits);
}

ShortHalfFec demodDvbs2Frame(const std::complex<float>* symbols, std::size_t count) {
    const auto header = detectDvbs2PlHeader(symbols, count);
    ShortHalfFec out;
    if (!header.plsDecoded) {
        out.note = "PL header was not found.";
        return out;
    }
    const auto info = dvbs2Modcod(header.modcod);
    if (!info.known) {
        out.note = "That MODCOD is not decoded.";
        return out;
    }
    if (header.shortFrame && info.rateModcod == 11) {
        out.note = "Short frames have no rate 9/10.";
        return out;
    }
    const int payloadSymbols = dvbs2PayloadSymbols(header.modcod, header.shortFrame, header.pilots);
    const auto payload = extractDvbs2DataSymbols(symbols, count, static_cast<std::size_t>(payloadSymbols));
    if (static_cast<int>(payload.data.size()) != payloadSymbols) {
        out.note = "Payload was shorter than one FECFRAME.";
        return out;
    }
    const auto sliced = sliceDvbs2FecBits(payload.data.data(), payload.data.size(),
        header.modcod, header.shortFrame, header.pilots);
    if (!sliced.sliced) {
        out.note = sliced.note;
        return out;
    }
    auto decoded = decodeDvbs2Qpsk(!header.shortFrame, info.rateModcod, sliced.fecBits);
    if (decoded.bchOk)
        decoded.note = "FECFRAME corrected. The app does not decode pictures. Commercial decrypt is not performed.";
    return decoded;
}

ShortHalfFec demodDvbs2QpskFrame(const std::complex<float>* symbols, std::size_t count) {
    const auto header = detectDvbs2PlHeader(symbols, count);
    if (header.plsDecoded && (header.modcod < 1 || header.modcod > 11)) {
        ShortHalfFec out;
        out.note = "Not a QPSK MODCOD.";
        return out;
    }
    return demodDvbs2Frame(symbols, count);
}

ShortHalfFec demodDvbs2IqFrame(const std::complex<float>* samples, std::size_t count,
    double sampleRateHz, double symbolRateHz, double rollOff) {
    const auto timed = recoverDvbs2Symbols(samples, count, sampleRateHz, symbolRateHz, rollOff);
    if (!timed.locked) {
        ShortHalfFec out;
        out.note = timed.note;
        return out;
    }
    return demodDvbs2Frame(timed.symbols.data(), timed.symbols.size());
}

bool encodeDvbs2ShortBch(const std::vector<int>& message, std::vector<int>& coded) {
    const Code* code = codeFor(false, 4);
    return code && bchEncode(*code, message, coded);
}

bool decodeDvbs2ShortBch(std::vector<int> coded, std::vector<int>& message) {
    const Code* code = codeFor(false, 4);
    return code && correctBch(*code, coded, message);
}

bool encodeDvbs2ShortHalf(const std::vector<int>& message, std::vector<int>& codeword) {
    return encodeDvbs2Qpsk(false, 4, message, codeword);
}

ShortHalfFec decodeDvbs2ShortHalf(const std::vector<int>& hardBits) {
    return decodeDvbs2Qpsk(false, 4, hardBits);
}

ShortHalfFec demodDvbs2ShortHalfFrame(const std::complex<float>* symbols, std::size_t count) {
    const auto header = detectDvbs2PlHeader(symbols, count);
    if (!header.plsDecoded || header.modcod != 4 || !header.shortFrame || header.pilots) {
        ShortHalfFec out;
        out.note = "Only short QPSK 1/2 without pilots is decoded by this entry. 8PSK and APSK are not implemented.";
        return out;
    }
    return demodDvbs2QpskFrame(symbols, count);
}
