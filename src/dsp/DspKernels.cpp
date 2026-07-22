#include "dsp/DspKernels.h"

#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace p25dsp {

float dotScalar(const float* samples, const float* taps, size_t count) noexcept
{
    float sum = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        sum += samples[i] * taps[i];
    }
    return sum;
}

#if defined(__AVX2__) && defined(__FMA__)
float dotAvx2(const float* samples, const float* taps, size_t count) noexcept
{
    __m256 sum = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m256 x = _mm256_loadu_ps(samples + i);
        const __m256 h = _mm256_loadu_ps(taps + i);
        sum = _mm256_fmadd_ps(x, h, sum);
    }
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, sum);
    float result = lanes[0] + lanes[1] + lanes[2] + lanes[3] +
                   lanes[4] + lanes[5] + lanes[6] + lanes[7];
    for (; i < count; ++i) {
        result += samples[i] * taps[i];
    }
    return result;
}
#endif

const DspKernels& dspKernels() noexcept
{
    static const DspKernels kKernels = []() {
        DspKernels kernels;
#if defined(__AVX2__) && defined(__FMA__)
        kernels.firDot = dotAvx2;
#else
        kernels.firDot = dotScalar;
#endif
        return kernels;
    }();
    return kKernels;
}

} // namespace p25dsp
