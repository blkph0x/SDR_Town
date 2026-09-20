#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

// RSPduo Dual Tuner spatial diversity / null-steering combiner (host DSP).
// Hardware supplies two phase-coherent IQ streams; this module applies relative
// amplitude and phase then sums (diversity) or subtracts (null-steer).

namespace SdrplayDiversity {

enum class Mode {
    Off = 0,
    EqualGainSum = 1,   // A + B * amp * e^(jθ)   — spatial diversity / coherent sum
    NullSteer = 2,      // A - B * amp * e^(jθ)   — null steering / interference cancel
};

struct Config {
    Mode mode = Mode::Off;
    float phaseDeg = 0.0f;   // relative phase applied to B (-180..180)
    float amplitudeB = 1.0f; // linear amplitude of B relative to A (0..4)
};

std::string modeName(Mode mode);
Mode modeFromName(const std::string& name);

// Combine equal-length IQ windows. Uses min(a,b) samples from the start.
// Returns empty if mode is Off or either input is empty.
std::vector<std::complex<float>> combine(
    std::span<const std::complex<float>> a,
    std::span<const std::complex<float>> b,
    const Config& config);

// Unit-test helper: apply a known phase/amp to a copy of `in`.
std::vector<std::complex<float>> applyPhaseAmp(
    std::span<const std::complex<float>> in,
    float phaseDeg,
    float amplitude);

} // namespace SdrplayDiversity
