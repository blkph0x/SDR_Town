#include "SstvRfRouter.h"
#include "SstvRateConverter.h"
#include "SstvVis.h"
#include "SstvModes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>

namespace {
constexpr uint64_t rate = 48000;
constexpr uint64_t headerSamples = rate * 910 / 1000; // Classic VIS, DEC-0091.
constexpr size_t acquisitionLimit = 2 * headerSamples + rate / 10; // DEC-0117.
constexpr size_t historyLimit = acquisitionLimit + rate / 10; // Tail of the selecting IQ block.
constexpr std::array<std::string_view,4> names{"USB","LSB","NFM","AM"};
constexpr std::array<DemodMode,4> modes{DemodMode::USB,DemodMode::LSB,DemodMode::NFM,DemodMode::AM};
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}

struct SstvRfRouter::State {
    struct Candidate {
        Demodulator demod;
        SstvRateConverter converter;
        SstvVisDetector vis{rate};
        std::deque<float> audio;
        double inputRate = 0;
        uint64_t samples = 0, beginning = 0;
        bool confirmed = false, tapIdentity = false;
        uint64_t tapEpoch = 0, tapNext = 0;
    };
    std::array<Candidate,4> candidates;
    double sr, center, target;
    uint64_t source, epoch = 0, next = 0, decisionAt = 0;
    bool identity = false;
    int selected = -1;
    bool automatic;
};

bool SstvRfRouter::supports(std::string_view mode) {
    return mode == "auto" || std::find(names.begin(),names.end(),mode) != names.end();
}
SstvRfRouter::SstvRfRouter(double sr,double center,double target,std::string_view mode,uint64_t source)
    : state_(std::make_unique<State>()) {
    require(supports(mode),"Unsupported SSTV RF mode");
    require(std::isfinite(sr) && sr >= 16000 && sr <= 10000000 &&
            std::isfinite(center) && center > 0 && std::isfinite(target) && target > 0,
            "Invalid SSTV RF source (supported IQ rates: 16 kHz..10 MHz)");
    const double halfWidth = mode == "USB" || mode == "LSB" ? 3000 : mode == "AM" ? 6000 : 7500;
    require(std::abs(target-center)+halfWidth < sr/2,"SSTV channel is outside the captured bandwidth");
    auto& s=*state_; s.sr=sr; s.center=center; s.target=target; s.source=source;
    s.automatic=mode=="auto";
    if(!s.automatic) s.selected=int(std::find(names.begin(),names.end(),mode)-names.begin());
}
SstvRfRouter::~SstvRfRouter()=default;
size_t SstvRfRouter::maxInputSamples() const {
    // 100 ms of IQ or the existing device-reader block cap, whichever is smaller.
    return std::min(size_t{65536},size_t(state_->sr/10));
}
std::string_view SstvRfRouter::selectedMode() const {
    return state_->selected<0 ? "searching" : names[size_t(state_->selected)];
}
void SstvRfRouter::process(const std::vector<std::complex<float>>& iq,uint64_t first,uint64_t epoch,bool gap) {
    auto& s=*state_;
    require(!gap,"SSTV IQ discontinuity; restart reception");
    require(iq.size()<=maxInputSamples() && first<=std::numeric_limits<uint64_t>::max()-iq.size(),"Invalid SSTV IQ block size");
    require(std::all_of(iq.begin(),iq.end(),[](auto v){return std::isfinite(v.real())&&std::isfinite(v.imag());}),"Non-finite SSTV IQ");
    if(iq.empty()) return;
    require(!s.identity || (epoch==s.epoch && first==s.next),"SSTV IQ source/position changed; restart reception");
    s.identity=true; s.epoch=epoch; s.next=first+iq.size();
    if(s.selected>=0) require(s.candidates[size_t(s.selected)].audio.empty(),"Drain SSTV audio before reading more IQ");
    // 10 ms is the shortest VIS element. Bound intermediate demod/resampler storage.
    const size_t chunk=std::max(size_t{1},size_t(s.sr/100));
    for(size_t offset=0;offset<iq.size();offset+=chunk) {
        const size_t count=std::min(chunk,iq.size()-offset);
        std::vector<std::complex<float>> part(iq.begin()+offset,iq.begin()+offset+count);
        for(size_t i=0;i<modes.size();++i) {
            if(s.selected>=0 ? int(i)!=s.selected : i==3) continue;
            auto& c=s.candidates[i];
            FmMultiplexBlock tap; double rms=0;
            c.demod.demodulateToAudio(part,s.sr,s.center,s.target,modes[i],rms,
                // HfDemod's BW argument is twice the one-sided audio passband.
                3000,-120,1,0,0.96,i==2?15000:i==3?12000:6000,0,48000,
                std::numeric_limits<double>::quiet_NaN(),false,&tap);
            if(tap.samples.empty()) continue;
            require(!c.tapIdentity || (!tap.discontinuity && tap.epoch==c.tapEpoch && tap.firstSample==c.tapNext),
                    "SSTV RF demodulator discontinuity; restart reception");
            c.tapIdentity=true; c.tapEpoch=tap.epoch; c.tapNext=tap.firstSample+tap.samples.size();
            if(!c.inputRate) {c.converter.start(tap.sampleRate); c.inputRate=tap.sampleRate;}
            require(c.inputRate==tap.sampleRate,"SSTV demodulator rate changed");
            const auto audio=c.converter.process(tap.samples);
            require(audio.size()<=SstvInputEvent::maxSamples,"SSTV RF output block exceeds budget");
            c.audio.insert(c.audio.end(),audio.begin(),audio.end()); c.samples+=audio.size();
            if(s.selected<0) {
                for(const auto& event:c.vis.process(audio)) {
                    if(!sstvModeByVis(event.code)) continue;
                    c.confirmed=true;
                    if(!s.decisionAt) s.decisionAt=event.headerEndSample+headerSamples;
                }
                while(c.audio.size()>acquisitionLimit) {c.audio.pop_front(); ++c.beginning;}
            }
            require(c.audio.size()<=historyLimit,"SSTV RF audio buffer limit exceeded");
        }
        if(s.selected<0 && s.decisionAt &&
           std::all_of(s.candidates.begin(),s.candidates.begin()+3,[&](const auto& c){return c.samples>=s.decisionAt;})) {
            int winner=-1;
            for(int i=0;i<3;++i) if(s.candidates[size_t(i)].confirmed) {
                require(winner<0,"Ambiguous SSTV RF header; select USB, LSB, NFM or AM manually");
                winner=i;
            }
            s.selected=winner;
            for(int i=0;i<4;++i) if(i!=winner) s.candidates[size_t(i)].audio.clear();
        }
    }
}
std::optional<SstvInputEvent> SstvRfRouter::pop() {
    auto& s=*state_;
    if(s.selected<0) return {};
    auto& c=s.candidates[size_t(s.selected)];
    if(c.audio.empty()) return {};
    SstvInputEvent out;
    out.sourceId=s.source; out.epoch=s.epoch; out.generation=1;
    out.sampleRate=rate; out.targetHz=s.target; out.firstSample=c.beginning;
    out.count=std::min(c.audio.size(),SstvInputEvent::maxSamples);
    for(size_t i=0;i<out.count;++i) {out.samples[i]=c.audio.front(); c.audio.pop_front();}
    c.beginning+=out.count;
    return out;
}
