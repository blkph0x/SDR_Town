#pragma once
#include <algorithm>
#include <complex>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

// DEC-0152 experiment only: no full-rate power estimate or application routing.
class WfmRetainedFirPrototype {
public:
    WfmRetainedFirPrototype(std::vector<float> taps, size_t factor)
        : taps_(std::move(taps)), factor_(factor) {
        if (taps_.empty() || !factor_) throw std::invalid_argument("FIR configuration");
        reset();
    }
    void reset() { history_.assign(taps_.size()-1, {}); phase_=0; }
    std::vector<std::complex<float>> process(std::span<const std::complex<float>> samples) {
        if (samples.empty()) return {};
        const size_t delay=history_.size();
        input_.resize(delay+samples.size());
        std::copy(history_.begin(),history_.end(),input_.begin());
        std::copy(samples.begin(),samples.end(),input_.begin()+delay);
        std::copy(input_.end()-delay,input_.end(),history_.begin());
        std::vector<std::complex<float>> result;
        result.reserve(samples.size()/factor_+1);
        for(size_t n=phase_?factor_-phase_:0;n<samples.size();n+=factor_) {
            std::complex<float> sum{};
            for(size_t k=0;k<taps_.size();++k) sum+=taps_[k]*input_[delay+n-k];
            result.push_back(sum);
        }
        phase_=(phase_+samples.size()%factor_)%factor_;
        return result;
    }
private:
    std::vector<float> taps_;
    size_t factor_,phase_=0;
    std::vector<std::complex<float>> history_,input_;
};
