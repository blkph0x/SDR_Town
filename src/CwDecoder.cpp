#include "CwDecoder.h"
#include "ggmorse/ggmorse.h"
#include "miniaudio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

struct CwDecoder::State {
    ma_resampler resampler{};
    bool initialized = false;
    double rate = 0;
    uint64_t epoch = 0, next = 0;
    std::unique_ptr<GGMorse> decoder;
    std::vector<float> pending;
    ~State() { if (initialized) ma_resampler_uninit(&resampler, nullptr); }
};

CwDecoder::CwDecoder(CwOptions options) : options_(options) {
    if (!std::isfinite(options.pitchHz) || !std::isfinite(options.speedWpm) ||
        (options.pitchHz != 0 && (options.pitchHz < 200 || options.pitchHz > 1200)) ||
        (options.speedWpm != 0 && (options.speedWpm < 5 || options.speedWpm > 55)))
        throw std::invalid_argument("CW pitch must be Auto or 200..1200 Hz; speed Auto or 5..55 WPM");
}
CwDecoder::~CwDecoder() = default;

void CwDecoder::process(std::span<const float> audio, double rate, uint64_t epoch,
                       uint64_t firstSample, bool discontinuity) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000 || audio.size() > 262144 ||
        firstSample > std::numeric_limits<uint64_t>::max() - audio.size() ||
        !std::all_of(audio.begin(), audio.end(), [](float x) {return std::isfinite(x) && std::abs(x) <= 16;})) {
        state_.reset();
        ++snapshot_.rejected;
        throw std::invalid_argument("Invalid CW audio block");
    }
    if (audio.empty()) return;
    const auto begin = std::chrono::steady_clock::now();
    if (!state_ || discontinuity || state_->rate != rate || state_->epoch != epoch || state_->next != firstSample) {
        state_ = std::make_unique<State>();
        state_->rate = rate; state_->epoch = epoch;
        // DEC-0167: filtered 4 kHz conversion, not upstream's integer boxcar.
        // Scale rates to retain non-integer SDR discriminator clocks (<=0.0001 Hz).
        auto config = ma_resampler_config_init(ma_format_f32, 1,
            ma_uint32(std::llround(rate * 10000)), 4000 * 10000, ma_resample_algorithm_linear);
        config.linear.lpfOrder = 8;
        if (ma_resampler_init(&config, nullptr, &state_->resampler) != MA_SUCCESS)
            throw std::runtime_error("CW audio converter failed");
        state_->initialized = true;
        state_->decoder = std::make_unique<GGMorse>(GGMorse::getDefaultParameters());
        auto parameters = GGMorse::getDefaultParametersDecode();
        parameters.frequency_hz = options_.pitchHz == 0 ? -1 : options_.pitchHz;
        parameters.speed_wpm = options_.speedWpm == 0 ? -1 : options_.speedWpm;
        state_->decoder->setParametersDecode(parameters);
        ++snapshot_.resets;
        if (!snapshot_.text.empty() && snapshot_.text.back() != '\n') snapshot_.text += '\n';
    }
    std::vector<float> converted(audio.size() / 2 + 64); // At least 2:1 decimation; phase carry bounded.
    ma_uint64 in = audio.size(), out = converted.size();
    if (ma_resampler_process_pcm_frames(&state_->resampler, audio.data(), &in, converted.data(), &out) != MA_SUCCESS ||
        in != audio.size() || out > converted.size()) {
        state_.reset(); ++snapshot_.rejected;
        throw std::runtime_error("CW audio conversion failed");
    }
    state_->pending.insert(state_->pending.end(), converted.begin(), converted.begin() + size_t(out));
    size_t consumed = 0;
    state_->decoder->decode([&](void* destination, uint32_t bytes) -> uint32_t {
        const size_t count = bytes / sizeof(float);
        if (bytes % sizeof(float) || count > state_->pending.size() - consumed) return 0;
        std::memcpy(destination, state_->pending.data() + consumed, bytes);
        consumed += count;
        return bytes;
    });
    state_->pending.erase(state_->pending.begin(), state_->pending.begin() + consumed);
    GGMorse::TxRx text;
    state_->decoder->takeRxData(text);
    for (const auto c : text) if (c >= 32 && c < 127) snapshot_.text += char(c);
    constexpr size_t historyLimit = 16384; // DEC-0167 UI memory policy, not protocol timing.
    if (snapshot_.text.size() > historyLimit) snapshot_.text.erase(0, snapshot_.text.size() - historyLimit);
    const auto stats = state_->decoder->getStatistics();
    snapshot_.pitchHz = std::isfinite(stats.estimatedPitch_Hz) ? stats.estimatedPitch_Hz : 0;
    snapshot_.speedWpm = std::isfinite(stats.estimatedSpeed_wpm) ? stats.estimatedSpeed_wpm : 0;
    snapshot_.samples += audio.size(); ++snapshot_.blocks;
    snapshot_.processingMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    state_->next = firstSample + audio.size();
}
