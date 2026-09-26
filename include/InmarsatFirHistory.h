#pragma once
#include <array>
#include <complex>
#include <cstddef>

// DEC-0136: mirrored history removes per-tap modulo without changing FIR order.
// This is Aero-only; coefficients and decimation policy remain with the caller.
template<std::size_t N>
class InmarsatFirHistory {
    static_assert(N > 0);
public:
    void push(std::complex<float> value) {
        history_[cursor_]=history_[cursor_+N]=value;
        if(++cursor_==N)cursor_=0;
    }
    std::complex<float> filter(const std::array<double,N>& coefficients) const {
        std::complex<double> sum=0;
        const auto* ordered=history_.data()+cursor_;
        for(std::size_t i=0;i<N;++i)
            sum+=std::complex<double>(ordered[i])*coefficients[i];
        return std::complex<float>(sum);
    }
private:
    std::array<std::complex<float>,2*N> history_{};
    std::size_t cursor_=0;
};
