#include "RdsDecoder.h"
#include "src/block_sync.hh"
#include <algorithm>

namespace {
// Initial metadata milestone uses the shared basic Latin subset only.
// Do not reinterpret RDS extended bytes as UTF-8 or terminal controls.
char textChar(uint8_t c) { return c >= 32 && c <= 126 ? static_cast<char>(c) : '?'; }
}

struct RdsDecoder::Impl {
    redsea::BlockStream stream;
    RdsStation station;
    uint64_t bits = 0, rejected = 0;
    uint16_t candidatePi = 0;
    unsigned piRepeats = 0;
    std::array<char, 8> ps{};
    uint8_t psMask = 0;
    std::string previousPs;
    std::array<char, 64> rt{};
    uint64_t rtMask = 0;
    int rtFlag = -1, rtVersion = -1;

    Impl() { stream.init(redsea::Options{}); }

    void clearStation() {
        station = {};
        ps.fill(' '); psMask = 0; previousPs.clear();
        rt.fill(' '); rtMask = 0; rtFlag = rtVersion = -1;
    }

    void consume(const std::array<uint16_t, 4>& w) {
        // Follow redsea's three-repeat PI acquisition principle. Station state
        // is invalidated immediately on a different validated PI, never blended.
        if (!piRepeats || candidatePi != w[0]) {
            clearStation(); candidatePi = w[0]; piRepeats = 1;
        } else if (piRepeats < 3) ++piRepeats;
        if (piRepeats < 3) return;
        station.identified = true; station.pi = w[0];
        station.pty = static_cast<uint8_t>((w[1] >> 5) & 31);
        station.trafficProgramme = (w[1] & 0x400) != 0;
        const unsigned type = w[1] >> 12;
        const bool versionB = (w[1] & 0x800) != 0;
        if (type == 0) {
            station.trafficAnnouncement = (w[1] & 0x10) != 0;
            const unsigned segment = w[1] & 3;
            const char a = textChar(w[3] >> 8), b = textChar(w[3] & 255);
            if (!station.programmeService.empty() &&
                (station.programmeService[segment*2] != a || station.programmeService[segment*2+1] != b))
                station.programmeService.clear();
            ps[segment*2] = a; ps[segment*2+1] = b;
            psMask |= static_cast<uint8_t>(1 << segment);
            if (psMask == 15) {
                const std::string value(ps.begin(), ps.end());
                if (value == previousPs) station.programmeService = value;
                previousPs = value; psMask = 0;
            }
        } else if (type == 2) {
            const int flag = (w[1] >> 4) & 1;
            if (flag != rtFlag || static_cast<int>(versionB) != rtVersion) {
                rt.fill(' '); rtMask = 0; station.radioText.clear();
                rtFlag = flag; rtVersion = versionB;
            }
            const unsigned width = versionB ? 2 : 4;
            const unsigned offset = (w[1] & 15) * width;
            const std::array<uint8_t, 4> chars{static_cast<uint8_t>(w[2] >> 8),
                static_cast<uint8_t>(w[2]), static_cast<uint8_t>(w[3] >> 8), static_cast<uint8_t>(w[3])};
            std::array<char, 4> segment{};
            bool changed = false;
            for (unsigned i = 0; i < width; ++i) {
                const uint8_t c = chars[i + (versionB ? 2 : 0)];
                segment[i] = c == 13 ? '\r' : textChar(c);
                changed |= (rtMask & (uint64_t{1} << (offset+i))) && rt[offset+i] != segment[i];
            }
            // Clear once before storing, or a changed later byte would discard
            // earlier bytes of this same valid segment.
            if (changed) { rtMask = 0; station.radioText.clear(); }
            for (unsigned i = 0; i < width; ++i) {
                rt[offset+i] = segment[i];
                rtMask |= uint64_t{1} << (offset+i);
            }
            const unsigned limit = versionB ? 32 : 64;
            for (unsigned i = 0; i < limit; ++i) {
                if (!(rtMask & (uint64_t{1} << i))) break;
                if (rt[i] == '\r') {
                    station.radioText.assign(rt.begin(), rt.begin()+i); break;
                }
                if (i+1 == limit) station.radioText.assign(rt.begin(), rt.begin()+limit);
            }
        }
    }
};

RdsDecoder::RdsDecoder() : impl_(std::make_unique<Impl>()) {}
RdsDecoder::~RdsDecoder() = default;
void RdsDecoder::reset() { impl_ = std::make_unique<Impl>(); }
const RdsStation& RdsDecoder::station() const { return impl_->station; }
uint64_t RdsDecoder::rejectedGroups() const { return impl_->rejected; }

std::optional<RdsGroupEvent> RdsDecoder::pushBit(bool bit) {
    auto& s = *impl_;
    ++s.bits; s.stream.pushBit(bit);
    if (!s.stream.hasGroupReady()) return {};
    const auto g = s.stream.popGroup();
    for (auto block : {redsea::BLOCK1, redsea::BLOCK2, redsea::BLOCK3, redsea::BLOCK4}) {
        if (!g.has(block)) { ++s.rejected; return {}; }
    }
    RdsGroupEvent event;
    for (unsigned i = 0; i < 4; ++i) event.words[i] = g.get(static_cast<redsea::eBlockNumber>(i));
    const auto type = g.getType();
    const bool versionB = (event.words[1] & 0x800) != 0;
    if (!type.has_value || (versionB && event.words[2] != event.words[0])) {
        ++s.rejected; return {};
    }
    s.consume(event.words);
    event.endBit = s.bits; event.station = s.station;
    event.correctedBlocks = static_cast<unsigned>(g.getNumErrors());
    return event;
}
