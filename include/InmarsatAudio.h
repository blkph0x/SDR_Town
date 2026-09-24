#pragma once
#include <QString>
#include <memory>
#include <span>
#include <cstdint>
#include <nlohmann/json.hpp>
// Own playback device and optional WAV; no P25 or analog audio state is shared.
class InmarsatAudio {
public:
    InmarsatAudio(bool playback, const QString& wavPath={});
    ~InmarsatAudio();
    void push(std::span<const int16_t> samples);
    void discardPlayback();
    void finish();
    size_t queued() const;
    nlohmann::json report() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
