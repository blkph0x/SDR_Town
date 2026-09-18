#include "P25AudioResampler.h"
#include "P25ReceiverSession.h"
#include <algorithm>
#include <cmath>

std::vector<float> resampleDecodedP25PcmWithState(P25AudioResamplerState& st,
                                                         const std::vector<float>& pcm,
                                                         double inputRate,
                                                         double outputRate)
{
    if (pcm.empty() || !std::isfinite(inputRate) || !std::isfinite(outputRate) ||
        inputRate <= 0.0 || outputRate <= 0.0) {
        return {};
    }

    // SDRTrunk addAudio()s JMBE floats with no per-block AGC. mbelib PCM is
    // already mapped to [-1, 1] in normalizedMbelibPcm(). A second peak
    // normalize + tanh here pumped each Voice4 island to full scale and
    // added harmonic "filler" in quiet/concealment frames.
    (void)0;
    const double gain = 1.0;

    if (std::abs(st.lastInputRate - inputRate) > 1.0 ||
        std::abs(st.lastOutputRate - outputRate) > 1.0) {
        st.phase = 0.0;
        st.histYm2 = st.histYm1 = st.histY0 = 0.0f;
        st.haveHist = false;
        st.dcBlockX1 = 0.0f;
        st.dcBlockY1 = 0.0f;
        st.lastInputRate = inputRate;
        st.lastOutputRate = outputRate;
    }

    // DEC-0077: delay by two input samples so the cubic stencil never needs
    // future samples clamped at a producer block boundary. At 8 kHz this is
    // 0.25 ms, independent of the producer's block size. Preserve PCM cadence.
    const double step = inputRate / outputRate;
    const double frameEnd = static_cast<double>(pcm.size());
    const double remainingInput = frameEnd - st.phase;
    if (remainingInput <= 0.0) {
        st.phase = 0.0;
        return {};
    }
    // Count output samples deterministically.  The previous ceil() on a raw
    // floating ratio occasionally turned exact 8 kHz -> 48 kHz AMBE blocks
    // into 960*n + 1 samples, leaving a sub-frame tail in the speaker queue.
    const size_t expected = std::max<size_t>(1, static_cast<size_t>(
        std::ceil((remainingInput / std::max(step, 1e-12)) - 1e-9)));
    std::vector<float> out;
    out.reserve(expected);
    constexpr double kP25AudioPi = 3.14159265358979323846;
    const float dcPole = static_cast<float>(std::exp(-2.0 * kP25AudioPi * 45.0 / outputRate));

    auto getY = [&](long i) -> float {
        if (i < 0) {
            if (!st.haveHist) return 0.0f;
            if (i == -1) return st.histY0;
            if (i == -2) return st.histYm1;
            if (i == -3) return st.histYm2;
            return 0.0f;
        }
        if ((size_t)i >= pcm.size()) return pcm.empty() ? 0.0f : pcm.back();
        return std::isfinite(pcm[static_cast<size_t>(i)]) ? pcm[static_cast<size_t>(i)] : 0.0f;
    };

    for (size_t n = 0; n < expected; ++n) {
        double pos = st.phase;
        long idx = static_cast<long>(std::floor(pos));
        double frac = pos - static_cast<double>(idx);
        float ym1 = getY(idx - 3);
        float y0  = getY(idx - 2);
        float y1  = getY(idx - 1);
        float y2  = getY(idx);
        float t = static_cast<float>(frac), t2 = t*t, t3 = t2*t;
        float c0 = y0;
        float c1 = 0.5f * (y1 - ym1);
        float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        float v = (c0 + c1 * t + c2 * t2 + c3 * t3) * static_cast<float>(gain);
        const float hp = v - st.dcBlockX1 + dcPole * st.dcBlockY1;
        st.dcBlockX1 = v;
        st.dcBlockY1 = std::isfinite(hp) ? hp : 0.0f;
        v = std::clamp(st.dcBlockY1, -1.0f, 1.0f);
        out.push_back(v);
        st.phase += step;
    }

    st.phase -= frameEnd;
    if (std::abs(st.phase) < 1e-8) {
        st.phase = 0.0;
    }
    // Do not aggressively reset phase to 0 during a call; that can introduce small
    // discontinuities in the resampled stream making "blocky" / not-joined audio.
    // Only reset on rate change (above). Allow fractional/negative for correct
    // history handoff to next 160-sample mbelib block.
    if (st.phase < -10.0 || st.phase > 200.0) {
        st.phase = 0.0;  // only on extreme drift
    }

    // Update cubic history from end of this block for next frame (stateful, no clicks)
    if (!pcm.empty()) {
        const long n = static_cast<long>(pcm.size());
        const float ym2 = getY(n - 3), ym1 = getY(n - 2), y0 = getY(n - 1);
        st.histYm2 = ym2;
        st.histYm1 = ym1;
        st.histY0 = y0;
        st.haveHist = true;
    }

    return out;
}
