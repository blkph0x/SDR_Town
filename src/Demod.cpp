#define _USE_MATH_DEFINES
#include "Demod.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include <cstddef>

// classifyMode and detectChannelBandwidth moved here from main.cpp (stateless, but now single source in Demod module).
DemodMode classifyMode(const std::vector<float>& powerDb, double sampleRate, double centerFreq) {
    if (powerDb.empty()) return DemodMode::NFM;
    if (centerFreq > 88e6 && centerFreq < 108e6) return DemodMode::WFM;
    double bw = detectChannelBandwidth(powerDb, sampleRate);
    if (bw > 80e3) return DemodMode::WFM;
    if (bw < 4e3) return DemodMode::USB;
    return DemodMode::NFM;
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

Demodulator::Demodulator() = default;
Demodulator::~Demodulator() = default;

void Demodulator::resetState() {
    dspStateNeedsReset = true;
}

std::vector<float> Demodulator::demodulateToAudio(const std::vector<std::complex<float>>& iq,
    double sr, double cf, double target, DemodMode mode, double& rmsOut,
    double lpfHz, double squelchDb, double gain, double wfmDeTauUs, double wfmPilotNotchR,
    double channelBwHz, size_t target_audio_samples, double outputRate)
{
    rmsOut = -100;
    if (iq.empty()) return {};
    if (lpfHz <= 0) {
        lpfHz = (mode == DemodMode::WFM || mode == DemodMode::AUTO) ? 15000.0 : 3500.0;
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
            firDelay.assign(firDelay.size(), std::complex<float>(0,0));
            dspStateNeedsReset = true;
            lastResetTarget = target;
            lastResetMode = mode;
        }
    }

    // DDC
    double phaseInc = 2 * 3.141592653589793 * (target - cf) / sr;
    std::vector<std::complex<float>> baseband(iq.size());
    for (size_t k=0; k<iq.size(); ++k) {
        baseband[k] = iq[k] * std::polar(1.0f, (float)-ph);
        ph += phaseInc;
    }

    // Channel FIR (same design as before)
    if (channelBwHz <= 0) {
        channelBwHz = (mode == DemodMode::WFM || mode == DemodMode::AUTO) ? 180000.0 : 25000.0;
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
            double sinc = (m == 0) ? 1.0 : std::sin(2*M_PI*fc*m) / (M_PI * m);
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

    // Pre-disc FM limiter for WFM
    if (isWFM) {
        for (auto &s : baseband) {
            float mag = std::abs(s) + 1e-9f;
            s = s / mag;
        }
    } else {
        for (auto &s : baseband) {
            float mag = std::abs(s);
            if (mag > 0.92f) s *= (0.92f / mag);
        }
    }

    std::vector<float> base(baseband.size());
    if (mode == DemodMode::AM) {
        for (size_t k=0; k<baseband.size(); ++k) base[k] = std::abs(baseband[k]);
    } else if (mode == DemodMode::USB || mode == DemodMode::LSB) {
        for (size_t k=0; k<baseband.size(); ++k) base[k] = std::real(baseband[k]);
    } else {
        for (size_t k=0; k<baseband.size(); ++k) {
            auto s = baseband[k];
            float d = std::arg(s * std::conj(prev));
            prev = s;
            base[k] = d;
        }
    }

    // AGC only for non-WFM
    if (mode != DemodMode::WFM && mode != DemodMode::AUTO) {
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

    // Post-demod LPF at demodRate
    {
        if (dspStateNeedsReset) { lpA = lpB = dcv = 0; }
        float alpha = 1.0f - std::exp(-2 * 3.14159265f * (float)lpfHz / (float)demodRate);
        float dca = 0.995f;
        for (auto &b : base) {
            lpA = lpA * (1-alpha) + b * alpha;
            lpB = lpB * (1-alpha) + lpA * alpha;
            dcv = dcv * dca + lpB * (1-dca);
            b = lpB - dcv;
        }
    }

    // Resample (cubic with history carried in members)
    std::vector<float> aud;
    if (exactAudioNeeded > 0 && !base.empty()) {
        if (dspStateNeedsReset) { haveHist = false; histYm2 = histYm1 = histY0 = 0; }
        aud.resize(exactAudioNeeded);
        for (size_t a = 0; a < exactAudioNeeded; ++a) {
            double pos = (double)a / exactAudioNeeded * (base.size() - 1);
            size_t idx = (size_t)std::floor(pos);
            double frac = pos - idx;
            if (idx >= base.size()-1) { aud[a] = base.back(); continue; }
            auto getY = [&](long i) -> float {
                if (i < 0) {
                    if (!haveHist) return (i >= -3 ? histY0 : 0.0f);
                    if (i == -1) return histY0;
                    if (i == -2) return histYm1;
                    if (i == -3) return histYm2;
                    return 0.0f;
                }
                if ((size_t)i >= base.size()) return base.back();
                return base[i];
            };
            float ym1 = getY( (long)idx -1 );
            float y0  = getY( (long)idx );
            float y1  = getY( (long)idx +1 );
            float y2  = getY( (long)idx +2 );
            float t = (float)frac, t2=t*t, t3=t2*t;
            float c0 = y0;
            float c1 = 0.5f*(y1-ym1);
            float c2 = ym1 - 2.5f*y0 + 2.0f*y1 - 0.5f*y2;
            float c3 = 0.5f*(y2-ym1) + 1.5f*(y0-y1);
            aud[a] = c0 + c1*t + c2*t2 + c3*t3;
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

    // Final audio-rate processing (de-emph, notch, LPF, squelch, gain)
    if (!aud.empty()) {
        if (mode == DemodMode::WFM || mode == DemodMode::AUTO) {
            if (dspStateNeedsReset) { des = 0; }
            float tau = wfmDeTauUs * 1e-6f;
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
        {
            double finalCut = lpfHz;
            if (mode == DemodMode::NFM || (mode == DemodMode::AUTO && finalCut < 8000)) finalCut = std::min(finalCut, 3800.0);
            else if (mode == DemodMode::WFM || mode == DemodMode::AUTO) finalCut = std::min(finalCut, 15000.0);
            if (finalCut > 100.0) {
                float fa = 1.0f - std::exp(-2 * 3.14159265f * (float)finalCut / (float)outputRate);
                if (dspStateNeedsReset) { flp1 = flp2 = 0; }
                for (auto &s : aud) { flp1 = flp1*(1-fa)+s*fa; flp2=flp2*(1-fa)+flp1*fa; s=flp2; }
            }
        }
        float sum=0;
        for (auto s : aud) sum += s*s;
        rmsOut = 10 * std::log10( (sum / aud.size()) + 1e-12 );
        lastRmsDb = rmsOut;

        // S0-6 (P2) + P1 calibration: smooth squelch with attack/release/hang + hysteresis.
        // Added live RMS storage, per-mode default helper, and slightly wider effective hysteresis via hang + target.
        // UI exposes Auto (from recent RMS - offset) and open/closed can be queried via isSquelchOpen().
        {
            if (dspStateNeedsReset) { squelchGateGain = 0.0f; squelchHangLeft = 0; }
            float thr = std::pow(10.0f, (float)squelchDb / 20.0f);
            float target = (squelchDb < -115.0f || rmsOut > squelchDb) ? 1.0f : 0.0f;

            // Hang + a bit of extra hysteresis (open stays open a little longer; close is reluctant).
            const int hangSamples = (int)(0.30 * outputRate);
            if (target > 0.5f) squelchHangLeft = hangSamples;
            else if (squelchHangLeft > 0) { target = 1.0f; --squelchHangLeft; }

            float attack = 0.025f;
            float release = 0.0007f;
            if (target > squelchGateGain)
                squelchGateGain = squelchGateGain * (1-attack) + target * attack;
            else
                squelchGateGain = squelchGateGain * (1-release) + target * release;

            squelchGateGain = std::clamp(squelchGateGain, 0.0f, 1.0f);

            for (auto &s : aud) {
                if (std::abs(s) < thr) s = 0;
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
        case DemodMode::NFM: return -88.0;
        case DemodMode::AM:  return -82.0;
        case DemodMode::USB:
        case DemodMode::LSB: return -95.0;
        case DemodMode::AUTO: return -85.0;
        default: return -90.0;
    }
}

// Back-compat free function wrappers (use a static Demodulator so old call sites keep working during transition).
static Demodulator s_legacyDemod;

std::vector<float> demodulateToAudio(const std::vector<std::complex<float>>& iq, double sr, double cf, double target, DemodMode mode, double& rmsOut, double lpfHz, double squelchDb, double gain, double wfmDeTauUs, double wfmPilotNotchR, double channelBwHz, size_t target_audio_samples, double outputRate) {
    return s_legacyDemod.demodulateToAudio(iq, sr, cf, target, mode, rmsOut, lpfHz, squelchDb, gain, wfmDeTauUs, wfmPilotNotchR, channelBwHz, target_audio_samples, outputRate);
}
