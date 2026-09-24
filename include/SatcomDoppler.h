#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// DEC-0114: translate tracked RF back to a stable nominal decoder frequency.
// Owned by the satellite IQ consumer, never by the GUI/planner thread.
class SatcomDoppler {
public:
    void reset() noexcept { oscillator_ = {1.0, 0.0}; samples_ = 0; }

    bool translate(std::vector<std::complex<float>>& iq, double sampleRate,
                   double centerHz, double trackedHz, double nominalHz,
                   double bandwidthHz) noexcept
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 ||
            !std::isfinite(centerHz) || !std::isfinite(trackedHz) ||
            !std::isfinite(nominalHz) || !std::isfinite(bandwidthHz) ||
            bandwidthHz <= 0.0 ||
            std::abs(trackedHz - centerHz) + bandwidthHz / 2.0 >= sampleRate / 2.0 ||
            std::abs(nominalHz - centerHz) + bandwidthHz / 2.0 >= sampleRate / 2.0)
            return false;

        constexpr double twoPi = 6.28318530717958647692;
        const double step = -twoPi * (trackedHz - nominalHz) / sampleRate;
        const std::complex<double> rotation(std::cos(step), std::sin(step));
        for (auto& sample : iq) {
            sample *= static_cast<std::complex<float>>(oscillator_);
            oscillator_ *= rotation;
            // Bound recurrence roundoff without callback-dependent phase resets.
            if ((++samples_ & 4095u) == 0) oscillator_ /= std::abs(oscillator_);
        }
        return true;
    }

private:
    std::complex<double> oscillator_{1.0, 0.0};
    uint64_t samples_ = 0;
};
