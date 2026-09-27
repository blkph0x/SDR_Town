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
    // One producer owns push/report; AES 0 means unknown, never inferred.
    void push(std::span<const int16_t> samples, uint32_t aes=0);
    void discardPlayback();
    void finish();
    size_t queued() const;
    nlohmann::json report() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
