#include "InmarsatDemod.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

} // namespace

double InmarsatDemod::symbolRate(InmarsatDemodMode m) {
    switch (m) {
    case InmarsatDemodMode::AeroMsk600: return 600.0;
    case InmarsatDemodMode::AeroMsk1200:
    case InmarsatDemodMode::EgcBpsk1200: return 1200.0;
    case InmarsatDemodMode::AeroVoice8400: return 8400.0;
    case InmarsatDemodMode::AeroOqpsk10500:
    default: return 10500.0;
    }
}

InmarsatDemodMode InmarsatDemod::modeFromBaud(int baud, bool egc) {
    if (egc) return InmarsatDemodMode::EgcBpsk1200;
    if (baud <= 600) return InmarsatDemodMode::AeroMsk600;
    if (baud <= 1200) return InmarsatDemodMode::AeroMsk1200;
    if (baud <= 8400) return InmarsatDemodMode::AeroVoice8400;
    return InmarsatDemodMode::AeroOqpsk10500;
}

void InmarsatDemod::reset(InmarsatDemodMode mode, double sampleRateHz, double channelOffsetHz) {
    mode_ = mode;
    sampleRateHz_ = std::max(48e3, sampleRateHz);
    channelOffsetHz_ = channelOffsetHz;
    phase_ = 0.0;
    ncoPhase_ = 0.0;
    costasPhase_ = 0.0;
    costasFreq_ = 0.0;
    symbolPhase_ = 0.0;
    prevSample_ = {1.f, 0.f};
    iirState_.assign(4, {0.f, 0.f});
    bitBuf_.clear();
    byteAcc_.clear();
    bitCount_ = 0;
    stats_ = {};
}

void InmarsatDemod::setMode(InmarsatDemodMode mode) { mode_ = mode; }

void InmarsatDemod::setChannelOffset(double offsetHz) { channelOffsetHz_ = offsetHz; }

void InmarsatDemod::emitBits(const uint8_t* bits, size_t nBits) {
    if (!bits || nBits == 0) return;
    stats_.bitsOut += nBits;
    for (size_t i = 0; i < nBits; ++i) {
        byteAcc_.push_back(bits[i] & 1);
        if (byteAcc_.size() >= 8) {
            uint8_t b = 0;
            for (int k = 0; k < 8; ++k) b = static_cast<uint8_t>((b << 1) | (byteAcc_[k] & 1));
            byteAcc_.erase(byteAcc_.begin(), byteAcc_.begin() + 8);
            bitBuf_.push_back(b);
            if (bitBuf_.size() >= 16) {
                if (sink_) sink_(bitBuf_.data(), bitBuf_.size());
                stats_.framesOut++;
                bitBuf_.clear();
            }
        }
    }
}

void InmarsatDemod::ddcAndDecimate(const std::complex<float>* iq, size_t n,
                                   std::vector<std::complex<float>>& out) {
    out.clear();
    if (!iq || n == 0 || sampleRateHz_ <= 0.0) return;
    const double sym = symbolRate(mode_);
    const int decim = std::max(1, static_cast<int>(sampleRateHz_ / (sym * 8.0)));
    const double ncoStep = 2.0 * 3.14159265358979323846 * (-channelOffsetHz_) / sampleRateHz_;
    out.reserve(n / static_cast<size_t>(decim) + 1);
    for (size_t i = 0; i < n; ++i) {
        ncoPhase_ += ncoStep;
        if (ncoPhase_ > 6.283185307179586) ncoPhase_ -= 6.283185307179586;
        if (ncoPhase_ < -6.283185307179586) ncoPhase_ += 6.283185307179586;
        const float c = static_cast<float>(std::cos(ncoPhase_));
        const float s = static_cast<float>(std::sin(ncoPhase_));
        std::complex<float> mixed = iq[i] * std::complex<float>(c, s);
        // One-pole LPF
        iirState_[0] = iirState_[0] * 0.85f + mixed * 0.15f;
        if ((i % static_cast<size_t>(decim)) == 0) out.push_back(iirState_[0]);
    }
}

void InmarsatDemod::demodPmsk(const std::vector<std::complex<float>>& baseband) {
    if (baseband.size() < 2) return;
    const double sps = 8.0;
    std::vector<uint8_t> bits;
    bits.reserve(baseband.size() / 4);
    for (size_t i = 1; i < baseband.size(); ++i) {
        symbolPhase_ += 1.0;
        if (symbolPhase_ < sps) continue;
        symbolPhase_ -= sps;
        const auto d = baseband[i] * std::conj(prevSample_);
        prevSample_ = baseband[i];
        const float ph = std::atan2(d.imag(), d.real());
        bits.push_back(ph >= 0.f ? 1 : 0);
        // Crude lock metric
        stats_.ebnoDb = 0.9 * stats_.ebnoDb + 0.1 * (20.0 * std::log10(std::max(1e-6f, std::abs(baseband[i]))));
        stats_.locked = std::abs(ph) > 0.2f;
    }
    emitBits(bits.data(), bits.size());
}

void InmarsatDemod::demodOqpsk(const std::vector<std::complex<float>>& baseband) {
    if (baseband.size() < 4) return;
    const double sps = 8.0;
    std::vector<uint8_t> bits;
    bits.reserve(baseband.size() / 2);
    for (size_t i = 0; i < baseband.size(); ++i) {
        auto z = baseband[i] * std::complex<float>(static_cast<float>(std::cos(costasPhase_)),
                                                   static_cast<float>(-std::sin(costasPhase_)));
        // Decision-directed Costas
        const float ei = (z.real() > 0.f ? 1.f : -1.f);
        const float eq = (z.imag() > 0.f ? 1.f : -1.f);
        const float err = z.imag() * ei - z.real() * eq;
        costasFreq_ = costasFreq_ * 0.999 + err * 0.01;
        costasPhase_ += costasFreq_ * 0.05 + err * 0.02;
        if (costasPhase_ > 6.28) costasPhase_ -= 6.28;
        if (costasPhase_ < -6.28) costasPhase_ += 6.28;

        symbolPhase_ += 1.0;
        if (symbolPhase_ < sps * 0.5) continue;
        // Stagger: emit I then Q
        if (symbolPhase_ >= sps) {
            symbolPhase_ -= sps;
            bits.push_back(z.real() >= 0.f ? 1 : 0);
            bits.push_back(z.imag() >= 0.f ? 1 : 0);
            stats_.ebnoDb = 0.95 * stats_.ebnoDb + 0.05 * (10.0 * std::log10(std::max(1e-9f, std::norm(z))));
            stats_.locked = std::abs(err) < 0.8f;
            stats_.freqOffsetHz = costasFreq_ * sampleRateHz_ / (2.0 * 3.14159265358979323846);
        }
    }
    emitBits(bits.data(), bits.size());
}

void InmarsatDemod::demodBpsk(const std::vector<std::complex<float>>& baseband) {
    if (baseband.size() < 2) return;
    const double sps = 8.0;
    std::vector<uint8_t> bits;
    for (size_t i = 0; i < baseband.size(); ++i) {
        auto z = baseband[i] * std::complex<float>(static_cast<float>(std::cos(costasPhase_)),
                                                   static_cast<float>(-std::sin(costasPhase_)));
        const float err = z.real() * z.imag();
        costasPhase_ += err * 0.05;
        symbolPhase_ += 1.0;
        if (symbolPhase_ < sps) continue;
        symbolPhase_ -= sps;
        bits.push_back(z.real() >= 0.f ? 1 : 0);
        stats_.locked = true;
        stats_.ebnoDb = 0.9 * stats_.ebnoDb + 0.1 * (10.0 * std::log10(std::max(1e-9f, std::norm(z))));
    }
    emitBits(bits.data(), bits.size());
}

void InmarsatDemod::process(const std::complex<float>* iq, size_t n) {
    std::vector<std::complex<float>> bb;
    ddcAndDecimate(iq, n, bb);
    if (bb.empty()) return;
    switch (mode_) {
    case InmarsatDemodMode::AeroMsk600:
    case InmarsatDemodMode::AeroMsk1200:
        demodPmsk(bb);
        break;
    case InmarsatDemodMode::EgcBpsk1200:
        demodBpsk(bb);
        break;
    case InmarsatDemodMode::AeroVoice8400:
    case InmarsatDemodMode::AeroOqpsk10500:
    default:
        demodOqpsk(bb);
        break;
    }
}
