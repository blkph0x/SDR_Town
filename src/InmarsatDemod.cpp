#include "InmarsatDemod.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

void wrapPhase(double& phase) {
    while (phase > std::numbers::pi_v<double>) phase -= kTwoPi;
    while (phase < -std::numbers::pi_v<double>) phase += kTwoPi;
}

std::complex<float> rotateBy(const std::complex<float>& sample, double phase) {
    return sample * std::complex<float>(
        static_cast<float>(std::cos(phase)),
        static_cast<float>(std::sin(phase)));
}
} // namespace

InmarsatDemodMode InmarsatDemod::modeFromBaud(int baud, bool egc) {
    if (egc) return InmarsatDemodMode::EgcBpsk1200;
    if (baud <= 600) return InmarsatDemodMode::AeroMsk600;
    if (baud <= 1200) return InmarsatDemodMode::AeroMsk1200;
    if (baud <= 9000) return InmarsatDemodMode::AeroVoice8400;
    return InmarsatDemodMode::AeroOqpsk10500;
}

double InmarsatDemod::symbolRate(InmarsatDemodMode mode) {
    switch (mode) {
    case InmarsatDemodMode::AeroMsk600: return 600.0;
    case InmarsatDemodMode::AeroMsk1200: return 1200.0;
    case InmarsatDemodMode::AeroVoice8400: return 8400.0;
    case InmarsatDemodMode::EgcBpsk1200: return 1200.0;
    case InmarsatDemodMode::AeroOqpsk10500:
    default: return 10500.0;
    }
}

void InmarsatDemod::configureRates() {
    const double rate = symbolRate(mode_);
    const double targetSamplesPerSymbol = 8.0;
    decimation_ = std::max(1, static_cast<int>(
        std::floor(sampleRateHz_ / (rate * targetSamplesPerSymbol))));
    basebandRateHz_ = sampleRateHz_ / static_cast<double>(decimation_);
    if (!std::isfinite(basebandRateHz_) || basebandRateHz_ <= 0.0)
        basebandRateHz_ = sampleRateHz_;
}

void InmarsatDemod::clearRawAssembler() {
    packedByte_ = 0;
    packedBitCount_ = 0;
    rawBlock_.clear();
}

void InmarsatDemod::reset(InmarsatDemodMode mode, double sampleRateHz,
                          double channelOffsetHz) {
    mode_ = mode;
    sampleRateHz_ = std::isfinite(sampleRateHz) && sampleRateHz > 0.0
        ? sampleRateHz
        : 2.048e6;
    channelOffsetHz_ = std::isfinite(channelOffsetHz) ? channelOffsetHz : 0.0;
    ncoPhase_ = 0.0;
    costasPhase_ = 0.0;
    costasFreq_ = 0.0;
    symbolPhase_ = 0.0;
    decimationPhase_ = 0;
    lowpassState_ = {0.f, 0.f};
    prevSample_ = {1.f, 0.f};
    symbolAccumulator_ = {0.0, 0.0};
    differentialAccumulator_ = 0.0;
    symbolAccumulatorCount_ = 0;
    momentAverage_ = {0.0, 0.0};
    signalPowerAverage_ = 0.0;
    carrierGoodSymbols_ = 0;
    carrierBadSymbols_ = 0;
    stats_ = {};
    clearRawAssembler();
    rawBlock_.reserve(64);
    configureRates();
}

void InmarsatDemod::setMode(InmarsatDemodMode mode) {
    if (mode == mode_) return;
    reset(mode, sampleRateHz_, channelOffsetHz_);
}

void InmarsatDemod::setChannelOffset(double offsetHz) {
    if (!std::isfinite(offsetHz)) return;
    if (std::abs(offsetHz - channelOffsetHz_) < 1e-9) return;
    channelOffsetHz_ = offsetHz;
    ncoPhase_ = 0.0;
    costasPhase_ = 0.0;
    costasFreq_ = 0.0;
    symbolPhase_ = 0.0;
    decimationPhase_ = 0;
    lowpassState_ = {0.f, 0.f};
    prevSample_ = {1.f, 0.f};
    symbolAccumulator_ = {0.0, 0.0};
    differentialAccumulator_ = 0.0;
    symbolAccumulatorCount_ = 0;
    momentAverage_ = {0.0, 0.0};
    signalPowerAverage_ = 0.0;
    carrierGoodSymbols_ = 0;
    carrierBadSymbols_ = 0;
    stats_ = {};
    clearRawAssembler();
}

void InmarsatDemod::ddcAndDecimate(const std::complex<float>* iq, size_t n,
                                   std::vector<std::complex<float>>& out) {
    out.clear();
    if (!iq || n == 0 || sampleRateHz_ <= 0.0) return;
    out.reserve(n / static_cast<size_t>(std::max(1, decimation_)) + 1);

    const double ncoIncrement = -kTwoPi * channelOffsetHz_ / sampleRateHz_;
    const double normalizedCutoff = std::clamp(
        symbolRate(mode_) * 1.35 / sampleRateHz_, 1.0e-6, 0.40);
    const float alpha = static_cast<float>(
        1.0 - std::exp(-kTwoPi * normalizedCutoff));

    for (size_t i = 0; i < n; ++i) {
        const std::complex<float> mixed = rotateBy(iq[i], ncoPhase_);
        ncoPhase_ += ncoIncrement;
        wrapPhase(ncoPhase_);

        lowpassState_ += alpha * (mixed - lowpassState_);
        if (decimationPhase_ == 0) out.push_back(lowpassState_);
        ++decimationPhase_;
        if (decimationPhase_ >= decimation_) decimationPhase_ = 0;
    }
}

void InmarsatDemod::updateCarrierQuality(const std::complex<float>& symbol,
                                         int momentOrder) {
    const double power = static_cast<double>(std::norm(symbol));
    constexpr double averageAlpha = 0.025;
    signalPowerAverage_ = (1.0 - averageAlpha) * signalPowerAverage_ +
                          averageAlpha * std::max(0.0, power);

    std::complex<double> moment{0.0, 0.0};
    const double magnitude = std::abs(symbol);
    if (std::isfinite(magnitude) && magnitude > 1.0e-8) {
        const std::complex<double> unit{
            static_cast<double>(symbol.real()) / magnitude,
            static_cast<double>(symbol.imag()) / magnitude};
        moment = {1.0, 0.0};
        for (int i = 0; i < std::max(1, momentOrder); ++i) moment *= unit;
    }
    momentAverage_ = (1.0 - averageAlpha) * momentAverage_ +
                     averageAlpha * moment;
    stats_.quality = std::clamp(std::abs(momentAverage_), 0.0, 1.0);
    stats_.ebnoDb = 0.0; // do not advertise an unvalidated Eb/N0 estimate

    const bool coherent = signalPowerAverage_ > 1.0e-10 && stats_.quality >= 0.62;
    const bool incoherent = signalPowerAverage_ <= 1.0e-12 || stats_.quality <= 0.35;
    if (coherent) {
        carrierGoodSymbols_ = std::min<uint32_t>(carrierGoodSymbols_ + 1, 1000000);
        carrierBadSymbols_ = 0;
    } else if (incoherent) {
        carrierBadSymbols_ = std::min<uint32_t>(carrierBadSymbols_ + 1, 1000000);
        carrierGoodSymbols_ = 0;
    } else {
        carrierGoodSymbols_ = 0;
        carrierBadSymbols_ = 0;
    }

    if (!stats_.carrierDetected && carrierGoodSymbols_ >= 32)
        stats_.carrierDetected = true;
    if (stats_.carrierDetected && carrierBadSymbols_ >= 24) {
        stats_.carrierDetected = false;
        clearRawAssembler();
    }

    // Protocol lock is intentionally fail-closed until a validated frame
    // synchronizer, deinterleaver and FEC chain exists.
    stats_.locked = false;
    stats_.framesOut = 0;
    ++stats_.symbolsOut;
}

void InmarsatDemod::emitBits(const uint8_t* bits, size_t nBits) {
    if (!bits || nBits == 0) return;
    for (size_t i = 0; i < nBits; ++i) {
        const uint8_t bit = bits[i] & 1u;
        ++stats_.bitsOut;
        if (!stats_.carrierDetected) {
            clearRawAssembler();
            continue;
        }

        packedByte_ = static_cast<uint8_t>((packedByte_ << 1) | bit);
        ++packedBitCount_;
        if (packedBitCount_ != 8) continue;

        rawBlock_.push_back(packedByte_);
        packedByte_ = 0;
        packedBitCount_ = 0;
        if (rawBlock_.size() < 64) continue;

        if (sink_) {
            try {
                sink_(rawBlock_.data(), rawBlock_.size());
            } catch (...) {
                // Diagnostics must never take down the live IQ worker.
            }
        }
        ++stats_.rawBlocksOut;
        rawBlock_.clear();
    }
}

void InmarsatDemod::demodBpsk(
    const std::vector<std::complex<float>>& baseband) {
    if (baseband.empty() || basebandRateHz_ <= 0.0) return;
    const double symbolStep = std::min(1.0, symbolRate(mode_) / basebandRateHz_);
    constexpr double alpha = 0.035;
    constexpr double beta = 0.00045;

    for (const auto& sample : baseband) {
        const std::complex<float> rotated = rotateBy(sample, -costasPhase_);
        const double norm = std::max(1.0e-12, static_cast<double>(std::norm(rotated)));
        const double decision = rotated.real() >= 0.0f ? 1.0 : -1.0;
        const double error = decision * static_cast<double>(rotated.imag()) /
                             std::sqrt(norm);
        costasFreq_ = std::clamp(costasFreq_ + beta * error, -0.25, 0.25);
        costasPhase_ += costasFreq_ + alpha * error;
        wrapPhase(costasPhase_);

        symbolAccumulator_ += std::complex<double>(rotated.real(), rotated.imag());
        ++symbolAccumulatorCount_;
        symbolPhase_ += symbolStep;
        if (symbolPhase_ < 1.0) continue;
        symbolPhase_ -= 1.0;

        const std::complex<float> symbol = symbolAccumulatorCount_ > 0
            ? std::complex<float>(
                static_cast<float>(symbolAccumulator_.real() / symbolAccumulatorCount_),
                static_cast<float>(symbolAccumulator_.imag() / symbolAccumulatorCount_))
            : std::complex<float>{0.f, 0.f};
        symbolAccumulator_ = {0.0, 0.0};
        symbolAccumulatorCount_ = 0;
        updateCarrierQuality(symbol, 2);
        const uint8_t bit = symbol.real() >= 0.0f ? 1u : 0u;
        emitBits(&bit, 1);
    }
    stats_.freqOffsetHz = costasFreq_ * basebandRateHz_ / kTwoPi;
}

void InmarsatDemod::demodOqpsk(
    const std::vector<std::complex<float>>& baseband) {
    if (baseband.empty() || basebandRateHz_ <= 0.0) return;
    const double symbolStep = std::min(1.0, symbolRate(mode_) / basebandRateHz_);
    constexpr double alpha = 0.025;
    constexpr double beta = 0.00025;

    for (const auto& sample : baseband) {
        const std::complex<float> rotated = rotateBy(sample, -costasPhase_);
        const double magnitude = std::abs(rotated);
        double phaseError = 0.0;
        if (magnitude > 1.0e-8) {
            std::complex<double> unit{
                static_cast<double>(rotated.real()) / magnitude,
                static_cast<double>(rotated.imag()) / magnitude};
            const std::complex<double> fourth = unit * unit * unit * unit;
            phaseError = std::atan2(fourth.imag(), fourth.real()) / 4.0;
        }
        costasFreq_ = std::clamp(costasFreq_ + beta * phaseError, -0.20, 0.20);
        costasPhase_ += costasFreq_ + alpha * phaseError;
        wrapPhase(costasPhase_);

        symbolAccumulator_ += std::complex<double>(rotated.real(), rotated.imag());
        ++symbolAccumulatorCount_;
        symbolPhase_ += symbolStep;
        if (symbolPhase_ < 1.0) continue;
        symbolPhase_ -= 1.0;

        const std::complex<float> symbol = symbolAccumulatorCount_ > 0
            ? std::complex<float>(
                static_cast<float>(symbolAccumulator_.real() / symbolAccumulatorCount_),
                static_cast<float>(symbolAccumulator_.imag() / symbolAccumulatorCount_))
            : std::complex<float>{0.f, 0.f};
        symbolAccumulator_ = {0.0, 0.0};
        symbolAccumulatorCount_ = 0;
        updateCarrierQuality(symbol, 4);
        const uint8_t bits[2] = {
            static_cast<uint8_t>(symbol.real() >= 0.0f ? 1u : 0u),
            static_cast<uint8_t>(symbol.imag() >= 0.0f ? 1u : 0u)};
        emitBits(bits, 2);
    }
    stats_.freqOffsetHz = costasFreq_ * basebandRateHz_ / kTwoPi;
}

void InmarsatDemod::demodPmsk(
    const std::vector<std::complex<float>>& baseband) {
    if (baseband.empty() || basebandRateHz_ <= 0.0) return;
    const double symbolStep = std::min(1.0, symbolRate(mode_) / basebandRateHz_);

    for (const auto& sample : baseband) {
        const std::complex<float> differential = sample * std::conj(prevSample_);
        prevSample_ = sample;
        const double magnitude = std::abs(differential);
        if (magnitude > 1.0e-9) {
            const std::complex<double> unit{
                static_cast<double>(differential.real()) / magnitude,
                static_cast<double>(differential.imag()) / magnitude};
            symbolAccumulator_ += unit;
            differentialAccumulator_ += std::atan2(unit.imag(), unit.real());
        }
        ++symbolAccumulatorCount_;
        symbolPhase_ += symbolStep;
        if (symbolPhase_ < 1.0) continue;
        symbolPhase_ -= 1.0;

        const std::complex<float> symbol = symbolAccumulatorCount_ > 0
            ? std::complex<float>(
                static_cast<float>(symbolAccumulator_.real() / symbolAccumulatorCount_),
                static_cast<float>(symbolAccumulator_.imag() / symbolAccumulatorCount_))
            : std::complex<float>{0.f, 0.f};
        const uint8_t bit = differentialAccumulator_ >= 0.0 ? 1u : 0u;
        symbolAccumulator_ = {0.0, 0.0};
        differentialAccumulator_ = 0.0;
        symbolAccumulatorCount_ = 0;
        updateCarrierQuality(symbol, 2);
        emitBits(&bit, 1);
    }
    stats_.freqOffsetHz = 0.0;
}

void InmarsatDemod::process(const std::complex<float>* iq, size_t n) {
    if (!iq || n == 0) return;
    std::vector<std::complex<float>> baseband;
    ddcAndDecimate(iq, n, baseband);
    switch (mode_) {
    case InmarsatDemodMode::AeroMsk600:
    case InmarsatDemodMode::AeroMsk1200:
        demodPmsk(baseband);
        break;
    case InmarsatDemodMode::EgcBpsk1200:
        demodBpsk(baseband);
        break;
    case InmarsatDemodMode::AeroVoice8400:
    case InmarsatDemodMode::AeroOqpsk10500:
    default:
        demodOqpsk(baseband);
        break;
    }
}
