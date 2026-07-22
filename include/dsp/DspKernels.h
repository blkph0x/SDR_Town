#pragma once

#include <cstddef>

namespace p25dsp {

using FirDotFunction = float (*)(const float* samples, const float* taps, size_t count) noexcept;

struct DspKernels {
    FirDotFunction firDot = nullptr;
};

const DspKernels& dspKernels() noexcept;

float dotScalar(const float* samples, const float* taps, size_t count) noexcept;

#if defined(__AVX2__) && defined(__FMA__)
float dotAvx2(const float* samples, const float* taps, size_t count) noexcept;
#endif

} // namespace p25dsp
