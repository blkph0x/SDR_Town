#include "InmarsatPipeline.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

void InmarsatPipeline::process(const std::complex<float>* iq, size_t count,
    uint64_t start, double rate, double center, double channel,
    InmarsatDemodMode mode, bool discontinuity) {
    if (!iq || count == 0) return;
    if (!std::isfinite(rate) || rate < 8000 || rate > 40e6 ||
        !std::isfinite(center) || !std::isfinite(channel) || center <= 0 || channel <= 0 ||
        std::abs(channel - center) >= rate / 2)
        throw std::runtime_error("Inmarsat channel is outside the recorded IQ passband or rates are invalid");
    const auto begin = std::chrono::steady_clock::now();
    // Validate the whole block before modifying state: NaN must not poison PLL history.
    double power = 0, peak = 0;
    for (size_t i = 0; i < count; ++i) {
        const double re = iq[i].real(), im = iq[i].imag();
        if (!std::isfinite(re) || !std::isfinite(im) || std::abs(re) > 1e6 || std::abs(im) > 1e6)
            throw std::runtime_error("Invalid or unbounded Inmarsat IQ amplitude");
        power += re * re + im * im;
        peak = std::max({peak, std::abs(re), std::abs(im)});
    }
    const bool gap = started_ && (discontinuity || start != nextSample_);
    if (!started_ || gap || rate_ != rate || center_ != center || channel_ != channel || mode_ != mode) {
        demod_.reset(mode, rate, channel - center);
        ++resets_;
        if (gap) ++gaps_;
    }
    started_ = true;
    rate_ = rate; center_ = center; channel_ = channel; mode_ = mode;
    const auto before = demod_.stats();
    demod_.process(iq, count);
    const auto after = demod_.stats();
    symbols_ += after.symbolsOut - before.symbolsOut;
    rawBlocks_ += after.rawBlocksOut - before.rawBlocksOut;
    nextSample_ = start + count;
    samples_ += count;
    ++blocks_;
    peak_ = std::max(peak_, peak);
    sumPower_ += power;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    totalMs_ += ms;
    maxMs_ = std::max(maxMs_, ms);
}

nlohmann::json InmarsatPipeline::report() const {
    const auto s = demod_.stats();
    return {{"samples", samples_}, {"blocks", blocks_}, {"nextSample", nextSample_},
        {"resets", resets_}, {"discontinuities", gaps_}, {"rateHz", rate_},
        {"centerHz", center_}, {"channelHz", channel_}, {"offsetHz", channel_ - center_},
        {"mode", static_cast<int>(mode_)}, {"symbols", symbols_}, {"rawBlocks", rawBlocks_},
        {"carrierDetected", s.carrierDetected}, {"quality", s.quality}, {"carrierOffsetHz", s.freqOffsetHz},
        {"processingMs", totalMs_}, {"maxBlockMs", maxMs_}, {"peakComponent", peak_},
        {"rms", samples_ ? std::sqrt(sumPower_ / samples_) : 0},
        {"protocolLock", false}, {"validatedFrames", 0}, {"voiceFrames", 0},
        {"pcmSamples", 0}, {"protocolDecoderAvailable", false}, {"aeroVocoderAvailable", false},
        {"capability", "physical_probe_only"}};
}
