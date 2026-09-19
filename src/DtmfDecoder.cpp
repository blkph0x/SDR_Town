#include "DtmfDecoder.h"
#include "miniaudio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
constexpr double kRowHz[]{697.0, 770.0, 852.0, 941.0};
constexpr double kColHz[]{1209.0, 1336.0, 1477.0, 1633.0};
constexpr char kPad[4][4]{
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

double goertzelPower(std::span<const float> samples, double rate, double frequency)
{
    const double coeff = 2.0 * std::cos(2.0 * std::numbers::pi * frequency / rate);
    double prev = 0, prev2 = 0;
    for (float sample : samples) {
        const double x = static_cast<double>(sample);
        const double curr = x + coeff * prev - prev2;
        prev2 = prev;
        prev = curr;
    }
    return std::max(0.0, prev * prev + prev2 * prev2 - coeff * prev * prev2);
}

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void DtmfDecoder::publish()
{
    std::lock_guard lock(mutex_);
    published_ = state_;
}

DtmfSnapshot DtmfDecoder::snapshot() const
{
    std::lock_guard lock(mutex_);
    return published_;
}

std::vector<DtmfDecoder::PendingEvent> DtmfDecoder::takePendingEvents()
{
    std::lock_guard lock(mutex_);
    std::vector<PendingEvent> out;
    out.swap(pending_);
    return out;
}

void DtmfDecoder::finishSequence(int64_t ms)
{
    if (state_.sequence.empty()) return;
    state_.lastSequence = state_.sequence;
    state_.lastSequenceMs = ms;
    pending_.push_back({PendingEvent::Kind::Sequence, 0, state_.sequence, ms});
    if (pending_.size() > 64) pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(pending_.size() - 64));
    state_.sequence.clear();
}

void DtmfDecoder::reset()
{
    const auto resets = state_.resets;
    finishSequence(nowMs());
    state_ = {};
    state_.resets = resets;
    window_.clear();
    bandpass_ = {};
    dc_ = 0;
    candidate_ = 0;
    candidateHits_ = 0;
    activeDigit_ = 0;
    silenceFrames_ = 0;
    frameLen_ = hopLen_ = 0;
    nextSample_ = 0;
    publish();
}

bool DtmfDecoder::process(std::span<const float> samples, double rate, double target,
                          uint64_t epoch, uint64_t first, bool gap)
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 96000 || !std::isfinite(target) ||
        samples.size() > 262144 || first > std::numeric_limits<uint64_t>::max() - samples.size() ||
        !std::all_of(samples.begin(), samples.end(), [](float x) { return std::isfinite(x); })) {
        reset();
        state_.status = "Invalid DTMF input (8-96 kHz required)";
        publish();
        return false;
    }

    if (!frameLen_ || gap || epoch != epoch_ || first != nextSample_ ||
        state_.sampleRate != rate || state_.targetHz != target) {
        const auto resets = state_.resets;
        finishSequence(nowMs());
        state_ = {};
        state_.resets = resets + 1;
        state_.sampleRate = rate;
        state_.targetHz = target;
        state_.status = "Searching DTMF";
        epoch_ = epoch;
        window_.clear();
        bandpass_ = {};
        dc_ = 0;
        candidate_ = 0;
        candidateHits_ = 0;
        activeDigit_ = 0;
        silenceFrames_ = 0;
        // ~20 ms analysis, 50% hop — robust against short bursts without burning CPU.
        frameLen_ = static_cast<size_t>(std::clamp(std::llround(rate * 0.020), 160LL, 1920LL));
        hopLen_ = std::max<size_t>(1, frameLen_ / 2);
        dcAlpha_ = 1.0 - std::exp(-2.0 * std::numbers::pi * 40.0 / rate);
        window_.reserve(frameLen_);
    }

    // Mild band-emphasize around DTMF (1st-order HP ~300 Hz + LP ~2 kHz cascade).
    const double hpA = 1.0 - std::exp(-2.0 * std::numbers::pi * 300.0 / rate);
    const double lpA = 1.0 - std::exp(-2.0 * std::numbers::pi * 2000.0 / rate);

    for (float sample : samples) {
        dc_ += dcAlpha_ * (static_cast<double>(sample) - dc_);
        double x = static_cast<double>(sample) - dc_;
        bandpass_[0] += hpA * (x - bandpass_[0]);
        x -= bandpass_[0];
        bandpass_[1] += lpA * (x - bandpass_[1]);
        x = bandpass_[1];
        bandpass_[2] += lpA * (x - bandpass_[2]);
        x = bandpass_[2];
        window_.push_back(static_cast<float>(x));
        while (window_.size() >= frameLen_) {
            evaluateFrame();
            if (hopLen_ >= window_.size()) window_.clear();
            else window_.erase(window_.begin(), window_.begin() + static_cast<std::ptrdiff_t>(hopLen_));
        }
    }

    state_.samples += samples.size();
    nextSample_ = first + samples.size();
    if (!samples.empty()) state_.updatedMs = nowMs();
    publish();
    return true;
}

void DtmfDecoder::evaluateFrame()
{
    ++state_.frames;
    if (window_.size() < frameLen_) return;

    std::span<const float> frame(window_.data(), frameLen_);
    double energy = 0;
    for (float s : frame) energy += static_cast<double>(s) * s;
    energy = std::max(energy, 1e-20);

    double rowP[4]{}, colP[4]{};
    int bestRow = -1, bestCol = -1;
    double rowBest = 0, rowSecond = 0, colBest = 0, colSecond = 0;
    for (int i = 0; i < 4; ++i) {
        rowP[i] = goertzelPower(frame, state_.sampleRate, kRowHz[i]);
        if (rowP[i] > rowBest) { rowSecond = rowBest; rowBest = rowP[i]; bestRow = i; }
        else rowSecond = std::max(rowSecond, rowP[i]);
        colP[i] = goertzelPower(frame, state_.sampleRate, kColHz[i]);
        if (colP[i] > colBest) { colSecond = colBest; colBest = colP[i]; bestCol = i; }
        else colSecond = std::max(colSecond, colP[i]);
    }

    const double pair = rowBest + colBest;
    state_.purity = std::clamp(pair / energy, 0.0, 1.0);
    const double rowDb = 10.0 * std::log10(rowBest + 1e-20);
    const double colDb = 10.0 * std::log10(colBest + 1e-20);
    state_.twistDb = std::abs(rowDb - colDb);

    // Engineering gates: relative dominance, twist, and absolute presence vs noise floor.
    const bool strong = pair > 0.12 * energy &&
        rowBest > 2.5 * std::max(rowSecond, 1e-20) &&
        colBest > 2.5 * std::max(colSecond, 1e-20) &&
        state_.twistDb <= 8.0 &&
        bestRow >= 0 && bestCol >= 0;

    char digit = 0;
    if (strong) digit = kPad[bestRow][bestCol];

    const int64_t ms = nowMs();
    if (digit) {
        silenceFrames_ = 0;
        if (digit == candidate_) candidateHits_ = std::min(8u, candidateHits_ + 1);
        else { candidate_ = digit; candidateHits_ = 1; }

        // Require ~40 ms (2 hops at 10 ms) before latching a new digit.
        if (candidateHits_ >= 2) {
            state_.toneActive = true;
            state_.digit = candidate_;
            state_.status = std::string("DTMF ") + candidate_;
            if (activeDigit_ != candidate_) {
                activeDigit_ = candidate_;
                if (state_.sequence.size() < 32) state_.sequence.push_back(candidate_);
                ++state_.confirmedDigits;
                state_.lastDigitMs = ms;
                pending_.push_back({PendingEvent::Kind::Digit, candidate_, state_.sequence, ms});
                if (pending_.size() > 64)
                    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(pending_.size() - 64));
            }
        }
    } else {
        candidate_ = 0;
        candidateHits_ = 0;
        state_.toneActive = false;
        // Hold the latched digit across brief weak Goertzel hops so one keypress
        // does not register twice (e.g. 1337 becoming 11333377).
        if (++silenceFrames_ >= 3) {
            if (activeDigit_) {
                activeDigit_ = 0;
                state_.status = state_.sequence.empty() ? "Searching DTMF" : ("Sequence " + state_.sequence);
            }
        }
        // ~300 ms of silence ends a keypad sequence (slow hand entry still stays one row).
        if (silenceFrames_ >= 30) finishSequence(ms);
        if (!state_.toneActive && state_.sequence.empty() && state_.status.find("DTMF") == 0)
            state_.status = "Searching DTMF";
    }
}

DtmfSnapshot decodeDtmfFile(const std::string& path, size_t chunkSize)
{
    if (!chunkSize || chunkSize > 8192) throw std::invalid_argument("Tone chunk must be 1..8192 samples");
    ma_decoder reader{};
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_file(path.c_str(), &config, &reader) != MA_SUCCESS)
        throw std::runtime_error("Cannot open DTMF recording");
    struct Guard { ma_decoder* reader; ~Guard() { ma_decoder_uninit(reader); } } guard{&reader};
    if (reader.outputChannels != 1 || reader.outputSampleRate < 8000 || reader.outputSampleRate > 96000)
        throw std::runtime_error("DTMF recording must be mono at 8..96 kHz");
    DtmfDecoder decoder;
    std::vector<float> samples(chunkSize);
    uint64_t first = 0;
    for (;;) {
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
    // Flush trailing sequence on EOF.
    std::vector<float> silence(static_cast<size_t>(reader.outputSampleRate / 5), 0.f);
    decoder.process(silence, reader.outputSampleRate, 0, 1, first, false);
    return decoder.snapshot();
}
