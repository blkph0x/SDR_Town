#include "DtmfDecoder.h"
#include "miniaudio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace {
constexpr double kHz[]{697, 770, 852, 941, 1209, 1336, 1477, 1633};
constexpr char kPad[]{'1','2','3','A','4','5','6','B','7','8','9','C','*','0','#','D'};
constexpr double kDominance = 6.309573444801933; // 8 dB, DEC-0168 / SpanDSP.
constexpr size_t kHistory = 64, kSequence = 32; // Bounded observer policy, DEC-0168.

double power(std::span<const double> samples, double coeff) {
    double prev = 0, prev2 = 0;
    for (double x : samples) {
        const double curr = x + coeff * prev - prev2;
        prev2 = prev; prev = curr;
    }
    return std::max(0.0, prev * prev + prev2 * prev2 - coeff * prev * prev2);
}
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
double transformed(double hz, const DtmfOptions& options) {
    const double shifted = hz * options.pitchScale + options.shiftHz;
    return options.inverted ? options.inversionHz - shifted : shifted;
}
void validate(const DtmfOptions& o) {
    if (!std::isfinite(o.pitchScale) || o.pitchScale < .8 || o.pitchScale > 1.2 ||
        !std::isfinite(o.shiftHz) || std::abs(o.shiftHz) > 500 ||
        !std::isfinite(o.inversionHz) || o.inversionHz < 2000 || o.inversionHz > 6000)
        throw std::invalid_argument("DTMF requires scale 0.8..1.2, shift +/-500 Hz, inversion 2000..6000 Hz");
}
}

DtmfDecoder::DtmfDecoder(DtmfOptions options) { setOptions(options); }
void DtmfDecoder::setOptions(DtmfOptions options) {
    validate(options);
    std::lock_guard lock(mutex_); requested_ = options;
}
DtmfOptions DtmfDecoder::options() const { std::lock_guard lock(mutex_); return requested_; }
void DtmfDecoder::publish() { std::lock_guard lock(mutex_); published_ = state_; }
DtmfSnapshot DtmfDecoder::snapshot() const { std::lock_guard lock(mutex_); return published_; }
std::vector<DtmfDecoder::PendingEvent> DtmfDecoder::takePendingEvents() {
    std::lock_guard lock(mutex_);
    std::vector<PendingEvent> out; out.swap(pending_); return out;
}
void DtmfDecoder::queue(PendingEvent event) {
    std::lock_guard lock(mutex_);
    if (pending_.size() == kHistory) { pending_.erase(pending_.begin()); ++state_.droppedEvents; }
    pending_.push_back(std::move(event));
}
void DtmfDecoder::finishSequence(int64_t ms) {
    if (state_.sequence.empty()) return;
    state_.lastSequence = state_.sequence;
    state_.lastSequenceMs = ms;
    queue({PendingEvent::Kind::Sequence, 0, state_.sequence, ms, state_.targetHz, {}});
    state_.sequence.clear();
}
void DtmfDecoder::finish() {
    finishSequence(nowMs());
    state_.toneActive = false;
    state_.status = "Finished";
    window_.clear(); frameLen_ = 0;
    publish();
}
void DtmfDecoder::reset() {
    finishSequence(nowMs());
    const auto resets = state_.resets;
    const auto dropped = state_.droppedEvents;
    state_ = {}; state_.resets = resets; state_.droppedEvents = dropped;
    window_.clear(); centered_.clear();
    candidate_ = activeDigit_ = 0;
    candidateSamples_ = silenceSamples_ = frameLen_ = hopLen_ = 0;
    nextSample_ = candidateStart_ = 0;
    publish();
}

bool DtmfDecoder::process(std::span<const float> samples, double rate, double target,
                          uint64_t epoch, uint64_t first, bool gap) {
    const auto started = std::chrono::steady_clock::now();
    const auto requested = options();
    bool valid = std::isfinite(rate) && rate >= 8000 && rate <= 96000 && std::isfinite(target) &&
        samples.size() <= 262144 && first <= std::numeric_limits<uint64_t>::max() - samples.size() &&
        std::all_of(samples.begin(), samples.end(), [](float x) {return std::isfinite(x);});
    for (double hz : kHz) for (double scale : {.985, 1.0, 1.015}) {
        const auto f = transformed(hz * scale, requested);
        valid = valid && f > 40 && f < rate * .49;
    }
    if (!valid) {
        reset(); state_.status = "Invalid DTMF input or transformed tones outside Nyquist (8-96 kHz required)";
        publish(); return false;
    }
    if (!frameLen_ || gap || epoch != epoch_ || first != nextSample_ ||
        state_.sampleRate != rate || state_.targetHz != target || requested != active_) {
        reset(); ++state_.resets;
        active_ = requested; state_.options = active_;
        state_.sampleRate = rate; state_.targetHz = target;
        state_.status = "Searching DTMF"; epoch_ = epoch;
        // DEC-0168: explicit short-burst profile, never inferred from speech/noise.
        frameLen_ = static_cast<size_t>(std::llround(rate * (active_.fast ? .0125 : .020)));
        hopLen_ = static_cast<size_t>(std::llround(rate * .0025));
        window_.reserve(frameLen_); centered_.resize(frameLen_);
        for (size_t i = 0; i < 8; ++i) for (size_t j = 0; j < 3; ++j) {
            frequencies_[i][j] = transformed(kHz[i] * (.985 + .015 * j), active_);
            coefficients_[i][j] = 2 * std::cos(2 * std::numbers::pi * frequencies_[i][j] / rate);
        }
    }
    uint64_t cursor = first;
    for (float sample : samples) {
        window_.push_back(sample); ++cursor;
        if (window_.size() == frameLen_) {
            evaluateFrame(cursor);
            window_.erase(window_.begin(), window_.begin() + static_cast<std::ptrdiff_t>(hopLen_));
        }
    }
    state_.samples += samples.size(); nextSample_ = first + samples.size();
    if (!samples.empty()) state_.updatedMs = nowMs();
    state_.processingUs += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    publish(); return true;
}

void DtmfDecoder::evaluateFrame(uint64_t frameEnd) {
    ++state_.frames;
    const double mean = std::accumulate(window_.begin(), window_.end(), 0.0) / frameLen_;
    double energy = 0;
    for (size_t i = 0; i < frameLen_; ++i) {
        const double x = window_[i] - mean;
        centered_[i] = x; energy += x * x;
    }
    std::array<double, 8> energies{}, frequency{};
    for (size_t i = 0; i < 8; ++i) for (size_t j = 0; j < 3; ++j) {
        const double p = power(centered_, coefficients_[i][j]);
        if (p > energies[i]) {energies[i] = p; frequency[i] = frequencies_[i][j];}
    }
    const auto row = std::max_element(energies.begin(), energies.begin() + 4) - energies.begin();
    const auto col = std::max_element(energies.begin() + 4, energies.end()) - energies.begin();
    double rowSecond = 0, colSecond = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (i != row) rowSecond = std::max(rowSecond, energies[i]);
        if (i + 4 != col) colSecond = std::max(colSecond, energies[i + 4]);
    }
    const double pair = energies[row] + energies[col];
    // Rectangular DFT amplitude scales with N; energy scales with N, not N^2.
    state_.purity = energy > 0 ? std::clamp(2 * pair / (frameLen_ * energy), 0.0, 1.0) : 0;
    state_.twistDb = 10 * std::log10((energies[col] + 1e-30) / (energies[row] + 1e-30));
    state_.rowHz = frequency[row]; state_.columnHz = frequency[col];
    state_.rejection.clear();
    if (energy / frameLen_ < 1e-12) state_.rejection = "below_numeric_floor";
    else if (state_.purity < .78) state_.rejection = "pair_purity";
    else if (energies[row] < kDominance * rowSecond || energies[col] < kDominance * colSecond)
        state_.rejection = "competing_tones";
    else if (state_.twistDb < -8 || state_.twistDb > 4) state_.rejection = "twist";
    const char digit = state_.rejection.empty() ? kPad[row * 4 + col - 4] : 0;
    const auto ms = nowMs();
    if (digit) {
        silenceSamples_ = 0;
        if (candidate_ != digit) {
            candidate_ = digit; candidateStart_ = frameEnd - frameLen_; candidateSamples_ = frameLen_;
        } else candidateSamples_ = std::min(candidateSamples_ + hopLen_, frameLen_ * 4);
        const auto required = static_cast<size_t>(std::llround(state_.sampleRate * (active_.fast ? .0175 : .0375)));
        if (candidateSamples_ >= required) {
            state_.toneActive = true; state_.digit = digit; state_.status = std::string("DTMF ") + digit;
            if (activeDigit_ != digit) {
                activeDigit_ = digit;
                if (state_.sequence.size() < kSequence) state_.sequence.push_back(digit);
                else ++state_.truncatedDigits;
                ++state_.confirmedDigits; state_.lastDigitMs = ms;
                DtmfDetection event{digit, epoch_, candidateStart_, frameEnd, state_.sampleRate,
                    state_.targetHz, frequency[row], frequency[col], state_.purity, state_.twistDb, active_};
                if (state_.history.size() == kHistory) state_.history.erase(state_.history.begin());
                state_.history.push_back(event);
                queue({PendingEvent::Kind::Digit, digit, state_.sequence, ms, state_.targetHz, event});
            }
        } else {state_.toneActive = false; state_.rejection = "duration";}
    } else {
        ++state_.rejectedFrames; candidate_ = 0; candidateSamples_ = 0; state_.toneActive = false;
        silenceSamples_ = std::min(silenceSamples_ + hopLen_, static_cast<size_t>(state_.sampleRate));
        if (silenceSamples_ >= static_cast<size_t>(std::llround(state_.sampleRate * (active_.fast ? .0075 : .020)))) {
            activeDigit_ = 0;
            state_.status = state_.sequence.empty() ? "Searching DTMF" : ("Sequence " + state_.sequence);
        }
        if (silenceSamples_ >= static_cast<size_t>(std::llround(state_.sampleRate * .300))) finishSequence(ms);
    }
}

DtmfSnapshot decodeDtmfFile(const std::string& path, size_t chunkSize,
                            DtmfOptions options, const std::function<bool()>& cancelled) {
    if (!chunkSize || chunkSize > 8192) throw std::invalid_argument("Tone chunk must be 1..8192 samples");
    DtmfDecoder decoder(options);
    ma_decoder reader{};
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
#ifdef _WIN32
    const auto opened = ma_decoder_init_file_w(std::filesystem::u8path(path).c_str(), &config, &reader);
#else
    const auto opened = ma_decoder_init_file(path.c_str(), &config, &reader);
#endif
    if (opened != MA_SUCCESS) throw std::runtime_error("Cannot open DTMF recording");
    struct Guard { ma_decoder* reader; ~Guard() {ma_decoder_uninit(reader);} } guard{&reader};
    if (reader.outputChannels != 1 || reader.outputSampleRate < 8000 || reader.outputSampleRate > 96000)
        throw std::runtime_error("DTMF recording must be mono at 8..96 kHz");
    std::vector<float> samples(chunkSize);
    uint64_t first = 0;
    for (;;) {
        if (cancelled && cancelled()) throw std::runtime_error("DTMF recording analysis cancelled");
        ma_uint64 count = 0;
        const auto result = ma_decoder_read_pcm_frames(&reader, samples.data(), chunkSize, &count);
        if (result != MA_SUCCESS && result != MA_AT_END) throw std::runtime_error("DTMF recording read failed");
        if (!count) break;
        if (first + count > uint64_t{reader.outputSampleRate} * 120)
            throw std::runtime_error("DTMF recording exceeds 120 seconds");
        if (!decoder.process(std::span(samples.data(), static_cast<size_t>(count)),
                             reader.outputSampleRate, 0, 1, first, first == 0))
            throw std::runtime_error(decoder.snapshot().status);
        first += count;
    }
    decoder.finish(); return decoder.snapshot();
}
