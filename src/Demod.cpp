#define _USE_MATH_DEFINES
#include "Demod.h"
#include "SignalClassifier.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include <cstddef>

static double localPercentile(std::vector<double> values, double percentile, double fallback)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
        return !std::isfinite(v);
    }), values.end());
    if (values.empty()) return fallback;
    std::sort(values.begin(), values.end());
    percentile = std::clamp(percentile, 0.0, 1.0);
    size_t idx = static_cast<size_t>(std::llround(percentile * (values.size() - 1)));
    idx = std::min(idx, values.size() - 1);
    return values[idx];
}

SignalOffsetEstimate estimateSignalOffsetFromSpectrum(const std::vector<float>& powerDb,
                                                       double sampleRateHz,
                                                       double centerFreqHz,
                                                       double targetFreqHz,
                                                       double searchHz,
                                                       double maxSignalBandwidthHz)
{
    SignalOffsetEstimate out;
    const int bins = static_cast<int>(powerDb.size());
    if (bins < 64 || sampleRateHz <= 0.0 || !std::isfinite(sampleRateHz)) return out;

    const double binHz = sampleRateHz / static_cast<double>(bins);
    const double fullStart = centerFreqHz - sampleRateHz * 0.5;
    const double rel = (targetFreqHz - fullStart) / binHz;
    if (!std::isfinite(rel) || rel < 0.0 || rel >= static_cast<double>(bins)) return out;

    searchHz = std::clamp(searchHz, binHz * 4.0, sampleRateHz * 0.5);
    maxSignalBandwidthHz = std::clamp(maxSignalBandwidthHz, binHz * 4.0, searchHz * 2.0);

    const int targetBin = std::clamp(static_cast<int>(std::llround(rel - 0.5)), 0, bins - 1);
    const int halfSearch = std::max(4, static_cast<int>(std::ceil(searchHz / binHz)));
    const int lo = std::max(0, targetBin - halfSearch);
    const int hi = std::min(bins - 1, targetBin + halfSearch);
    if (hi <= lo) return out;

    std::vector<double> local;
    local.reserve(static_cast<size_t>(hi - lo + 1));
    int peakIdx = targetBin;
    double peakDb = -180.0;
    for (int i = lo; i <= hi; ++i) {
        const double v = powerDb[static_cast<size_t>(i)];
        local.push_back(v);
        if (v > peakDb) {
            peakDb = v;
            peakIdx = i;
        }
    }

    const double floorDb = localPercentile(local, 0.25, -120.0);
    const double snrDb = peakDb - floorDb;
    out.peakDb = peakDb;
    out.snrDb = snrDb;
    if (snrDb < 5.0) return out;

    const double thresholdDb = floorDb + std::clamp(snrDb * 0.35, 3.0, 10.0);
    const int quietRunNeeded = std::max(1, static_cast<int>(std::ceil(1400.0 / binHz)));
    const int maxSignalBins = std::max(4, static_cast<int>(std::ceil(maxSignalBandwidthHz / binHz)));

    int left = peakIdx;
    int quiet = 0;
    while (left > lo && peakIdx - left < maxSignalBins) {
        --left;
        if (powerDb[static_cast<size_t>(left)] >= thresholdDb) quiet = 0;
        else if (++quiet >= quietRunNeeded) {
            left += quietRunNeeded;
            break;
        }
    }

    int right = peakIdx;
    quiet = 0;
    while (right < hi && right - left < maxSignalBins) {
        ++right;
        if (powerDb[static_cast<size_t>(right)] >= thresholdDb) quiet = 0;
        else if (++quiet >= quietRunNeeded) {
            right -= quietRunNeeded;
            break;
        }
    }

    left = std::clamp(left, lo, hi);
    right = std::clamp(right, lo, hi);
    if (left > right) std::swap(left, right);

    double weighted = 0.0;
    double weightSum = 0.0;
    for (int i = left; i <= right; ++i) {
        const double aboveFloor = static_cast<double>(powerDb[static_cast<size_t>(i)]) - floorDb;
        if (aboveFloor < 1.5) continue;
        const double w = std::max(0.0, std::pow(10.0, std::clamp(aboveFloor, 0.0, 80.0) / 10.0) - 1.0);
        weighted += (static_cast<double>(i) + 0.5) * w;
        weightSum += w;
    }
    if (weightSum <= 0.0) return out;

    const double centerBin = weighted / weightSum;
    const double signalFreqHz = fullStart + centerBin * binHz;
    const double offsetHz = signalFreqHz - targetFreqHz;
    if (!std::isfinite(offsetHz) || std::abs(offsetHz) > searchHz + binHz) return out;

    const bool nearSearchEdge = (peakIdx - lo) < 2 || (hi - peakIdx) < 2;
    double confidence = std::clamp((snrDb - 5.0) / 18.0, 0.0, 1.0);
    if (nearSearchEdge) confidence *= 0.5;

    out.valid = confidence >= 0.20;
    out.offsetHz = offsetHz;
    out.signalFreqHz = signalFreqHz;
    out.confidence = confidence;
    return out;
}

std::vector<P25ControlCandidate> detectP25ControlCandidates(const std::vector<float>& powerDb,
                                                            double sampleRateHz,
                                                            double centerFreqHz,
                                                            size_t maxResults)
{
    std::vector<P25ControlCandidate> hits;
    const int bins = static_cast<int>(powerDb.size());
    if (bins < 64 || sampleRateHz <= 0.0 || !std::isfinite(sampleRateHz)) return hits;

    std::vector<double> all;
    all.reserve(powerDb.size());
    for (float v : powerDb) all.push_back(v);

    const double floorDb = localPercentile(all, 0.35, -120.0);
    const double binHz = sampleRateHz / static_cast<double>(bins);
    const double thresholdDb = floorDb + 5.0;
    const int maxGapBins = std::max(1, static_cast<int>(std::ceil(1800.0 / binHz)));
    const double minBwHz = 5500.0;
    const double maxBwHz = 21000.0;
    const double fullStart = centerFreqHz - sampleRateHz / 2.0;

    int regionStart = -1;
    int regionEnd = -1;
    int quietRun = 0;
    double regionPeakDb = -180.0;

    auto flushRegion = [&]() {
        if (regionStart < 0 || regionEnd < regionStart) return;
        const double bwHz = std::max(binHz, (regionEnd - regionStart + 1) * binHz);
        const double snrDb = regionPeakDb - floorDb;
        if (bwHz >= minBwHz && bwHz <= maxBwHz && snrDb >= 6.0) {
            double weighted = 0.0;
            double weightSum = 0.0;
            for (int i = regionStart; i <= regionEnd; ++i) {
                double w = std::max(0.0, std::pow(10.0, (powerDb[static_cast<size_t>(i)] - floorDb) / 10.0) - 1.0);
                weighted += (i + 0.5) * w;
                weightSum += w;
            }
            double centerBin = (weightSum > 0.0)
                ? weighted / weightSum
                : (regionStart + regionEnd + 1) * 0.5;
            P25ControlCandidate c;
            c.freqHz = fullStart + centerBin * binHz;
            c.bandwidthHz = bwHz;
            c.peakDb = regionPeakDb;
            c.snrDb = snrDb;
            hits.push_back(c);
        }
    };

    for (int i = 0; i < bins; ++i) {
        const double p = powerDb[static_cast<size_t>(i)];
        if (p >= thresholdDb) {
            if (regionStart < 0) {
                regionStart = i;
                regionPeakDb = p;
            }
            regionEnd = i;
            quietRun = 0;
            regionPeakDb = std::max(regionPeakDb, p);
        } else if (regionStart >= 0) {
            if (++quietRun > maxGapBins) {
                regionEnd = std::max(regionStart, i - quietRun);
                flushRegion();
                regionStart = -1;
                regionEnd = -1;
                quietRun = 0;
                regionPeakDb = -180.0;
            }
        }
    }
    if (regionStart >= 0) {
        regionEnd = std::max(regionStart, bins - quietRun - 1);
        flushRegion();
    }

    std::sort(hits.begin(), hits.end(), [](const P25ControlCandidate& a, const P25ControlCandidate& b) {
        return a.snrDb > b.snrDb;
    });
    if (hits.size() > maxResults) hits.resize(maxResults);
    return hits;
}

// classifyMode and detectChannelBandwidth moved here from main.cpp (stateless, but now single source in Demod module).
DemodMode classifyMode(const std::vector<float>& powerDb, double sampleRate, double centerFreq) {
    return AdvancedSignalClassifier::instance()
        .classifySpectrum(powerDb, sampleRate, centerFreq, centerFreq)
        .demodMode;
}

DemodMode classifyModeAround(const std::vector<float>& powerDb,
                             double sampleRate,
                             double centerFreq,
                             double targetFreq)
{
    return AdvancedSignalClassifier::instance()
        .classifySpectrum(powerDb, sampleRate, centerFreq, targetFreq)
        .demodMode;
}

double detectChannelBandwidth(const std::vector<float>& powerDb, double sampleRate) {
    if (powerDb.empty()) return 25000.0;
    auto it = std::max_element(powerDb.begin(), powerDb.end());
    int peakIdx = std::distance(powerDb.begin(), it);
    float peak = *it;
    float thresh = peak - 10.0f; // 10 dB occupied BW
    int left = peakIdx;
    while (left > 0 && powerDb[left] > thresh) --left;
    int right = peakIdx;
    while (right < (int)powerDb.size()-1 && powerDb[right] > thresh) ++right;
    double bw = (right - left) * (sampleRate / powerDb.size());
    return std::max(5000.0, bw * 1.3); // guard band for filter
}

double detectChannelBandwidthAround(const std::vector<float>& powerDb,
                                    double sampleRate,
                                    double centerFreq,
                                    double targetFreq,
                                    double maxSearchHz)
{
    if (powerDb.empty() || sampleRate <= 0.0 || !std::isfinite(sampleRate)) return 25000.0;
    const int bins = static_cast<int>(powerDb.size());
    const double binHz = sampleRate / static_cast<double>(bins);
    const double fullStart = centerFreq - sampleRate / 2.0;
    const double rel = (targetFreq - fullStart) / binHz;
    if (!std::isfinite(rel)) return 25000.0;

    if (rel < 0.0 || rel >= static_cast<double>(bins)) return 25000.0;

    const int centerBin = std::clamp(static_cast<int>(std::llround(rel - 0.5)), 0, bins - 1);
    const int halfSearch = std::max(4, static_cast<int>(std::ceil(std::clamp(maxSearchHz, 5000.0, sampleRate * 0.5) / binHz)));
    const int lo = std::max(0, centerBin - halfSearch);
    const int hi = std::min(bins - 1, centerBin + halfSearch);

    int peakIdx = centerBin;
    float peak = powerDb[centerBin];
    std::vector<double> local;
    local.reserve(static_cast<size_t>(hi - lo + 1));
    for (int i = lo; i <= hi; ++i) {
        local.push_back(powerDb[static_cast<size_t>(i)]);
        if (powerDb[static_cast<size_t>(i)] > peak) {
            peak = powerDb[static_cast<size_t>(i)];
            peakIdx = i;
        }
    }
    if (local.empty()) return 25000.0;
    std::sort(local.begin(), local.end());
    double floorDb = local[static_cast<size_t>(std::floor((local.size() - 1) * 0.25))];
    const double snrDb = std::max(0.0, static_cast<double>(peak) - floorDb);
    if (snrDb < 3.0) return 2500.0;

    // Use a floor-relative threshold, capped by a peak-relative threshold, so
    // strong AM carriers do not hide lower sideband energy and flat digital
    // plateaus (P25/DMR/etc.) are not collapsed to one bright bin.
    const double floorLift = std::clamp(snrDb * 0.40, 3.0, 8.0);
    float thresh = static_cast<float>(std::max(floorDb + 3.0, std::min(floorDb + floorLift, static_cast<double>(peak) - 18.0)));

    const int quietRunNeeded = std::max(1, static_cast<int>(std::ceil(1500.0 / binHz)));
    int left = peakIdx;
    int quiet = 0;
    while (left > lo) {
        --left;
        if (powerDb[static_cast<size_t>(left)] > thresh) {
            quiet = 0;
        } else if (++quiet >= quietRunNeeded) {
            left += quietRunNeeded;
            break;
        }
    }
    quiet = 0;
    int right = peakIdx;
    while (right < hi) {
        ++right;
        if (powerDb[static_cast<size_t>(right)] > thresh) {
            quiet = 0;
        } else if (++quiet >= quietRunNeeded) {
            right -= quietRunNeeded;
            break;
        }
    }

    double bw = std::max(binHz, (right - left + 1) * binHz);
    return std::clamp(bw * 1.25, 2500.0, std::min(sampleRate, maxSearchHz * 2.0));
}

Demodulator::Demodulator() = default;
Demodulator::~Demodulator() = default;

void Demodulator::resetState() {
    dspStateNeedsReset = true;
}

std::vector<float> Demodulator::demodulateToAudio(const std::vector<std::complex<float>>& iq,
    double sr, double cf, double target, DemodMode mode, double& rmsOut,
    double lpfHz, double squelchDb, double gain, double wfmDeTauUs, double wfmPilotNotchR,
    double channelBwHz, size_t target_audio_samples, double outputRate, double externalSquelchLevelDb, bool audioLpfEnabled)
{
    rmsOut = -100;
    if (iq.empty()) return {};
    if (sr <= 0.0 || !std::isfinite(sr)) return {};
    if (lpfHz <= 0) {
        if (mode == DemodMode::WFM || mode == DemodMode::AUTO) lpfHz = 15000.0;
        else if (mode == DemodMode::AM) lpfHz = 9000.0;
        else if (mode == DemodMode::CW) lpfHz = 900.0;
        else if (mode == DemodMode::USB || mode == DemodMode::LSB) lpfHz = 3000.0;
        else lpfHz = 3000.0;
    }

    // === STATE-OF-THE-ART AUTOMATIC RATE & BITRATE CALCULATIONS (no compromise) ===
    if (outputRate <= 0.0) outputRate = 48000.0;
    double chunkDuration = (double)iq.size() / sr;
    size_t exactAudioNeeded = target_audio_samples > 0 ? target_audio_samples
        : (size_t)std::llround(chunkDuration * outputRate);

    // All state is now per Demodulator instance (no more statics that bleed across receivers/modes).
    double localInternalRate = (mode == DemodMode::WFM || mode == DemodMode::AUTO) ? 192000.0 : 48000.0;
    if (channelBwHz > 0) {
        double t = (mode == DemodMode::WFM || mode == DemodMode::AUTO) ? std::max(180000.0, channelBwHz * 1.1) : (channelBwHz * 2.0);
        localInternalRate = std::max(localInternalRate, std::min(t, sr));
    }
    localInternalRate = std::min(localInternalRate, sr);
    internalRate = localInternalRate; // publish to member for diagnostics if needed

    // Reset key DSP state on large retune or mode change (uses member variables).
    {
        if (std::abs(target - lastResetTarget) > 5000.0 || mode != lastResetMode) {
            prev = {1,0};
            ph = 0;
            cwBfoPhase = 0.0;
            firDelay.assign(firDelay.size(), std::complex<float>(0,0));
            dspStateNeedsReset = true;
            lastResetTarget = target;
            lastResetMode = mode;
        }
    }

    // DDC
    const double twoPi = 2.0 * 3.14159265358979323846;
    double phaseInc = twoPi * (target - cf) / sr;
    std::vector<std::complex<float>> baseband(iq.size());
    for (size_t k=0; k<iq.size(); ++k) {
        baseband[k] = iq[k] * std::polar(1.0f, (float)-ph);
        ph += phaseInc;
        if (ph > 3.14159265358979323846 || ph < -3.14159265358979323846) {
            ph = std::remainder(ph, twoPi);
        }
    }

    // Channel FIR (same design as before)
    if (channelBwHz <= 0) {
        channelBwHz = (mode == DemodMode::WFM || mode == DemodMode::AUTO) ? 180000.0
            : (mode == DemodMode::AM ? 20000.0 : (mode == DemodMode::CW ? 1000.0 : 12500.0));
    }
    if (std::abs(channelBwHz - lastBw) > 50 || std::abs(sr - lastS) > 100) {
        double fc = channelBwHz / 2.0 / sr;
        double atten = 60.0;
        double beta = 0.0;
        if (atten > 50) beta = 0.1102 * (atten - 8.7);
        else if (atten > 21) beta = 0.5842 * std::pow(atten - 21, 0.4) + 0.07886 * (atten - 21);
        int half = (int)std::ceil(3.3 / (fc * 0.5) + 1);
        half = std::min(half, 160);
        int nTaps = 2 * half + 1;
        chanTaps.resize(nTaps);
        double sum = 0;
        for (int i = 0; i < nTaps; ++i) {
            int m = i - half;
            double w = 0;
            double arg = (double)m / half;
            if (std::abs(arg) < 1.0) {
                double x = beta * std::sqrt(1.0 - arg*arg);
                w = std::cyl_bessel_i(0, x) / std::cyl_bessel_i(0, beta);
            }
            double sinc = (m == 0) ? (2.0 * fc) : std::sin(2*M_PI*fc*m) / (M_PI * m);
            chanTaps[i] = (float)(w * sinc);
            sum += chanTaps[i];
        }
        if (sum != 0) for (auto &t : chanTaps) t /= (float)sum;
        lastBw = channelBwHz; lastS = sr;
        firDelay.assign( std::min(firDelay.size(), (size_t)nTaps-1), std::complex<float>(0,0) );
    }
    if (!chanTaps.empty() && channelBwHz > 0) {
        size_t M = chanTaps.size();
        size_t D = (M > 0 ? M-1 : 0);
        if (firDelay.size() != D) firDelay.resize(D, std::complex<float>(0,0));

        std::vector<std::complex<float>> ext;
        ext.reserve(D + baseband.size());
        ext.insert(ext.end(), firDelay.begin(), firDelay.end());
        ext.insert(ext.end(), baseband.begin(), baseband.end());

        std::vector<std::complex<float>> inputTail;
        if (baseband.size() >= D) {
            inputTail.assign(ext.end() - D, ext.end());
        }

        std::vector<std::complex<float>> filt(ext.size());
        int h = (int)M / 2;
        for (size_t n = 0; n < ext.size(); ++n) {
            std::complex<float> acc(0,0);
            for (int k = -h; k <= h; ++k) {
                long idx = (long)n + k;
                if (idx >= 0 && idx < (long)ext.size()) acc += ext[idx] * chanTaps[k + h];
            }
            filt[n] = acc;
        }

        baseband.assign(filt.begin() + D, filt.end());

        if (!inputTail.empty()) {
            firDelay = std::move(inputTail);
        } else if (baseband.size() >= D) {
            firDelay.assign(baseband.end() - D, baseband.end());
        } else {
            firDelay = baseband;
            firDelay.resize(D, std::complex<float>(0,0));
        }
    }

    if (mode == DemodMode::LSB) {
        for (auto &s : baseband) s = std::conj(s);
    }

    double channelPower = 0.0;
    for (const auto& s : baseband) channelPower += std::norm(s);
    double channelLevelDb = 10.0 * std::log10(channelPower / std::max<size_t>(1, baseband.size()) + 1e-20);

    // WFM channelizer decimation (still scalar for now; polyphase upgrade planned)
    double demodRate = sr;
    bool isWFM = (mode == DemodMode::WFM || mode == DemodMode::AUTO);
    if (isWFM && internalRate > 1000.0 && internalRate < sr * 0.95) {
        int M = std::max(1, (int)std::llround(sr / internalRate));
        if (M > 1) {
            std::vector<std::complex<float>> dec;
            dec.reserve(baseband.size() / M + 1);
            for (size_t i = 0; i < baseband.size(); i += M) {
                dec.push_back(baseband[i]);
            }
            baseband = std::move(dec);
            demodRate = sr / M;
        }
    }

    // Pre-disc FM limiter for FM modes only. Do not limit AM/SSB: their amplitude carries information.
    if (isWFM) {
        for (auto &s : baseband) {
            float mag = std::abs(s) + 1e-9f;
            s = s / mag;
        }
    } else if (mode == DemodMode::NFM) {
        for (auto &s : baseband) {
            float mag = std::abs(s) + 1e-9f;
            s = s / mag;
        }
    }

    std::vector<float> base(baseband.size());
    if (mode == DemodMode::AM) {
        if (dspStateNeedsReset) { amCarrierValid = false; amCarrier = 1.0f; }
        float carrierAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * 20.0f / (float)std::max(1000.0, demodRate));
        for (size_t k=0; k<baseband.size(); ++k) {
            float env = std::abs(baseband[k]);
            if (!amCarrierValid) {
                amCarrier = std::max(env, 1e-4f);
                amCarrierValid = true;
            }
            amCarrier = amCarrier * (1.0f - carrierAlpha) + env * carrierAlpha;
            base[k] = (env - amCarrier) / std::max(amCarrier, 1e-4f);
        }
    } else if (mode == DemodMode::CW) {
        constexpr double cwPitchHz = 700.0;
        const double step = 2.0 * M_PI * cwPitchHz / std::max(1000.0, demodRate);
        if (dspStateNeedsReset) cwBfoPhase = 0.0;
        for (size_t k=0; k<baseband.size(); ++k) {
            const std::complex<float> bfo(std::cos(cwBfoPhase), std::sin(cwBfoPhase));
            base[k] = std::real(baseband[k] * bfo);
            cwBfoPhase += step;
            if (cwBfoPhase > M_PI || cwBfoPhase < -M_PI) {
                cwBfoPhase = std::remainder(cwBfoPhase, 2.0 * M_PI);
            }
        }
    } else if (mode == DemodMode::USB || mode == DemodMode::LSB) {
        for (size_t k=0; k<baseband.size(); ++k) base[k] = std::real(baseband[k]);
    } else {
        const double deviationNormHz = (mode == DemodMode::NFM)
            ? std::clamp(channelBwHz * 0.20, 1800.0, 5000.0)
            : 75000.0;
        const float fmScale = static_cast<float>(demodRate / (2.0 * M_PI * deviationNormHz));
        for (size_t k=0; k<baseband.size(); ++k) {
            auto s = baseband[k];
            float d = std::arg(s * std::conj(prev));
            prev = s;
            base[k] = d * fmScale;
        }
    }

    // Voice AGC only for SSB. FM levels are set by deviation; AM is carrier-normalized above.
    // Running AGC before squelch on AM/NFM lifts noise and makes the squelch meter misleading.
    if (mode == DemodMode::USB || mode == DemodMode::LSB || mode == DemodMode::CW) {
        if (dspStateNeedsReset) { agcGain = 1.0f; }
        float attack = 0.05f;
        float decay = 0.0005f;
        float targetLevel = 0.3f;
        for (auto &b : base) {
            float env = std::abs(b) + 1e-9f;
            float desired = targetLevel / env;
            if (desired > agcGain) agcGain = agcGain * (1-attack) + desired * attack;
            else agcGain = agcGain * (1-decay) + desired * decay;
            agcGain = std::clamp(agcGain, 0.01f, 100.0f);
            b *= agcGain;
        }
    }

    // Optional post-demod LPF at demodRate. Can be disabled for data/decoder workflows.
    if (audioLpfEnabled && lpfHz > 100.0) {
        if (dspStateNeedsReset) { lpA = lpB = dcv = 0; }
        float alpha = 1.0f - std::exp(-2 * 3.14159265f * (float)lpfHz / (float)demodRate);
        // Rate-normalized DC removal. A fixed 0.995 coefficient becomes a ~1.9 kHz
        // high-pass at 2.4 MS/s, which removes much of NFM voice before resampling.
        float dca = std::exp(-2.0f * 3.14159265f * 20.0f / (float)std::max(1000.0, demodRate));
        dca = std::clamp(dca, 0.0f, 0.9999999f);
        for (auto &b : base) {
            lpA = lpA * (1-alpha) + b * alpha;
            lpB = lpB * (1-alpha) + lpA * alpha;
            dcv = dcv * dca + lpB * (1-dca);
            b = lpB - dcv;
        }
    }

    // Resample (streaming cubic with fractional phase carried between chunks)
    std::vector<float> aud;
    if (exactAudioNeeded > 0 && !base.empty()) {
        if (dspStateNeedsReset ||
            std::abs(lastResampInputRate - demodRate) > 1.0 ||
            std::abs(lastResampOutputRate - outputRate) > 1.0) {
            haveHist = false;
            histYm2 = histYm1 = histY0 = 0;
            resampPhase = 0.0;
            lastResampInputRate = demodRate;
            lastResampOutputRate = outputRate;
        }
        aud.resize(exactAudioNeeded);
        const double step = (outputRate > 0.0) ? (demodRate / outputRate) : 1.0;
        for (size_t a = 0; a < exactAudioNeeded; ++a) {
            double pos = resampPhase + (double)a * step;
            long idx = (long)std::floor(pos);
            double frac = pos - idx;
            auto getY = [&](long i) -> float {
                if (i < 0) {
                    if (!haveHist) return 0.0f;
                    if (i == -1) return histY0;
                    if (i == -2) return histYm1;
                    if (i == -3) return histYm2;
                    return 0.0f;
                }
                if ((size_t)i >= base.size()) return base.back();
                return base[i];
            };
            float ym1 = getY(idx - 1);
            float y0  = getY(idx);
            float y1  = getY(idx + 1);
            float y2  = getY(idx + 2);
            float t = (float)frac, t2=t*t, t3=t2*t;
            float c0 = y0;
            float c1 = 0.5f*(y1-ym1);
            float c2 = ym1 - 2.5f*y0 + 2.0f*y1 - 0.5f*y2;
            float c3 = 0.5f*(y2-ym1) + 1.5f*(y0-y1);
            aud[a] = c0 + c1*t + c2*t2 + c3*t3;
        }
        resampPhase += (double)exactAudioNeeded * step;
        resampPhase -= (double)base.size();
        if (resampPhase < -3.0 || resampPhase > (double)base.size()) resampPhase = 0.0;
        if (resampPhase < 0.0 && resampPhase > -3.0) {
            // Keep a tiny negative carry; the cubic history above provides those samples.
        } else if (resampPhase < 0.0) {
            resampPhase = 0.0;
        }
        if (base.size() >= 3) {
            histYm2 = base[base.size()-3];
            histYm1 = base[base.size()-2];
            histY0  = base.back();
            haveHist = true;
        } else if (!base.empty()) {
            histY0 = base.back();
            haveHist = true;
        }
    } else {
        double ar = outputRate;
        size_t dec = std::max<size_t>(1, (size_t)std::round(sr / ar));
        aud.reserve(base.size() / dec + 1);
        for (size_t j=0; j<base.size(); j += dec) {
            float sm=0; int c=0;
            for (size_t m=0; m<dec && j+m<base.size(); ++m) { sm += base[j+m]; ++c; }
            if (c) aud.push_back(sm / c);
        }
    }

    // Final audio-rate processing (de-emph, notch, optional LPF, squelch, gain)
    if (!aud.empty()) {
        if (mode == DemodMode::WFM || mode == DemodMode::AUTO || (mode == DemodMode::NFM && audioLpfEnabled)) {
            if (dspStateNeedsReset) { des = 0; }
            const double tauUs = (mode == DemodMode::NFM) ? 750.0 : wfmDeTauUs;
            float tau = (float)std::max(1e-6, tauUs * 1e-6);
            float dp = std::exp(-1.0f / (tau * (float)outputRate));
            for (auto &s : aud) { des = des * dp + s * (1.0f - dp); s = des; }
        }
        if (mode == DemodMode::WFM || mode == DemodMode::AUTO) {
            if (dspStateNeedsReset) { nx1=nx2=ny1=ny2=0; }
            float w0 = 2*3.14159265f*19000/(float)outputRate;
            float rr = wfmPilotNotchR;
            float b0=1, b1=-2*std::cos(w0), b2=1;
            float a1=-2*rr*std::cos(w0), a2=rr*rr;
            for (auto &s : aud) {
                float no = b0*s + b1*nx1 + b2*nx2 - a1*ny1 - a2*ny2;
                nx2=nx1; nx1=s; ny2=ny1; ny1=no; s = no;
            }
        }
        if (audioLpfEnabled) {
            double finalCut = lpfHz;
            if (mode == DemodMode::NFM || (mode == DemodMode::AUTO && finalCut < 8000)) finalCut = std::min(finalCut, 4500.0);
            else if (mode == DemodMode::WFM || mode == DemodMode::AUTO) finalCut = std::min(finalCut, 15000.0);
            if (finalCut > 100.0) {
                float fa = 1.0f - std::exp(-2 * 3.14159265f * (float)finalCut / (float)outputRate);
                if (dspStateNeedsReset) { flp1 = flp2 = 0; }
                for (auto &s : aud) { flp1 = flp1*(1-fa)+s*fa; flp2=flp2*(1-fa)+flp1*fa; s=flp2; }
            }
        }
        float sum=0;
        for (auto s : aud) sum += s*s;
        double audioRmsDb = 10 * std::log10( (sum / aud.size()) + 1e-12 );
        (void)audioRmsDb;
        const double squelchMetricDb = std::isfinite(externalSquelchLevelDb) ? externalSquelchLevelDb : channelLevelDb;
        rmsOut = squelchMetricDb;
        lastRmsDb = rmsOut;

        // S0-6 (P2) + P1 calibration: smooth squelch with attack/release/hang + hysteresis.
        // Added live RMS storage, per-mode default helper, and slightly wider effective hysteresis via hang + target.
        // UI exposes Auto (from recent RMS + offset) and open/closed can be queried via isSquelchOpen().
        {
            if (dspStateNeedsReset || squelchGateNeedsReset) {
                squelchGateGain = 0.0f;
                squelchHangLeft = 0;
                squelchGateNeedsReset = false;
            }

            // Live reaction to squelchDb changes (from main GUI spin, Auto, future table, or CLI "squelch" cmd).
            // If the threshold is raised (sq > previous), force immediate close (bypass hang) so muting feels live.
            // Lowering sq lets the normal fast attack open the gate promptly. Freq-click previously "fixed" it only
            // because sync+setActive forced a full resetDemodState() which zeroed the gate.
            if (std::abs(squelchDb - lastAppliedSquelchDb) > 0.01) {
                if (squelchDb > lastAppliedSquelchDb + 0.1) {
                    squelchGateGain = 0.0f;
                    squelchHangLeft = 0;
                }
                lastAppliedSquelchDb = squelchDb;
            }

            float target = (squelchDb < -115.0f || squelchMetricDb > squelchDb) ? 1.0f : 0.0f;

            // Hang is tracked in output samples and decremented by each processed block.
            // The old code decremented once per block, so "0.3s" could stick open for minutes.
            const int hangSamples = (int)((mode == DemodMode::AM || mode == DemodMode::USB || mode == DemodMode::LSB || mode == DemodMode::CW) ? 0.10 * outputRate : 0.18 * outputRate);
            if (target > 0.5f) squelchHangLeft = hangSamples;
            else if (squelchHangLeft > 0) {
                target = 1.0f;
                squelchHangLeft = std::max(0, squelchHangLeft - (int)aud.size());
            }

            const float attack = 1.0f - std::exp(-1.0f / (0.006f * (float)outputRate));
            const float release = 1.0f - std::exp(-1.0f / (0.025f * (float)outputRate));
            for (auto &s : aud) {
                if (target > squelchGateGain)
                    squelchGateGain = squelchGateGain * (1-attack) + target * attack;
                else
                    squelchGateGain = squelchGateGain * (1-release) + target * release;
                squelchGateGain = std::clamp(squelchGateGain, 0.0f, 1.0f);
                s *= gain * squelchGateGain;
            }
        }
        dspStateNeedsReset = false;
    }
    dspStateNeedsReset = false;
    return aud;
}

double Demodulator::getSquelchDefaultForMode(DemodMode m) const {
    switch (m) {
        case DemodMode::WFM: return -72.0;
        case DemodMode::NFM: return -105.0;
        case DemodMode::AM:  return -100.0;
        case DemodMode::USB:
        case DemodMode::LSB:
        case DemodMode::CW: return -105.0;
        case DemodMode::AUTO: return -105.0;
        default: return -105.0;
    }
}

// Back-compat free function wrappers (use a static Demodulator so old call sites keep working during transition).
static Demodulator s_legacyDemod;

std::vector<float> demodulateToAudio(const std::vector<std::complex<float>>& iq, double sr, double cf, double target, DemodMode mode, double& rmsOut, double lpfHz, double squelchDb, double gain, double wfmDeTauUs, double wfmPilotNotchR, double channelBwHz, size_t target_audio_samples, double outputRate, double externalSquelchLevelDb, bool audioLpfEnabled) {
    return s_legacyDemod.demodulateToAudio(iq, sr, cf, target, mode, rmsOut, lpfHz, squelchDb, gain, wfmDeTauUs, wfmPilotNotchR, channelBwHz, target_audio_samples, outputRate, externalSquelchLevelDb, audioLpfEnabled);
}
