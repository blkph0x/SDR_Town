#pragma once
#include <complex>
#include <vector>

// Remove the DC component from the display FFT's private sample copy.
// The shared receiver IQ ring and decoder samples are never modified.
inline void removeSpectrumDc(std::vector<std::complex<float>>& samples) {
    if (samples.empty()) return;
    std::complex<double> sum{};
    for (const auto sample : samples) sum += std::complex<double>(sample.real(), sample.imag());
    const auto mean = sum / static_cast<double>(samples.size());
    const std::complex<float> dc(static_cast<float>(mean.real()), static_cast<float>(mean.imag()));
    for (auto& sample : samples) sample -= dc;
}
