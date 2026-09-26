#pragma once
#include <algorithm>
#include <complex>
#include <vector>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

// DEC-0147: vectorize independent outputs, preserving tap accumulation order.
// Keep every full-rate output for unchanged downstream power/squelch semantics.
class WfmSpeechFir {
public:
    void reset() { history_.clear(); tapCount_=0; }
    void process(std::vector<std::complex<float>>& samples, const std::vector<float>& taps) {
        if(taps.empty() || samples.empty())return;
        const size_t delay=taps.size()-1;
        if(tapCount_!=taps.size()) {
            history_.assign(delay,{});tapCount_=taps.size();
        }
        input_.resize(delay+samples.size());
        std::copy(history_.begin(),history_.end(),input_.begin());
        std::copy(samples.begin(),samples.end(),input_.begin()+delay);
        std::copy(input_.end()-delay,input_.end(),history_.begin());
        size_t n=0;
#if defined(_M_X64) || defined(__SSE2__)
        // std::complex<float> guarantees interleaved real/imag float access.
        for(;n+4<=samples.size();n+=4) {
            __m128 a=_mm_setzero_ps(),b=_mm_setzero_ps();
            for(size_t k=0;k<taps.size();++k) {
                const float* p=reinterpret_cast<const float*>(input_.data()+delay+n-k);
                const auto t=_mm_set1_ps(taps[k]);
                a=_mm_add_ps(a,_mm_mul_ps(t,_mm_loadu_ps(p)));
                b=_mm_add_ps(b,_mm_mul_ps(t,_mm_loadu_ps(p+4)));
            }
            float* p=reinterpret_cast<float*>(samples.data()+n);
            _mm_storeu_ps(p,a);_mm_storeu_ps(p+4,b);
        }
#endif
        for(;n<samples.size();++n) {
            std::complex<float> sum{};
            for(size_t k=0;k<taps.size();++k)sum+=taps[k]*input_[delay+n-k];
            samples[n]=sum;
        }
    }
private:
    size_t tapCount_=0;
    std::vector<std::complex<float>> history_,input_;
};
