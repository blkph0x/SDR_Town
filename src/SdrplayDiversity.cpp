#include "SdrplayDiversity.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace SdrplayDiversity {

std::string modeName(Mode mode) {
    switch (mode) {
    case Mode::EqualGainSum: return "sum";
    case Mode::NullSteer: return "null";
    case Mode::Off:
    default: return "off";
    }
}

Mode modeFromName(const std::string& name) {
    if (name == "sum" || name == "equal" || name == "diversity") return Mode::EqualGainSum;
    if (name == "null" || name == "nullsteer" || name == "cancel") return Mode::NullSteer;
    return Mode::Off;
}

std::vector<std::complex<float>> applyPhaseAmp(
    std::span<const std::complex<float>> in,
    float phaseDeg,
    float amplitude)
{
    std::vector<std::complex<float>> out;
    out.resize(in.size());
    const float rad = phaseDeg * (std::numbers::pi_v<float> / 180.0f);
    const std::complex<float> scale = std::polar(std::max(0.0f, amplitude), rad);
    for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] * scale;
    return out;
}

std::vector<std::complex<float>> combine(
    std::span<const std::complex<float>> a,
    std::span<const std::complex<float>> b,
    const Config& config)
{
    if (config.mode == Mode::Off || a.empty() || b.empty()) return {};
    const size_t n = std::min(a.size(), b.size());
    std::vector<std::complex<float>> out(n);
    const float rad = config.phaseDeg * (std::numbers::pi_v<float> / 180.0f);
    const std::complex<float> scaleB = std::polar(std::max(0.0f, config.amplitudeB), rad);
    const bool nullMode = (config.mode == Mode::NullSteer);
    for (size_t i = 0; i < n; ++i) {
        const auto wb = b[i] * scaleB;
        out[i] = nullMode ? (a[i] - wb) : (a[i] + wb);
    }
    return out;
}

} // namespace SdrplayDiversity
