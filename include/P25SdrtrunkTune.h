#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Purpose: Tuner LO for P25 Phase 2 follows, copied from SDRTrunk
//          CenterFrequencyCalculator + R8xEmbeddedTuner DC/usable constants.
// Spec:    docs/DECISIONS.md DEC-0015 / DEC-0016
//          https://github.com/DSheirer/sdrtrunk
//            source/tuner/manager/CenterFrequencyCalculator.java
//            module/decode/p25/phase2/DecodeConfigP25Phase2.java
//            source/tuner/rtl/r8x/R8xEmbeddedTuner.java
//            source/tuner/TunerController.java isTunedFor
// Invariants: no DSP, no I/O. 250 kHz "low-IF" is not a valid substitute.

// DecodeConfigP25Phase2.getChannelSpecification():
//   new ChannelSpecification(50000.0, 12500, 6500.0, 7200.0)
// Second argument is the tuner-channel bandwidth in Hz.
inline constexpr double kP25SdrtrunkChannelBandwidthHz = 12500.0;

// R8xEmbeddedTuner (R820T / R820T2): DC_SPIKE_AVOID_BUFFER.
// TunerController.getMiddleUnusableHalfBandwidth() returns this value.
inline constexpr double kP25SdrtrunkDcSpikeHalfBandwidthHz = 5000.0;

// R8xEmbeddedTuner: USABLE_BANDWIDTH_PERCENT.
// TunerController.getUsableBandwidth() = sampleRate * this.
inline constexpr double kP25SdrtrunkUsableBandwidthPercent = 0.98;

inline constexpr double kP25SdrtrunkInvalidCenterHz = 0.0;

inline double p25SdrtrunkUsableBandwidthHz(double sampleRateHz) noexcept
{
    const double sr = (std::isfinite(sampleRateHz) && sampleRateHz > 0.0)
        ? sampleRateHz
        : 2.048e6;
    return sr * kP25SdrtrunkUsableBandwidthPercent;
}

inline double p25SdrtrunkChannelMinHz(double channelHz) noexcept
{
    return channelHz - kP25SdrtrunkChannelBandwidthHz * 0.5;
}

inline double p25SdrtrunkChannelMaxHz(double channelHz) noexcept
{
    return channelHz + kP25SdrtrunkChannelBandwidthHz * 0.5;
}

// TunerChannel.overlaps is inclusive. CenterFrequencyCalculator's
// single-channel park is minFrequency − dcHalf + 1, so channel min sits
// 1 Hz inside maxAvoid. Treat that designed graze as not overlapping;
// a channel actually in the DC spike overlaps by kilohertz.
inline bool p25SdrtrunkChannelOverlapsRange(double channelHz,
                                            double minAvoidHz,
                                            double maxAvoidHz) noexcept
{
    const double cmin = p25SdrtrunkChannelMinHz(channelHz);
    const double cmax = p25SdrtrunkChannelMaxHz(channelHz);
    const double overlapHz = std::min(cmax, maxAvoidHz) - std::max(cmin, minAvoidHz);
    return overlapHz > 1.0;
}

inline std::size_t p25SdrtrunkCollectChannels(double aHz, double bHz,
                                              std::array<double, 2>& out) noexcept
{
    std::size_t n = 0;
    if (std::isfinite(aHz) && aHz > 0.0) {
        out[n++] = aHz;
    }
    if (std::isfinite(bHz) && bHz > 0.0) {
        if (n == 0 || std::abs(bHz - out[0]) > 1.0) {
            out[n++] = bHz;
        }
    }
    if (n == 2 && p25SdrtrunkChannelMinHz(out[1]) < p25SdrtrunkChannelMinHz(out[0])) {
        std::swap(out[0], out[1]);
    }
    return n;
}

// TunerController.isTunedFor: every channel inside center ± usable/2,
// and none overlapping the DC hole.
inline bool p25SdrtrunkTunerIsTunedFor(double centerHz,
                                       double sampleRateHz,
                                       double aHz,
                                       double bHz = 0.0) noexcept
{
    if (!std::isfinite(centerHz) || centerHz <= 0.0) return false;
    std::array<double, 2> ch{};
    const std::size_t n = p25SdrtrunkCollectChannels(aHz, bHz, ch);
    if (n == 0) return false;
    const double usableHalf = p25SdrtrunkUsableBandwidthHz(sampleRateHz) * 0.5;
    const double minTuned = centerHz - usableHalf;
    const double maxTuned = centerHz + usableHalf;
    const double minAvoid = centerHz - kP25SdrtrunkDcSpikeHalfBandwidthHz;
    const double maxAvoid = centerHz + kP25SdrtrunkDcSpikeHalfBandwidthHz;
    for (std::size_t i = 0; i < n; ++i) {
        if (p25SdrtrunkChannelMinHz(ch[i]) < minTuned) return false;
        if (p25SdrtrunkChannelMaxHz(ch[i]) > maxTuned) return false;
        if (p25SdrtrunkChannelOverlapsRange(ch[i], minAvoid, maxAvoid)) return false;
    }
    return true;
}

// CenterFrequencyCalculator.getCenterFrequency. Returns 0 if no solution.
inline double p25SdrtrunkGetCenterFrequencyHz(double sampleRateHz,
                                              double aHz,
                                              double bHz = 0.0) noexcept
{
    std::array<double, 2> ch{};
    const std::size_t n = p25SdrtrunkCollectChannels(aHz, bHz, ch);
    if (n == 0) return kP25SdrtrunkInvalidCenterHz;

    const double dcHalf = kP25SdrtrunkDcSpikeHalfBandwidthHz;
    const double usableBw = p25SdrtrunkUsableBandwidthHz(sampleRateHz);
    const double usableHalf = usableBw * 0.5;

    if (n == 1) {
        // Single channel: sit just to the right of the DC hole.
        return p25SdrtrunkChannelMinHz(ch[0]) - dcHalf + 1.0;
    }

    const double minChannel = p25SdrtrunkChannelMinHz(ch[0]);
    const double maxChannel = p25SdrtrunkChannelMaxHz(ch[1]);
    double candidate = maxChannel - usableHalf;
    bool valid = true;

    if (maxChannel - minChannel <= usableHalf) {
        candidate = minChannel - dcHalf;
        return candidate;
    }

    if (dcHalf > 0.0) {
        bool processingRequired = true;
        int guard = 0;
        while (valid && processingRequired && guard++ < 16) {
            processingRequired = false;
            const double minAvoid = candidate - dcHalf;
            const double maxAvoid = candidate + dcHalf;
            for (std::size_t i = 0; i < n; ++i) {
                if (!p25SdrtrunkChannelOverlapsRange(ch[i], minAvoid, maxAvoid)) {
                    continue;
                }
                const double adjustment = p25SdrtrunkChannelMaxHz(ch[i]) - minAvoid + 1.0;
                if (candidate + adjustment - usableHalf <= minChannel) {
                    candidate += adjustment;
                    processingRequired = true;
                } else {
                    valid = false;
                }
                break;
            }
        }
    }

    return valid ? candidate : kP25SdrtrunkInvalidCenterHz;
}

// CenterFrequencyCalculator.canTune for at most two P25 channels.
inline bool p25SdrtrunkCanTune(double sampleRateHz,
                               double aHz,
                               double bHz = 0.0) noexcept
{
    std::array<double, 2> ch{};
    const std::size_t n = p25SdrtrunkCollectChannels(aHz, bHz, ch);
    if (n == 0) return false;
    if (n == 1) return true;
    const double span = p25SdrtrunkChannelMaxHz(ch[1]) - p25SdrtrunkChannelMinHz(ch[0]);
    if (span > p25SdrtrunkUsableBandwidthHz(sampleRateHz)) return false;
    return p25SdrtrunkGetCenterFrequencyHz(sampleRateHz, aHz, bHz) != kP25SdrtrunkInvalidCenterHz;
}

// Follow LO: always the single-channel park for the granted voice
// (DEC-0016). companionHz is accepted for call-site compatibility but
// must not pull the LO toward the CC+voice *set* — that put 421.975 MHz
// at 997 kHz offset on capture 20260907_115315. Keep CC only when
// isTunedFor(this center, {voice, companion}).
inline double p25SdrtrunkTunerCenterHz(double voiceHz,
                                       double sampleRateHz,
                                       double companionHz = 0.0) noexcept
{
    (void)companionHz;
    if (!std::isfinite(voiceHz) || voiceHz <= 0.0) return voiceHz;
    return p25SdrtrunkGetCenterFrequencyHz(sampleRateHz, voiceHz, 0.0);
}
