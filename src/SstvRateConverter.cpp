#include "SstvRateConverter.h"
#include "SstvLiveInput.h"
#include "miniaudio.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

struct SstvRateConverter::State {
    ma_resampler resampler{};
    bool initialized=false,ready=false,bypass=false;
    double rate=0;
    ~State() {if(initialized) ma_resampler_uninit(&resampler,nullptr);}
};
SstvRateConverter::SstvRateConverter():state_(std::make_unique<State>()) {}
SstvRateConverter::~SstvRateConverter()=default;
void SstvRateConverter::reset() {state_=std::make_unique<State>();}
void SstvRateConverter::start(double inputRate) {
    reset();
    if(!std::isfinite(inputRate) || inputRate<8000 || inputRate>96000)
        throw std::invalid_argument("SSTV input rate must be 8..96 kHz");
    constexpr unsigned scale=10000; // DEC-0097: <=0.108 output sample/session rounding error.
    const auto scaled=static_cast<ma_uint32>(std::llround(inputRate*scale));
    state_->rate=double(scaled)/scale;
    state_->bypass=inputRate==outputRate;
    if(!state_->bypass) {
        const auto config=ma_resampler_config_init(ma_format_f32,1,scaled,outputRate*scale,ma_resample_algorithm_linear);
        if(ma_resampler_init(&config,nullptr,&state_->resampler)!=MA_SUCCESS)
            throw std::runtime_error("SSTV resampler initialization failed");
        state_->initialized=true;
    }
    state_->ready=true;
}
double SstvRateConverter::effectiveInputRate() const {return state_->ready?state_->rate:0;}
std::vector<float> SstvRateConverter::process(std::span<const float> samples) {
    if(!state_->ready) throw std::logic_error("SSTV resampler needs a new stream");
    if(samples.size()>SstvInputEvent::maxSamples ||
       !std::all_of(samples.begin(),samples.end(),[](float value){return std::isfinite(value);})) {
        reset(); throw std::invalid_argument("Invalid SSTV resampler samples");
    }
    if(samples.empty()) return {};
    if(state_->bypass) return {samples.begin(),samples.end()};
    // Maximum 6x expansion at 8 kHz, plus one input interval of carried phase.
    std::vector<float> result((samples.size()+1)*6);
    ma_uint64 input=samples.size(),output=result.size();
    if(ma_resampler_process_pcm_frames(&state_->resampler,samples.data(),&input,result.data(),&output)!=MA_SUCCESS ||
       input!=samples.size() || output>result.size()) {
        reset(); throw std::runtime_error("SSTV resampler did not consume its bounded input");
    }
    if(!std::all_of(result.begin(),result.begin()+size_t(output),[](float value){return std::isfinite(value);})) {
        reset(); throw std::runtime_error("Non-finite SSTV resampler output");
    }
    result.resize(size_t(output));
    return result;
}
