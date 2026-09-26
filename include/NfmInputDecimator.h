#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <span>
#include <vector>

// DEC-0148: causal FIR evaluated only at retained outputs; no new sample clock.
class NfmInputDecimator {
public:
    void reset() { std::fill(history_.begin(),history_.end(),std::complex<float>{});write_=phase_=0; }
    void configure(size_t factor) {
        factor=std::max(size_t(1),factor);
        if(factor==factor_)return;
        factor_=factor;
        const size_t half=8*factor_;
        const double cutoff=.25/factor_,beta=.1102*(80-8.7);
        taps_.resize(2*half+1);
        double sum=0;
        for(size_t i=0;i<taps_.size();++i) {
            const double m=double(i)-half,x=m/half;
            const double window=std::cyl_bessel_i(0,beta*std::sqrt(std::max(0.0,1-x*x)))/std::cyl_bessel_i(0,beta);
            const double sinc=m==0?2*cutoff:std::sin(2*std::numbers::pi*cutoff*m)/(std::numbers::pi*m);
            taps_[i]=float(window*sinc);sum+=taps_[i];
        }
        for(auto& tap:taps_)tap=float(tap/sum);
        history_.assign(taps_.size(),{});reset();
    }
    std::vector<std::complex<float>> process(std::span<const std::complex<float>> input) {
        std::vector<std::complex<float>> output;
        if(taps_.empty())return output;
        output.reserve(input.size()/factor_+1);
        for(const auto& s:input) {
            history_[write_]=s;
            if(++phase_==factor_) {
                phase_=0;std::complex<float> sum{};size_t at=write_;
                for(float tap:taps_) {sum+=tap*history_[at];at=at==0?history_.size()-1:at-1;}
                output.push_back(sum);
            }
            if(++write_==history_.size())write_=0;
        }
        return output;
    }
    const std::vector<float>& taps() const {return taps_;}
    size_t delaySamples() const {return taps_.empty()?0:(taps_.size()-1)/2;}
private:
    size_t factor_=0,phase_=0,write_=0;
    std::vector<float> taps_;
    std::vector<std::complex<float>> history_;
};
