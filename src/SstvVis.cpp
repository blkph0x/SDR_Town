#include "SstvVis.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

SstvVisDetector::SstvVisDetector(uint32_t rate) : rate_(rate) {
    if (rate < 8000 || rate > 96000) throw std::invalid_argument("SSTV audio rate must be 8..96 kHz");
    history_.resize(static_cast<size_t>((uint64_t{rate} * 910 + 999) / 1000) + 1);
}

void SstvVisDetector::reset() {
    samples_ = nextMs_ = candidates_ = parityRejected_ = framingRejected_ = 0;
    std::fill(history_.begin(), history_.end(), 0.0f);
}

uint64_t SstvVisDetector::atMs(uint64_t ms) const { return ms * rate_ / 1000; }

std::string_view SstvVisDetector::modeName(unsigned code) {
    // QSSTV sstvparam.cpp, seven data bits without the parity bit (DEC-0091).
    switch (code) {
    case 8: return "Robot 36";
    case 12: return "Robot 72";
    case 44: return "Martin M1";
    case 40: return "Martin M2";
    case 60: return "Scottie S1";
    case 56: return "Scottie S2";
    case 76: return "Scottie DX";
    case 95: return "PD120";
    default: return "Unknown";
    }
}

int SstvVisDetector::tone(uint64_t start, uint64_t end) const {
    constexpr std::array<int,4> frequencies{1100,1200,1300,1900};
    std::array<double,4> coefficient{}, previous{}, older{}, power{};
    for (size_t j=0;j<4;++j)
        coefficient[j]=2*std::cos(2*std::numbers::pi*frequencies[j]/rate_);
    double energy=0;
    for (auto i=start;i<end;++i) {
        const double x=history_[static_cast<size_t>(i%history_.size())];
        energy+=x*x;
        for (size_t j=0;j<4;++j) {
            const double current=x+coefficient[j]*previous[j]-older[j];
            older[j]=previous[j]; previous[j]=current;
        }
    }
    if (energy <= std::numeric_limits<double>::min()) return 0;
    for (size_t j=0;j<4;++j)
        power[j]=std::max(0.0,previous[j]*previous[j]+older[j]*older[j]-coefficient[j]*previous[j]*older[j]);
    const size_t best=static_cast<size_t>(std::max_element(power.begin(),power.end())-power.begin());
    double runner=0;
    for (size_t j=0;j<4;++j) if(j!=best) runner=std::max(runner,power[j]);
    // DEC-0091: normalized sinusoidal energy, independent of recording volume.
    return 2*power[best]/(double(end-start)*energy)>=0.75 && power[best]>2*runner ? frequencies[best] : 0;
}

bool SstvVisDetector::inspect(uint64_t start, SstvVisEvent& event) {
    const auto probe=[&](uint64_t ms) { return tone(atMs(start+ms),atMs(start+ms+10)); };
    // Cheap discriminating anchors first: a continuous 1900 Hz tone must not
    // force sixty redundant leader probes at every candidate position.
    if(probe(0)!=1900 || probe(300)!=1200 || probe(310)!=1900 || probe(620)!=1200) return false;
    // Check full leader interiors, not just one point that speech might match.
    for (uint64_t ms=10;ms<300;ms+=10) if(probe(ms)!=1900) return false;
    for (uint64_t ms=320;ms<610;ms+=10) if(probe(ms)!=1900) return false;
    if(probe(610)!=1200 || probe(630)!=1200) return false;
    ++candidates_;
    unsigned bits=0;
    for (unsigned bit=0;bit<8;++bit) {
        const int hz=probe(650+30*bit);
        if(hz!=1100 && hz!=1300) { ++framingRejected_; return false; }
        if(hz==1100) bits|=1u<<bit;
    }
    if(probe(880)!=1200 || probe(890)!=1200 || probe(900)!=1200) { ++framingRejected_; return false; }
    if(std::popcount(bits)%2) { ++parityRejected_; return false; }
    event={atMs(start),atMs(start+910),bits&127u,std::string(modeName(bits&127u))};
    return true;
}

std::vector<SstvVisEvent> SstvVisDetector::process(std::span<const float> input) {
    if(input.size()>8192 || !std::all_of(input.begin(),input.end(),[](float x){return std::isfinite(x);})) {
        reset();
        throw std::invalid_argument("SSTV block must contain <=8192 finite samples; detector reset");
    }
    // Bound the epoch before sample-index arithmetic could overflow.
    if(samples_>std::numeric_limits<uint64_t>::max()/1000-input.size()) {
        reset(); throw std::overflow_error("SSTV sample epoch overflow; detector reset");
    }
    std::vector<SstvVisEvent> result;
    for(float x:input) {
        history_[static_cast<size_t>(samples_%history_.size())]=x;
        ++samples_;
        while(atMs(nextMs_+910)<=samples_) {
            SstvVisEvent event;
            if(inspect(nextMs_,event)) {
                result.push_back(std::move(event));
                nextMs_+=910; // The same header cannot be emitted again on an overlapping search.
            } else ++nextMs_;
        }
    }
    return result;
}

SstvVisReport SstvVisDetector::counters() const {
    return {rate_,samples_,candidates_,parityRejected_,framingRejected_,{}};
}
