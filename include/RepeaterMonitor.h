#pragma once
#include <cmath>
#include <limits>

// Pure helpers for opt-in UHF repeater dual-watch (output listen + input control tap).

struct RepeaterPassbandPlan {
    bool feasible = false;
    double centerHz = 0;
    double halfBandwidthHz = 0;
    double marginHz = 0;
    const char* reason = "disabled";
};

inline RepeaterPassbandPlan planRepeaterDualWatch(double outputHz, double inputHz,
                                                  double sampleRateHz, double channelBwHz)
{
    RepeaterPassbandPlan plan;
    if (!std::isfinite(outputHz) || !std::isfinite(inputHz) || !std::isfinite(sampleRateHz) ||
        !std::isfinite(channelBwHz) || outputHz <= 0 || inputHz <= 0 || sampleRateHz < 200e3 ||
        channelBwHz <= 0) {
        plan.reason = "invalid parameters";
        return plan;
    }
    if (std::abs(outputHz - inputHz) < 1.0) {
        plan.reason = "output and input are the same frequency";
        return plan;
    }
    const double span = std::abs(outputHz - inputHz) + channelBwHz;
    // Keep both channels inside ~90% of Nyquist to avoid edge roll-off / aliasing.
    const double usable = sampleRateHz * 0.90;
    plan.halfBandwidthHz = sampleRateHz * 0.5;
    plan.marginHz = usable - span;
    plan.centerHz = 0.5 * (outputHz + inputHz);
    if (span > usable) {
        plan.reason = "sample rate too narrow for output+input pair";
        return plan;
    }
    const double halfNeed = 0.5 * span;
    if (std::abs(outputHz - plan.centerHz) + 0.5 * channelBwHz > plan.halfBandwidthHz * 0.95 ||
        std::abs(inputHz - plan.centerHz) + 0.5 * channelBwHz > plan.halfBandwidthHz * 0.95) {
        plan.reason = "pair does not fit RF passband around midpoint";
        return plan;
    }
    (void)halfNeed;
    plan.feasible = true;
    plan.reason = "ok";
    return plan;
}

inline bool frequencyInPassband(double freqHz, double centerHz, double sampleRateHz,
                                double channelBwHz)
{
    if (!std::isfinite(freqHz) || !std::isfinite(centerHz) || !std::isfinite(sampleRateHz))
        return false;
    const double edge = std::abs(freqHz - centerHz) + 0.5 * std::max(0.0, channelBwHz);
    return edge <= sampleRateHz * 0.45;
}
