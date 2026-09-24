#include "HfDemod.h"

#include "Demod.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HfDemod {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kWorkRateHz = 48000.0;
constexpr int kMinimumResamplerHalf = 24;
constexpr int kMaximumResamplerHalf = 1024;

double sinc(double value) noexcept {
    if (std::abs(value) < 1.0e-12) return 1.0;
    const double x = kPi * value;
    return std::sin(x) / x;
}

double blackman(double normalized) noexcept {
    normalized = std::clamp(normalized, -1.0, 1.0);
    const double phase = (normalized + 1.0) * kPi;
    return 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
}

struct SincResampler {
    int half = kMinimumResamplerHalf;
    double position = kMinimumResamplerHalf;
    double inputRate = 0.0;
    double outputRate = 0.0;
    std::vector<std::complex<float>> buffer;
    std::vector<float> coefficients;

    void reset() {
        buffer.assign(static_cast<size_t>(half), {});
        position = half;
    }
};

struct State {
    std::mutex mutex;
    bool configured = false;
    bool explicitReset = true;
    DemodMode mode = DemodMode::AM;
    double inputRate = 0.0;
    double centerHz = 0.0;
    double targetHz = 0.0;
    double channelBandwidthHz = 0.0;
    double audioLowPassHz = 0.0;
    double outputRate = 0.0;

    std::complex<double> oscillator{1.0, 0.0};
    uint32_t oscillatorSamples = 0;
    float impulseMean = 0.0f;
    std::complex<float> impulseLastGood{0.0f, 0.0f};
    std::complex<float> iqDc{0.0f, 0.0f};

    SincResampler iqResampler;
    SincResampler audioResampler;

    std::vector<std::complex<float>> channelTaps;
    std::vector<std::complex<float>> channelDelay;
    size_t channelWrite = 0;

    float carrier = 1.0f;
    bool carrierValid = false;
    size_t carrierPrimingSamples = 0;
    double carrierPrimingSum = 0.0;
    double cwPhase = 0.0;

    float agcEnvelope = 0.0f;
    float agcGain = 1.0f;

    float hpInput = 0.0f;
    float hpOutput = 0.0f;
    float lpOne = 0.0f;
    float lpTwo = 0.0f;

    float squelchGain = 0.0f;
    int squelchHang = 0;
    float clickFade = 0.0f;
    int startupMuteSamples = 0;

    uint64_t decoderEpoch = 0;
    uint64_t decoderSamples = 0;
    bool decoderContinuous = false;
    double decoderIdentityHz = 0.0;
    DemodMode decoderMode = DemodMode::AM;
    double decoderRate = 0.0;
};

// Demod.cpp retains one legacy process-lifetime Demodulator for compatibility.
// Static destruction order across translation units is unspecified, so an ordinary
// namespace-static mutex/map here can be destroyed before that Demodulator calls
// HfDemod::release() during process shutdown. Keep the tiny registry infrastructure
// alive until process termination; normal Demodulator destruction still erases its
// per-owner State, so this does not accumulate receiver DSP state during runtime.
std::mutex& registryMutex() {
    static auto* mutex = new std::mutex();
    return *mutex;
}

std::unordered_map<const void*, std::shared_ptr<State>>& registry() {
    static auto* states =
        new std::unordered_map<const void*, std::shared_ptr<State>>();
    return *states;
}

std::shared_ptr<State> stateFor(const void* owner) {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& slot = registry()[owner];
    if (!slot) slot = std::make_shared<State>();
    return slot;
}

void clearStreamingState(State& state) {
    state.carrierPrimingSamples = 0;
    state.carrierPrimingSum = 0;
    state.oscillator = {1.0, 0.0};
    state.oscillatorSamples = 0;
    state.impulseMean = 0.0f;
    state.impulseLastGood = {};
    state.iqDc = {};
    state.iqResampler.reset();
    state.audioResampler.reset();
    state.channelDelay.assign(state.channelTaps.size(), {});
    state.channelWrite = 0;
    state.carrier = 1.0f;
    state.carrierValid = false;
    state.cwPhase = 0.0;
    state.agcEnvelope = 0.0f;
    state.agcGain = 1.0f;
    state.hpInput = state.hpOutput = 0.0f;
    state.lpOne = state.lpTwo = 0.0f;
    state.squelchGain = 0.0f;
    state.squelchHang = 0;
    state.clickFade = 0.0f;
    // AM envelope normalization needs a short acquisition interval while the
    // channel FIR fills. SSB and CW must not prepend silence: doing so changes
    // whole-block pitch/timing measurements and delays weak-signal decoder data.
    state.startupMuteSamples = state.mode == DemodMode::AM
        ? static_cast<int>(0.012 * kWorkRateHz) +
              static_cast<int>(state.channelTaps.size() / 2)
        : 0;
    state.decoderContinuous = false;
    state.explicitReset = false;
}

std::vector<std::complex<float>> designComplexBandpass(
    double rateHz, double lowHz, double highHz, int taps)
{
    if (!(rateHz > 0.0) || !(highHz > lowHz)) return {};
    taps = std::max(31, taps | 1);
    const int half = taps / 2;
    const double center = 0.5 * (lowHz + highHz);
    const double halfBandwidth = 0.5 * (highHz - lowHz);
    const double normalizedHalfBandwidth =
        std::clamp(halfBandwidth / rateHz, 1.0 / rateHz, 0.45);

    std::vector<std::complex<float>> result(static_cast<size_t>(taps));
    std::complex<double> response{};
    for (int index = 0; index < taps; ++index) {
        const int offset = index - half;
        const double window = blackman(static_cast<double>(offset) / std::max(1, half));
        const double lowpass =
            2.0 * normalizedHalfBandwidth *
            sinc(2.0 * normalizedHalfBandwidth * static_cast<double>(offset));
        const double angle = 2.0 * kPi * center * static_cast<double>(offset) / rateHz;
        const std::complex<double> tap =
            lowpass * window * std::complex<double>(std::cos(angle), std::sin(angle));
        result[static_cast<size_t>(index)] =
            {static_cast<float>(tap.real()), static_cast<float>(tap.imag())};
        const double responseAngle = -2.0 * kPi * center *
                                     static_cast<double>(index) / rateHz;
        response += tap * std::complex<double>(
            std::cos(responseAngle), std::sin(responseAngle));
    }

    const double gain = std::abs(response);
    if (gain > 1.0e-12) {
        for (auto& tap : result) tap /= static_cast<float>(gain);
    }
    return result;
}

std::vector<std::complex<float>> filterComplex(
    State& state, const std::vector<std::complex<float>>& samples)
{
    std::vector<std::complex<float>> output;
    output.resize(samples.size());
    const size_t count = state.channelTaps.size();
    if (count == 0 || samples.empty()) return samples;
    if (state.channelDelay.size() != count) {
        state.channelDelay.assign(count, {});
        state.channelWrite = 0;
    }

    for (size_t n = 0; n < samples.size(); ++n) {
        state.channelDelay[state.channelWrite] = samples[n];
        std::complex<float> accumulated{};
        size_t at = state.channelWrite;
        for (size_t k = 0; k < count; ++k) {
            accumulated += state.channelTaps[k] * state.channelDelay[at];
            at = at == 0 ? count - 1 : at - 1;
        }
        output[n] = accumulated;
        if (++state.channelWrite == count) state.channelWrite = 0;
    }
    return output;
}

std::vector<std::complex<float>> resampleComplex(
    SincResampler& state,
    const std::vector<std::complex<float>>& input,
    double inputRateHz,
    double outputRateHz)
{
    if (input.empty()) return {};
    if (std::abs(inputRateHz - outputRateHz) < 0.5) return input;

    // DEC-0111: tabulate the original sinc/window, not a weaker coarse
    // decimator. Interpolated fractional phases preserve its stopband.
    constexpr int phases = 1024;
    if (state.inputRate != inputRateHz || state.outputRate != outputRateHz) {
        state.inputRate = inputRateHz;
        state.outputRate = outputRateHz;
        state.half = std::clamp(static_cast<int>(std::ceil(
            8.0 * std::max(1.0, inputRateHz / outputRateHz))),
            kMinimumResamplerHalf, kMaximumResamplerHalf);
        state.reset();
        const int taps = state.half * 2;
        state.coefficients.resize(static_cast<size_t>(phases + 1) * taps);
        const double cutoff = 0.38 * std::min(1.0, outputRateHz / inputRateHz);
        for (int phase = 0; phase <= phases; ++phase) {
            double sum = 0.0;
            float* row = state.coefficients.data() + static_cast<size_t>(phase) * taps;
            for (int tap = 0; tap < taps; ++tap) {
                const double distance = static_cast<double>(phase) / phases -
                                        (tap - state.half + 1);
                row[tap] = static_cast<float>(2.0 * cutoff *
                    sinc(2.0 * cutoff * distance) * blackman(distance / state.half));
                sum += row[tap];
            }
            for (int tap = 0; tap < taps; ++tap) row[tap] /= static_cast<float>(sum);
        }
    }
    const int half = state.half;
    state.buffer.insert(state.buffer.end(), input.begin(), input.end());

    std::vector<std::complex<float>> output;
    output.reserve(static_cast<size_t>(
        std::ceil(input.size() * outputRateHz / inputRateHz)) + 2);

    const double step = inputRateHz / outputRateHz;
    while (state.position + half < static_cast<double>(state.buffer.size())) {
        const long center = static_cast<long>(std::floor(state.position));
        std::complex<double> accumulated{};
        const double fraction = (state.position - center) * phases;
        const int phase = std::min(static_cast<int>(fraction), phases - 1);
        const float blend = static_cast<float>(fraction - phase);
        const int taps = half * 2;
        const float* row = state.coefficients.data() + static_cast<size_t>(phase) * taps;
        const auto* samples = state.buffer.data() + center - half + 1;
        for (int tap = 0; tap < taps; ++tap) {
            const double coefficient = row[tap] + blend * (row[tap + taps] - row[tap]);
            accumulated += std::complex<double>(samples[tap].real(), samples[tap].imag()) * coefficient;
        }
        output.emplace_back(
            static_cast<float>(accumulated.real()),
            static_cast<float>(accumulated.imag()));
        state.position += step;
    }

    const long removable =
        static_cast<long>(std::floor(state.position)) -
        half;
    if (removable > 0) {
        const size_t drop = std::min(
            static_cast<size_t>(removable), state.buffer.size());
        state.buffer.erase(state.buffer.begin(),
            state.buffer.begin() + static_cast<std::ptrdiff_t>(drop));
        state.position -= static_cast<double>(drop);
    }

    return output;
}

double defaultAudioLowPass(DemodMode mode) noexcept {
    switch (mode) {
        case DemodMode::AM: return 5000.0;
        case DemodMode::CW: return 1100.0;
        case DemodMode::USB:
        case DemodMode::LSB: return 3000.0;
        default: return 3000.0;
    }
}

double defaultChannelBandwidth(DemodMode mode) noexcept {
    switch (mode) {
        case DemodMode::AM: return 10000.0;
        case DemodMode::CW: return 700.0;
        case DemodMode::USB:
        case DemodMode::LSB: return 6000.0;
        default: return 6000.0;
    }
}

bool materiallyDifferent(double left, double right, double tolerance) noexcept {
    return !std::isfinite(left) || !std::isfinite(right) ||
           std::abs(left - right) > tolerance;
}

} // namespace

bool supports(DemodMode mode) noexcept {
    return mode == DemodMode::AM || mode == DemodMode::USB ||
           mode == DemodMode::LSB || mode == DemodMode::CW;
}

std::vector<float> demodulate(
    const void* owner,
    const std::vector<std::complex<float>>& iq,
    double inputRateHz,
    double centerHz,
    double targetHz,
    DemodMode mode,
    double& rmsOutDb,
    double audioLowPassHz,
    double squelchDb,
    double audioGain,
    double channelBandwidthHz,
    size_t targetAudioSamples,
    double outputRateHz,
    double externalSquelchLevelDb,
    bool audioLowPassEnabled,
    FmMultiplexBlock* decoderAudio,
    double dataIdentityHz)
{
    if (decoderAudio) *decoderAudio = {};
    rmsOutDb = -140.0;
    if (!owner || !supports(mode) || iq.empty() ||
        !(inputRateHz > 0.0) || !std::isfinite(inputRateHz) ||
        !std::isfinite(centerHz) || !std::isfinite(targetHz)) {
        return {};
    }

    if (!(outputRateHz > 0.0) || !std::isfinite(outputRateHz))
        outputRateHz = kWorkRateHz;
    if (!(audioLowPassHz > 0.0) || !std::isfinite(audioLowPassHz))
        audioLowPassHz = defaultAudioLowPass(mode);
    if (!(channelBandwidthHz > 0.0) || !std::isfinite(channelBandwidthHz))
        channelBandwidthHz = defaultChannelBandwidth(mode);

    // Treat stale settings from a previous wideband mode as invalid rather than
    // feeding a 180 kHz WFM profile into an HF voice channel.
    if (mode == DemodMode::USB || mode == DemodMode::LSB) {
        if (audioLowPassHz > 5000.0) audioLowPassHz = 3000.0;
        if (channelBandwidthHz > 12000.0) channelBandwidthHz = 6000.0;
        audioLowPassHz = std::clamp(
            audioLowPassHz, 1200.0, std::min(5000.0, outputRateHz * 0.42));
        channelBandwidthHz = std::clamp(
            channelBandwidthHz, 2800.0, std::min(12000.0, inputRateHz * 0.80));
    } else if (mode == DemodMode::CW) {
        if (audioLowPassHz > 2000.0) audioLowPassHz = 1100.0;
        if (channelBandwidthHz > 3000.0) channelBandwidthHz = 700.0;
        audioLowPassHz = std::clamp(
            audioLowPassHz, 500.0, std::min(1800.0, outputRateHz * 0.40));
        channelBandwidthHz = std::clamp(
            channelBandwidthHz, 200.0, std::min(3000.0, inputRateHz * 0.70));
    } else {
        if (audioLowPassHz > 12000.0) audioLowPassHz = 5000.0;
        if (channelBandwidthHz > 30000.0) channelBandwidthHz = 10000.0;
        audioLowPassHz = std::clamp(
            audioLowPassHz, 1800.0, std::min(10000.0, outputRateHz * 0.42));
        channelBandwidthHz = std::clamp(
            channelBandwidthHz, 4000.0, std::min(30000.0, inputRateHz * 0.80));
    }
    audioGain = std::clamp(audioGain, 0.0, 20.0);

    const double workRateHz = std::min(kWorkRateHz, inputRateHz);
    auto state = stateFor(owner);
    std::lock_guard<std::mutex> stateLock(state->mutex);

    const bool configurationChanged =
        !state->configured || state->explicitReset ||
        state->mode != mode ||
        materiallyDifferent(state->inputRate, inputRateHz, 0.5) ||
        materiallyDifferent(state->centerHz, centerHz, 0.5) ||
        materiallyDifferent(state->targetHz, targetHz, 0.5) ||
        materiallyDifferent(
            state->channelBandwidthHz, channelBandwidthHz, 0.5) ||
        materiallyDifferent(state->audioLowPassHz, audioLowPassHz, 0.5) ||
        materiallyDifferent(state->outputRate, outputRateHz, 0.5);

    if (configurationChanged) {
        state->configured = true;
        state->mode = mode;
        state->inputRate = inputRateHz;
        state->centerHz = centerHz;
        state->targetHz = targetHz;
        state->channelBandwidthHz = channelBandwidthHz;
        state->audioLowPassHz = audioLowPassHz;
        state->outputRate = outputRateHz;

        const double nyquist = workRateHz * 0.5;
        if (mode == DemodMode::USB || mode == DemodMode::LSB) {
            const double low = std::min(120.0, audioLowPassHz * 0.20);
            const double high = std::clamp(
                std::min(audioLowPassHz, channelBandwidthHz * 0.5),
                low + 300.0, nyquist * 0.86);
            state->channelTaps = mode == DemodMode::USB
                ? designComplexBandpass(workRateHz, low, high, 257)
                : designComplexBandpass(workRateHz, -high, -low, 257);
        } else if (mode == DemodMode::CW) {
            const double halfWidth = std::clamp(
                channelBandwidthHz * 0.5, 100.0, 900.0);
            state->channelTaps = designComplexBandpass(
                workRateHz, -halfWidth, halfWidth, 401);
        } else {
            const double halfWidth = std::clamp(
                channelBandwidthHz * 0.5, 1200.0, nyquist * 0.86);
            state->channelTaps = designComplexBandpass(
                workRateHz, -halfWidth, halfWidth, 257);
        }
        clearStreamingState(*state);
    }

    const double phaseStep =
        2.0 * kPi * (targetHz - centerHz) / inputRateHz;
    const std::complex<double> oscillatorStep(std::cos(phaseStep), -std::sin(phaseStep));
    const float dcAlpha = static_cast<float>(
        1.0 - std::exp(-2.0 * kPi * 2.0 / inputRateHz));
    const float impulseAlpha = static_cast<float>(
        1.0 - std::exp(-1.0 / std::max(1.0, 0.050 * inputRateHz)));

    std::vector<std::complex<float>> mixed;
    mixed.reserve(iq.size());
    for (const auto& inputSample : iq) {
        const std::complex<float> oscillator(state->oscillator);
        std::complex<float> sample = inputSample * oscillator;
        state->oscillator *= oscillatorStep;
        // Bound floating-point recurrence drift independently of input chunks.
        if (++state->oscillatorSamples == 4096) {
            state->oscillator /= std::abs(state->oscillator);
            state->oscillatorSamples = 0;
        }

        const float magnitude = std::abs(sample);
        if (state->impulseMean <= 1.0e-7f) {
            state->impulseMean = std::max(magnitude, 1.0e-7f);
            state->impulseLastGood = sample;
        }
        const float threshold = std::max(
            state->impulseMean * 8.0f, state->impulseMean + 0.15f);
        if (magnitude > threshold && state->impulseMean > 1.0e-5f) {
            sample = state->impulseLastGood * 0.35f;
        } else {
            state->impulseLastGood = sample;
        }
        // A01: rejected samples still inform the level estimate. Otherwise a
        // sustained stronger carrier remains classified as an impulse forever.
        state->impulseMean += impulseAlpha * (magnitude - state->impulseMean);

        if (mode == DemodMode::USB || mode == DemodMode::LSB) {
            state->iqDc += dcAlpha * (sample - state->iqDc);
            sample -= state->iqDc;
        }
        mixed.push_back(sample);
    }

    auto atWorkRate = resampleComplex(
        state->iqResampler, mixed, inputRateHz, workRateHz);
    if (atWorkRate.empty()) return {};

    auto channel = filterComplex(*state, atWorkRate);
    if (channel.empty()) return {};

    double channelPower = 0.0;
    for (const auto& sample : channel) channelPower += std::norm(sample);
    const double channelLevelDb = 10.0 * std::log10(
        channelPower / static_cast<double>(channel.size()) + 1.0e-20);
    const double squelchMetricDb = std::isfinite(externalSquelchLevelDb)
        ? externalSquelchLevelDb : channelLevelDb;
    rmsOutDb = squelchMetricDb;

    std::vector<float> audio(channel.size());
    if (mode == DemodMode::AM) {

        const float carrierAlpha = static_cast<float>(
            1.0 - std::exp(-2.0 * kPi * 4.0 / workRateHz));
        for (size_t index = 0; index < channel.size(); ++index) {
            const float envelope = std::abs(channel[index]);
            if (!state->carrierValid) {
                // DEC-0113: prime on the sample clock, not callback boundaries.
                const size_t settle = state->channelTaps.size();
                if (state->carrierPrimingSamples >= settle) state->carrierPrimingSum += envelope;
                if (++state->carrierPrimingSamples >= settle * 2) {
                    state->carrier = std::max(1.0e-5f, static_cast<float>(state->carrierPrimingSum / settle));
                    state->carrierValid = true;
                }
                audio[index] = 0;
                continue;
            }
            state->carrier +=
                carrierAlpha * (envelope - state->carrier);
            audio[index] =
                (envelope - state->carrier) /
                std::max(state->carrier, 1.0e-5f);
        }
    } else if (mode == DemodMode::CW) {
        constexpr double pitchHz = 700.0;
        const double step = 2.0 * kPi * pitchHz / workRateHz;
        for (size_t index = 0; index < channel.size(); ++index) {
            const std::complex<float> bfo(
                static_cast<float>(std::cos(state->cwPhase)),
                static_cast<float>(std::sin(state->cwPhase)));
            audio[index] = std::real(channel[index] * bfo);
            state->cwPhase =
                std::remainder(state->cwPhase + step, 2.0 * kPi);
        }
    } else {
        for (size_t index = 0; index < channel.size(); ++index)
            audio[index] = 2.0f * std::real(channel[index]);
    }

    if (mode == DemodMode::USB ||
        mode == DemodMode::LSB ||
        mode == DemodMode::CW) {
        const float envelopeAttack = static_cast<float>(
            1.0 - std::exp(-1.0 / (0.002 * workRateHz)));
        const float envelopeRelease = static_cast<float>(
            1.0 - std::exp(-1.0 / (0.120 * workRateHz)));
        const float reduceGain = static_cast<float>(
            1.0 - std::exp(-1.0 / (0.003 * workRateHz)));
        constexpr float targetLevel = 0.28f;

        for (auto& sample : audio) {
            const float level = std::abs(sample);
            const float envelopeCoefficient =
                level > state->agcEnvelope
                    ? envelopeAttack : envelopeRelease;
            state->agcEnvelope +=
                envelopeCoefficient * (level - state->agcEnvelope);
            const float desired = std::clamp(
                targetLevel / std::max(state->agcEnvelope, 0.003f),
                0.03f, 50.0f);
            const float gainCoefficient =
                desired < state->agcGain ? reduceGain : 1.0f;
            // The envelope already owns the 120 ms release. A second slow
            // gain-release integrator compounds recovery after real overload.
            state->agcGain +=
                gainCoefficient * (desired - state->agcGain);
            sample *= state->agcGain;
        }
    }

    const double highPassHz =
        mode == DemodMode::CW ? 250.0 :
        (mode == DemodMode::AM ? 30.0 : 80.0);
    const float hpPole = static_cast<float>(
        std::exp(-2.0 * kPi * highPassHz / workRateHz));
    const float lpAlpha = static_cast<float>(
        1.0 - std::exp(
            -2.0 * kPi * std::min(audioLowPassHz, workRateHz * 0.44) /
            workRateHz));

    for (auto& sample : audio) {
        const float highPassed =
            hpPole * (state->hpOutput + sample - state->hpInput);
        state->hpInput = sample;
        state->hpOutput = highPassed;
        sample = highPassed;

        if (audioLowPassEnabled) {
            state->lpOne += lpAlpha * (sample - state->lpOne);
            state->lpTwo += lpAlpha * (state->lpOne - state->lpTwo);
            sample = state->lpTwo;
        }
    }

    const std::vector<float> decoderSource = audio;

    if (decoderAudio && (mode == DemodMode::USB ||
                         mode == DemodMode::LSB)) {
        const double identity = std::isfinite(dataIdentityHz)
            ? dataIdentityHz : targetHz;
        const bool discontinuity =
            !state->decoderContinuous ||
            state->decoderMode != mode ||
            materiallyDifferent(state->decoderRate, workRateHz, 0.5) ||
            materiallyDifferent(state->decoderIdentityHz, identity, 0.5) ||
            configurationChanged;
        if (discontinuity) {
            ++state->decoderEpoch;
            state->decoderSamples = 0;
        }
        decoderAudio->samples = decoderSource;
        decoderAudio->sampleRate = workRateHz;
        decoderAudio->targetHz = identity;
        decoderAudio->epoch = state->decoderEpoch;
        decoderAudio->firstSample = state->decoderSamples;
        decoderAudio->discontinuity = discontinuity;
        decoderAudio->resetReasons = discontinuity ? 1u : 0u;
        state->decoderSamples += decoderAudio->samples.size();
        state->decoderContinuous = true;
        state->decoderMode = mode;
        state->decoderRate = workRateHz;
        state->decoderIdentityHz = identity;
    } else {
        state->decoderContinuous = false;
    }

    const bool squelchDisabled = squelchDb <= -115.0;
    const float targetGate =
        (squelchDisabled || squelchMetricDb > squelchDb) ? 1.0f : 0.0f;
    const int hangSamples = static_cast<int>(0.080 * workRateHz);
    if (targetGate > 0.5f) {
        state->squelchHang = hangSamples;
    }

    const float gateAttack = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.004 * workRateHz)));
    const float gateRelease = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.025 * workRateHz)));
    for (auto& sample : audio) {
        float gate = targetGate;
        if (gate < 0.5f && state->squelchHang > 0) {
            gate = 1.0f;
            --state->squelchHang;
        }
        const float coefficient =
            gate > state->squelchGain ? gateAttack : gateRelease;
        state->squelchGain +=
            coefficient * (gate - state->squelchGain);
        state->squelchGain =
            std::clamp(state->squelchGain, 0.0f, 1.0f);

        if (state->startupMuteSamples > 0) {
            --state->startupMuteSamples;
            sample = 0.0f;
            continue;
        }

        state->clickFade +=
            static_cast<float>(
                1.0 - std::exp(-1.0 / (0.008 * workRateHz))) *
            (1.0f - state->clickFade);
        sample *= static_cast<float>(audioGain) *
                  state->squelchGain * state->clickFade;
        sample = std::clamp(sample, -0.98f, 0.98f);
    }

    // DEC-0111: the input clock owns the output count. A legacy per-block
    // requested length must not stretch speech or duplicate endpoints.
    (void)targetAudioSamples;
    if (std::abs(outputRateHz - workRateHz) < 0.5) return audio;
    std::vector<std::complex<float>> audioInput;
    audioInput.reserve(audio.size());
    for (float sample : audio) audioInput.emplace_back(sample, 0.0f);
    const auto converted = resampleComplex(state->audioResampler, audioInput, workRateHz, outputRateHz);
    audio.resize(converted.size());
    for (size_t i = 0; i < converted.size(); ++i) audio[i] = converted[i].real();
    return audio;
}

void reset(const void* owner) noexcept {
    if (!owner) return;
    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        auto& states = registry();
        const auto found = states.find(owner);
        if (found == states.end()) return;
        state = found->second;
    }
    std::lock_guard<std::mutex> stateLock(state->mutex);
    state->explicitReset = true;
}

void release(const void* owner) noexcept {
    if (!owner) return;
    std::lock_guard<std::mutex> lock(registryMutex());
    registry().erase(owner);
}

} // namespace HfDemod
