#include "CtcssDecoder.h"
#include "miniaudio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
// Classic 38 frequencies also tabulated by GNU Radio; see DEC-0081.
constexpr double frequencies[]{67,71.9,74.4,77,79.7,82.5,85.4,88.5,91.5,94.8,
    97.4,100,103.5,107.2,110.9,114.8,118.8,123,127.3,131.8,136.5,141.3,
    146.2,151.4,156.7,162.2,167.9,173.8,179.9,186.2,192.8,203.5,210.7,
    218.1,225.7,233.6,241.8,250.3};
double energy(std::span<const double> samples, double rate, double frequency) {
    const double coefficient=2*std::cos(2*std::numbers::pi*frequency/rate);
    double previous=0, previous2=0;
    for (double sample : samples) {
        const double current=sample+coefficient*previous-previous2;
        previous2=previous; previous=current;
    }
    return std::max(0.0,previous*previous+previous2*previous2-coefficient*previous*previous2);
}
}

std::span<const double> CtcssDecoder::tones() { return frequencies; }
CtcssSnapshot CtcssDecoder::snapshot() const { std::lock_guard lock(mutex_); return published_; }
void CtcssDecoder::publish() { std::lock_guard lock(mutex_); published_=state_; }
void CtcssDecoder::reset() {
    const auto resets=state_.resets;
    state_={}; state_.resets=resets; window_.clear(); lowpass_={};
    dc_=candidate_=0; matches_=0; length_=0; nextSample_=0; publish();
}

bool CtcssDecoder::process(std::span<const float> samples, double rate, double target,
    uint64_t epoch, uint64_t first, bool gap) {
    if (!std::isfinite(rate) || rate<8000 || rate>96000 || !std::isfinite(target) ||
        samples.size()>262144 || first>std::numeric_limits<uint64_t>::max()-samples.size() ||
        !std::all_of(samples.begin(),samples.end(),[](float x){return std::isfinite(x);})) {
        reset(); state_.status="Invalid tone input (8-96 kHz required)"; publish(); return false;
    }
    if (!length_ || gap || epoch!=epoch_ || first!=nextSample_ ||
        state_.sampleRate!=rate || state_.targetHz!=target) {
        reset(); ++state_.resets; epoch_=epoch;
        state_.sampleRate=rate; state_.targetHz=target; state_.status="Searching CTCSS";
        length_=static_cast<size_t>(std::llround(rate)); // DEC-0081: one-second analysis.
        window_.reserve(length_);
        alpha_=1-std::exp(-2*std::numbers::pi*300/rate);
        dcAlpha_=1-std::exp(-2*std::numbers::pi*20/rate);
    }
    for (float sample:samples) {
        dc_+=dcAlpha_*(sample-dc_);
        double filtered=sample-dc_;
        for (auto& pole:lowpass_) { pole+=alpha_*(filtered-pole); filtered=pole; }
        window_.push_back(filtered);
        if (window_.size()==length_) { evaluate(); window_.clear(); }
    }
    state_.samples+=samples.size(); nextSample_=first+samples.size();
    if (!samples.empty()) state_.updatedMs=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    publish(); return true;
}

void CtcssDecoder::evaluate() {
    ++state_.windows;
    double sumWeight=0, weightedEnergy=0;
    for (size_t i=0;i<window_.size();++i) {
        const double weight=.5-.5*std::cos(2*std::numbers::pi*i/(window_.size()-1));
        sumWeight+=weight; weightedEnergy+=weight*window_[i]*window_[i];
        window_[i]*=weight;
    }
    double best=0, runner=0, frequency=0;
    for (double tone:frequencies) {
        const double value=energy(window_,state_.sampleRate,tone);
        if (value>best) { runner=best; best=value; frequency=tone; }
        else runner=std::max(runner,value);
    }
    state_.purity=weightedEnergy>1e-15 ? std::clamp(2*best/(sumWeight*weightedEnergy),0.0,1.0) : 0;
    // DEC-0081 engineering profile; deliberately not a standards-compliance claim.
    const bool valid=state_.purity>=.65 && best>4*runner && frequency>0 &&
        best>energy(window_,state_.sampleRate,frequency*.99) &&
        best>energy(window_,state_.sampleRate,frequency*1.01);
    if (valid) {
        matches_=frequency==candidate_ ? std::min(2u,matches_+1) : 1;
        candidate_=frequency;
    } else { matches_=0; candidate_=0; }
    state_.frequencyHz=matches_>=2 ? candidate_ : 0;
    if (state_.frequencyHz>0) ++state_.confirmedWindows;
    state_.status=state_.frequencyHz>0 ? "CTCSS detected" : "Searching CTCSS";
}

CtcssSnapshot decodeCtcssFile(const std::string& path,size_t chunkSize) {
    if (!chunkSize || chunkSize>8192) throw std::invalid_argument("Tone chunk must be 1..8192 samples");
    ma_decoder reader{};
    auto config=ma_decoder_config_init(ma_format_f32,0,0);
    if (ma_decoder_init_file(path.c_str(),&config,&reader)!=MA_SUCCESS) throw std::runtime_error("Cannot open tone recording");
    struct Guard { ma_decoder* reader; ~Guard(){ma_decoder_uninit(reader);} } guard{&reader};
    if (reader.outputChannels!=1 || reader.outputSampleRate<8000 || reader.outputSampleRate>96000)
        throw std::runtime_error("Tone recording must be mono at 8..96 kHz");
    CtcssDecoder decoder; std::vector<float> samples(chunkSize); uint64_t first=0;
    for (;;) {
        ma_uint64 count=0;
        const auto result=ma_decoder_read_pcm_frames(&reader,samples.data(),chunkSize,&count);
        if (result!=MA_SUCCESS && result!=MA_AT_END) throw std::runtime_error("Tone recording read failed");
        if (!count) break;
        if (first+count>uint64_t{reader.outputSampleRate}*120) throw std::runtime_error("Tone recording exceeds 120 seconds");
        if (!decoder.process(std::span(samples.data(),static_cast<size_t>(count)),reader.outputSampleRate,0,1,first,first==0))
            throw std::runtime_error(decoder.snapshot().status);
        first+=count;
    }
    return decoder.snapshot();
}
