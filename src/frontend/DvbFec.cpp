#include "frontend/DvbFec.h"

#include <array>
#include <cstdint>

namespace {
constexpr int kMessage = 7032;
constexpr int kBch = 7200;
constexpr int kParityBch = 168;
constexpr int kLdpc = 16200;
constexpr int kParityLdpc = 9000;
constexpr int kQ = 25;

constexpr int kRows[20][8] = {
    {20, 712, 2386, 6354, 4061, 1062, 5045, 5158},
    {21, 2543, 5748, 4822, 2348, 3089, 6328, 5876},
    {22, 926, 5701, 269, 3693, 2438, 3190, 3507},
    {23, 2802, 4520, 3577, 5324, 1091, 4667, 4449},
    {24, 5140, 2003, 1263, 4742, 6497, 1185, 6202},
    {0, 4046, 6934, -1, -1, -1, -1, -1},
    {1, 2855, 66, -1, -1, -1, -1, -1},
    {2, 6694, 212, -1, -1, -1, -1, -1},
    {3, 3439, 1158, -1, -1, -1, -1, -1},
    {4, 3850, 4422, -1, -1, -1, -1, -1},
    {5, 5924, 290, -1, -1, -1, -1, -1},
    {6, 1467, 4049, -1, -1, -1, -1, -1},
    {7, 7820, 2242, -1, -1, -1, -1, -1},
    {8, 4606, 3080, -1, -1, -1, -1, -1},
    {9, 4633, 7877, -1, -1, -1, -1, -1},
    {10, 3884, 6868, -1, -1, -1, -1, -1},
    {11, 8935, 4996, -1, -1, -1, -1, -1},
    {12, 3028, 764, -1, -1, -1, -1, -1},
    {13, 5988, 1057, -1, -1, -1, -1, -1},
    {14, 7411, 3450, -1, -1, -1, -1, -1},
};

struct Field {
    std::uint16_t exp[16384]{};
    std::uint16_t log[16384]{};
    Field() {
        std::uint16_t value = 1;
        for (int i = 0; i < 16383; ++i) {
            exp[i] = value;
            log[value] = static_cast<std::uint16_t>(i);
            value = static_cast<std::uint16_t>(value << 1);
            if (value & (1u << 14)) value = static_cast<std::uint16_t>((value ^ 0x2B) & 0x3FFF);
        }
    }
    std::uint16_t mul(std::uint16_t a, std::uint16_t b) const {
        if (a == 0 || b == 0) return 0;
        return exp[(static_cast<int>(log[a]) + log[b]) % 16383];
    }
    std::uint16_t inv(std::uint16_t a) const {
        return exp[(16383 - log[a]) % 16383];
    }
    std::uint16_t pow(int exponent) const {
        int wrapped = exponent % 16383;
        if (wrapped < 0) wrapped += 16383;
        return exp[wrapped];
    }
};

const Field& field() {
    static const Field instance;
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

const Big& generator() {
    static const Big value = [] {
        const int polys[][12] = {
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
        Big product;
        setBit(product, 0);
        for (const auto& poly : polys) {
            int count = 0;
            while (count < 12 && poly[count] >= 0) ++count;
            product = multiply(product, maskFrom(poly, count));
        }
        return product;
    }();
    return value;
}

void xorLower(std::uint64_t reg[3]) {
    const Big& poly = generator();
    reg[0] ^= poly.word[0];
    reg[1] ^= poly.word[1];
    reg[2] ^= poly.word[2] & ((1ull << 40) - 1);
}

bool bchEncodeBits(const std::vector<int>& message, std::vector<int>& coded) {
    if (static_cast<int>(message.size()) != kMessage) return false;
    std::uint64_t reg[3] = {};
    for (int bit : message) {
        const int feedback = static_cast<int>((reg[2] >> 39) & 1ull) ^ (bit & 1);
        const int carry0 = static_cast<int>(reg[0] >> 63);
        const int carry1 = static_cast<int>(reg[1] >> 63);
        reg[0] <<= 1;
        reg[1] = (reg[1] << 1) | static_cast<std::uint64_t>(carry0);
        reg[2] = ((reg[2] << 1) | static_cast<std::uint64_t>(carry1)) & ((1ull << 40) - 1);
        if (feedback) xorLower(reg);
    }
    coded = message;
    coded.resize(kBch);
    for (int i = 0; i < kParityBch; ++i) {
        const int index = 167 - i;
        const int bit = static_cast<int>((reg[index / 64] >> (index % 64)) & 1ull);
        coded[kMessage + i] = bit;
    }
    return true;
}

std::array<std::uint16_t, 25> syndromes(const std::vector<int>& coded) {
    const Field& gf = field();
    std::array<std::uint16_t, 25> out{};
    for (int power = 1; power <= 24; ++power) {
        std::uint16_t acc = 0;
        const std::uint16_t step = gf.pow(power);
        for (int bit : coded) acc = static_cast<std::uint16_t>(gf.mul(acc, step) ^ (bit & 1));
        out[static_cast<std::size_t>(power)] = acc;
    }
    return out;
}

bool syndromesClear(const std::array<std::uint16_t, 25>& value) {
    for (int i = 1; i <= 24; ++i)
        if (value[static_cast<std::size_t>(i)] != 0) return false;
    return true;
}

bool bchCorrect(std::vector<int>& coded) {
    if (static_cast<int>(coded.size()) != kBch) return false;
    const auto syn = syndromes(coded);
    if (syndromesClear(syn)) return true;
    const Field& gf = field();
    std::vector<std::uint16_t> locator{1};
    std::vector<std::uint16_t> previous{1};
    int degree = 0;
    int shift = 1;
    std::uint16_t discrepancyScale = 1;
    for (int step = 0; step < 24; ++step) {
        std::uint16_t delta = syn[static_cast<std::size_t>(step + 1)];
        for (int i = 1; i <= degree && i < static_cast<int>(locator.size()); ++i)
            delta = static_cast<std::uint16_t>(delta ^ gf.mul(locator[static_cast<std::size_t>(i)], syn[static_cast<std::size_t>(step + 1 - i)]));
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
    if (errors <= 0 || errors > 12) return false;
    std::vector<int> positions;
    for (int i = 0; i < kBch; ++i) {
        const std::uint16_t point = gf.pow(-(kBch - 1 - i));
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
    return syndromesClear(syndromes(coded));
}

void ldpcEncodeInto(const std::vector<int>& info, std::vector<int>& codeword) {
    std::vector<int> parity(kParityLdpc, 0);
    for (int row = 0; row < 20; ++row) {
        for (int bit = 0; bit < 360; ++bit) {
            if (!info[static_cast<std::size_t>(row * 360 + bit)]) continue;
            for (int address : kRows[row]) {
                if (address < 0) break;
                parity[static_cast<std::size_t>((address + bit * kQ) % kParityLdpc)] ^= 1;
            }
        }
    }
    for (int i = 1; i < kParityLdpc; ++i) parity[static_cast<std::size_t>(i)] ^= parity[static_cast<std::size_t>(i - 1)];
    codeword = info;
    codeword.insert(codeword.end(), parity.begin(), parity.end());
}

int failedChecks(const std::vector<int>& code) {
    std::vector<int> acc(kParityLdpc, 0);
    for (int row = 0; row < 20; ++row) {
        for (int bit = 0; bit < 360; ++bit) {
            if (!code[static_cast<std::size_t>(row * 360 + bit)]) continue;
            for (int address : kRows[row]) {
                if (address < 0) break;
                acc[static_cast<std::size_t>((address + bit * kQ) % kParityLdpc)] ^= 1;
            }
        }
    }
    if (code[kBch]) acc[0] ^= 1;
    for (int j = 1; j < kParityLdpc; ++j)
        if (code[static_cast<std::size_t>(kBch + j)] ^ code[static_cast<std::size_t>(kBch + j - 1)])
            acc[static_cast<std::size_t>(j)] ^= 1;
    int bad = 0;
    for (int bit : acc) bad += bit;
    return bad;
}

struct Graph {
    std::vector<std::pair<int, int>> edges;
    std::vector<std::vector<int>> atCheck;
    std::vector<std::vector<int>> atVar;
    Graph() : atCheck(kParityLdpc), atVar(kLdpc) {
        auto add = [&](int var, int check) {
            atVar[static_cast<std::size_t>(var)].push_back(static_cast<int>(edges.size()));
            atCheck[static_cast<std::size_t>(check)].push_back(static_cast<int>(edges.size()));
            edges.emplace_back(var, check);
        };
        for (int row = 0; row < 20; ++row) {
            for (int bit = 0; bit < 360; ++bit) {
                for (int address : kRows[row]) {
                    if (address < 0) break;
                    add(row * 360 + bit, (address + bit * kQ) % kParityLdpc);
                }
            }
        }
        add(kBch, 0);
        for (int j = 1; j < kParityLdpc; ++j) {
            add(kBch + j, j);
            add(kBch + j - 1, j);
        }
    }
};

const Graph& graph() {
    static const Graph instance;
    return instance;
}
}

bool encodeDvbs2ShortBch(const std::vector<int>& message, std::vector<int>& coded) {
    return bchEncodeBits(message, coded);
}

bool decodeDvbs2ShortBch(std::vector<int> coded, std::vector<int>& message) {
    if (!bchCorrect(coded)) return false;
    message.assign(coded.begin(), coded.begin() + kMessage);
    return true;
}

bool encodeDvbs2ShortHalf(const std::vector<int>& message, std::vector<int>& codeword) {
    std::vector<int> bch;
    if (!bchEncodeBits(message, bch)) return false;
    ldpcEncodeInto(bch, codeword);
    return static_cast<int>(codeword.size()) == kLdpc;
}

ShortHalfFec decodeDvbs2ShortHalf(const std::vector<int>& hardBits) {
    ShortHalfFec out;
    out.note = "Short FECFRAME nominal rate 1/2 only. Other rates are not implemented.";
    if (static_cast<int>(hardBits.size()) != kLdpc) {
        out.note = "Short rate 1/2 expects 16200 hard bits.";
        return out;
    }
    const Graph& net = graph();
    std::vector<float> llr(kLdpc);
    for (int i = 0; i < kLdpc; ++i) llr[static_cast<std::size_t>(i)] = hardBits[static_cast<std::size_t>(i)] ? -8.0f : 8.0f;
    std::vector<float> checkToVar(net.edges.size(), 0.0f);
    std::vector<int> hard = hardBits;
    for (int iteration = 1; iteration <= 30; ++iteration) {
        for (int check = 0; check < kParityLdpc; ++check) {
            const auto& links = net.atCheck[static_cast<std::size_t>(check)];
            std::vector<float> incoming(links.size());
            for (std::size_t n = 0; n < links.size(); ++n) {
                const int edge = links[n];
                incoming[n] = llr[static_cast<std::size_t>(net.edges[static_cast<std::size_t>(edge)].first)] - checkToVar[static_cast<std::size_t>(edge)];
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
        for (int var = 0; var < kLdpc; ++var) {
            float total = llr[static_cast<std::size_t>(var)];
            for (int edge : net.atVar[static_cast<std::size_t>(var)])
                total += checkToVar[static_cast<std::size_t>(edge)];
            hard[static_cast<std::size_t>(var)] = total >= 0.0f ? 0 : 1;
        }
        out.iterations = iteration;
        if (failedChecks(hard) == 0) {
            out.ldpcConverged = true;
            break;
        }
    }
    if (!out.ldpcConverged) {
        out.note = "LDPC did not converge. Other rates are not implemented.";
        return out;
    }
    std::vector<int> bch(hard.begin(), hard.begin() + kBch);
    if (!decodeDvbs2ShortBch(bch, out.messageBits)) {
        out.note = "BCH rejected the LDPC output. Other rates are not implemented.";
        return out;
    }
    out.bchOk = true;
    out.note = "Short FECFRAME nominal rate 1/2 corrected. Other rates are not implemented. Commercial decrypt is not performed.";
    return out;
}
