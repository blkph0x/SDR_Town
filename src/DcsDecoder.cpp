#include "DcsDecoder.h"
#include "miniaudio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {
constexpr uint32_t mask = (1u<<23)-1;
// 105 payloads in the checked SDRTrunk DCSCode enumeration; DEC-0084.
constexpr uint16_t payloads[]{0023,0025,0026,0031,0032,0036,0043,0047,0051,0053,
0054,0065,0071,0072,0073,0074,0114,0115,0116,0122,0125,0131,0132,0134,0143,
0145,0152,0155,0156,0162,0165,0172,0174,0205,0212,0223,0225,0226,0243,0244,
0245,0246,0251,0252,0255,0261,0263,0265,0266,0271,0274,0306,0311,0315,0325,
0331,0332,0343,0346,0351,0356,0364,0365,0371,0411,0412,0413,0423,0431,0432,
0445,0446,0452,0454,0455,0462,0464,0465,0466,0503,0506,0516,0523,0526,0532,
0546,0565,0606,0612,0624,0627,0631,0632,0645,0654,0662,0664,0703,0712,0723,
0731,0732,0734,0743,0754};
uint32_t syndrome(uint32_t word) {
    // ETSI vectors / OP25 polynomial; independent long division.
    for (int bit=22;bit>=11;--bit) if (word & (1u<<bit)) word ^= 0xc75u<<(bit-11);
    return word;
}
const auto& codebook() {
    static const auto book=[] {
        std::unordered_map<uint32_t,std::vector<DcsIdentity>> result;
        for (auto code:payloads) for (bool invert:{false,true}) {
            uint32_t word=DcsBitDecoder::encode(code) ^ (invert ? mask : 0);
            for (int rotation=0;rotation<23;++rotation) {
                auto& entries=result[word]; const DcsIdentity id{code,invert};
                if (std::find(entries.begin(),entries.end(),id)==entries.end()) entries.push_back(id);
                word=(word>>1)|((word&1)<<22);
            }
        }
        return result;
    }();
    return book;
}
}

std::span<const uint16_t> DcsBitDecoder::codes() { return payloads; }
uint32_t DcsBitDecoder::encode(uint16_t code) {
    if (code>0777) throw std::invalid_argument("DCS payload exceeds nine bits");
    static const auto parity=[] {
        std::array<uint16_t,2048> result{};
        for (uint16_t p=0;p<2048;++p) result[syndrome(uint32_t(p)<<12)]=p;
        return result;
    }();
    const uint32_t data=0x800|code;
    return data|(uint32_t(parity[syndrome(data)])<<12);
}
std::vector<DcsIdentity> DcsBitDecoder::aliases(uint32_t word) {
    const auto& book=codebook(); const auto it=book.find(word);
    return it==book.end() ? std::vector<DcsIdentity>{} : it->second;
}
void DcsBitDecoder::reset() { *this=DcsBitDecoder{}; }
bool DcsBitDecoder::process(std::span<const uint8_t> input) {
    if (input.size()>262144 || bits_>std::numeric_limits<uint64_t>::max()-input.size() ||
        !std::all_of(input.begin(),input.end(),[](auto bit){return bit<=1;})) { reset(); return false; }
    for (auto bit:input) {
        word_=(word_>>1)|(uint32_t(bit)<<22); ++bits_;
        if (bits_>=23) {
            const auto index=bits_%23;
            const auto found=codebook().find(word_);
            if (found!=codebook().end()) {
                repeats_[index]=words_[index]==word_ ? std::min(3u,repeats_[index]+1) : 1;
                words_[index]=word_;
                if (repeats_[index]>=3) { identities_=found->second; lastConfirmed_=bits_; }
            } else repeats_[index]=0;
        }
        if (bits_-lastConfirmed_>=46) identities_.clear(); // DEC-0084 display expiry.
    }
    return true;
}

std::string dcsLabel(const DcsIdentity& id) {
    std::ostringstream text; text<<std::oct<<std::setw(3)<<std::setfill('0')<<id.code<<(id.inverted?'I':'N');
    return text.str();
}
DcsIdentity preferredDcsIdentity(const std::vector<DcsIdentity>& identities) {
    if (identities.empty()) return {};
    DcsIdentity best = identities.front();
    for (const auto& id : identities) {
        if (id.inverted != best.inverted) {
            if (!id.inverted) best = id;
            continue;
        }
        if (id.code < best.code) best = id;
    }
    return best;
}
void DcsDecoder::publish() { std::lock_guard lock(mutex_); published_=state_; }
DcsSnapshot DcsDecoder::snapshot() const { std::lock_guard lock(mutex_); return published_; }
void DcsDecoder::reset() {
    const auto resets=state_.resets; state_={}; state_.resets=resets;
    lanes_={}; lowpass_={}; dc_=0; next_=0;
    for (size_t i=0;i<lanes_.size();++i) lanes_[i].phase=double(i)/lanes_.size();
    publish();
}
bool DcsDecoder::process(std::span<const float> samples,double rate,double target,uint64_t epoch,uint64_t first,bool gap) {
    if (!std::isfinite(rate) || rate<8000 || rate>96000 || !std::isfinite(target) ||
        samples.size()>262144 || first>std::numeric_limits<uint64_t>::max()-samples.size() ||
        !std::all_of(samples.begin(),samples.end(),[](float v){return std::isfinite(v);})) {
        reset(); state_.status="Invalid DCS input (8-96 kHz required)"; publish(); return false;
    }
    if (state_.sampleRate==0 || gap || epoch_!=epoch || next_!=first || state_.sampleRate!=rate || state_.targetHz!=target) {
        reset(); ++state_.resets; state_.sampleRate=rate; state_.targetHz=target; epoch_=epoch;
        dcAlpha_=1-std::exp(-2*std::numbers::pi*2/rate);
        alpha_=1-std::exp(-2*std::numbers::pi*300/rate);
    }
    for (float sample:samples) {
        dc_+=dcAlpha_*(sample-dc_); double value=sample-dc_;
        for (auto& pole:lowpass_) { pole+=alpha_*(value-pole); value=pole; }
        for (auto& lane:lanes_) {
            lane.sum+=value; lane.phase+=134.4/rate;
            if (lane.phase>=1) {
                const uint8_t bit=lane.sum>=0; lane.decoder.process(std::span(&bit,1));
                lane.phase-=1; lane.sum=0;
            }
        }
    }
    unsigned best=0, runner=0; std::vector<DcsIdentity> winner;
    for (size_t i=0;i<lanes_.size();++i) {
        const auto& ids=lanes_[i].decoder.identities(); if (ids.empty() || ids==winner) continue;
        unsigned votes=0;
        for (const auto& lane:lanes_) if (lane.decoder.identities()==ids) ++votes;
        if (votes>best) { runner=best; best=votes; winner=ids; } else runner=std::max(runner,votes);
    }
    state_.agreeingPhases=best;
    state_.identities=best>=2 && best>runner ? winner : std::vector<DcsIdentity>{};
    state_.status=state_.identities.empty() ? "Searching DCS" : "DCS detected (equivalent labels)";
    next_=first+samples.size(); state_.samples+=samples.size();
    if (!samples.empty()) state_.updatedMs=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    publish(); return true;
}

DcsSnapshot decodeDcsFile(const std::string& path,size_t chunkSize) {
    if (!chunkSize || chunkSize>8192) throw std::invalid_argument("DCS chunk must be 1..8192 samples");
    ma_decoder reader{}; auto config=ma_decoder_config_init(ma_format_f32,0,0);
    if (ma_decoder_init_file(path.c_str(),&config,&reader)!=MA_SUCCESS) throw std::runtime_error("Cannot open DCS recording");
    struct Guard { ma_decoder* reader; ~Guard(){ma_decoder_uninit(reader);} } guard{&reader};
    if (reader.outputChannels!=1 || reader.outputSampleRate<8000 || reader.outputSampleRate>96000)
        throw std::runtime_error("DCS recording must be mono at 8..96 kHz");
    DcsDecoder decoder; std::vector<float> samples(chunkSize); uint64_t first=0;
    for (;;) {
        ma_uint64 count=0; const auto result=ma_decoder_read_pcm_frames(&reader,samples.data(),chunkSize,&count);
        if (result!=MA_SUCCESS && result!=MA_AT_END) throw std::runtime_error("DCS recording read failed");
        if (!count) break;
        if (first+count>uint64_t(reader.outputSampleRate)*120) throw std::runtime_error("DCS recording exceeds 120 seconds");
        if (!decoder.process(std::span(samples.data(),size_t(count)),reader.outputSampleRate,0,1,first,first==0))
            throw std::runtime_error(decoder.snapshot().status);
        first+=count;
    }
    return decoder.snapshot();
}
std::vector<DcsIdentity> decodeDcsBitsFile(const std::string& path) {
    std::ifstream file(path,std::ios::binary); if (!file) throw std::runtime_error("Cannot open DCS bit recording");
    std::vector<uint8_t> bits; char ch; size_t bytes=0;
    while (file.get(ch)) {
        if (++bytes>65536) throw std::runtime_error("DCS bit recording exceeds 64 KiB");
        if (ch=='0' || ch=='1') bits.push_back(uint8_t(ch-'0'));
        else if (ch!=' ' && ch!='\r' && ch!='\n' && ch!='\t') throw std::runtime_error("DCS bits require ASCII 0/1 and whitespace");
        if (bits.size()>16128) throw std::runtime_error("DCS bit recording exceeds 120 seconds");
    }
    if (file.bad()) throw std::runtime_error("DCS bit recording read failed");
    DcsBitDecoder decoder; decoder.process(bits); return decoder.identities();
}
