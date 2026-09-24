#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

struct SatcomSignalPeak { double frequencyHz; float powerDb; };

// A10: a display can show a wide slice, but only in-plan bins may acquire a lock.
inline std::optional<SatcomSignalPeak> selectSatcomSignalPeak(
    std::span<const float> power, double centerHz, double rateHz, double lowHz, double highHz) {
    if (power.empty() || !std::isfinite(centerHz) || !std::isfinite(rateHz) || rateHz <= 0 ||
        !std::isfinite(lowHz) || !std::isfinite(highHz) || lowHz > highHz) return {};
    std::optional<SatcomSignalPeak> result;
    const double binHz = rateHz / power.size();
    for (size_t i = 0; i < power.size(); ++i) {
        const double frequency = centerHz - rateHz * 0.5 + (i + 0.5) * binHz;
        if (frequency < lowHz || frequency > highHz || !std::isfinite(power[i])) continue;
        if (!result || power[i] > result->powerDb) result = {frequency, power[i]};
    }
    return result;
}
