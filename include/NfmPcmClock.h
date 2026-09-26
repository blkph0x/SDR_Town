#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// DEC-0143: existing cubic polynomial, now causal with two input samples of
// delay. Zero initial history is the filter initial condition, not gap filling.
// Four samples suffice: no callback-sized buffers or invented future samples.
class NfmPcmClock {
public:
    bool configure(double inputRate, double outputRate, bool reset) {
        if (!reset && inputRate==inputRate_ && outputRate==outputRate_)return false;
        inputRate_=inputRate;outputRate_=outputRate;
        input_=output_=0;history_={};return true;
    }
    std::vector<float> process(std::span<const float> samples) {
        std::vector<float> result;
        if(!std::isfinite(inputRate_) || !std::isfinite(outputRate_) ||
            inputRate_<=0 || outputRate_<=0)return result;
        const long double step=static_cast<long double>(inputRate_)/outputRate_;
        result.reserve(static_cast<size_t>(std::ceil(samples.size()/step))+1);
        for(float value:samples) {
            history_[0]=history_[1];history_[1]=history_[2];
            history_[2]=history_[3];history_[3]=value;
            ++input_;
            // The absolute output index makes partitioning irrelevant to phase.
            for(long double time=output_*step;time<input_;time=output_*step) {
                const float t=static_cast<float>(time-std::floor(time)),t2=t*t,t3=t2*t;
                const float ym1=history_[0],y0=history_[1],y1=history_[2],y2=history_[3];
                const float c1=.5f*(y1-ym1);
                const float c2=ym1-2.5f*y0+2*y1-.5f*y2;
                const float c3=.5f*(y2-ym1)+1.5f*(y0-y1);
                result.push_back(y0+c1*t+c2*t2+c3*t3);
                ++output_;
            }
        }
        return result;
    }
private:
    double inputRate_=0,outputRate_=0;
    uint64_t input_=0,output_=0;
    std::array<float,4> history_{};
};
