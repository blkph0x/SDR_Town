#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Aero voice interface, currently unavailable. DEC-0120: ordinary P25 mbelib
// does not implement the mini-m AMBE4800x3600 path used by libaeroambe.
class InmarsatVoice {
public:
    using PcmSink = std::function<void(const int16_t* pcm, int nSamples)>;

    InmarsatVoice();
    ~InmarsatVoice();

    bool backendAvailable() const;
    void reset();
    void feedBytes(const uint8_t* data, size_t len);
    void setPcmSink(PcmSink sink) { pcmSink_ = std::move(sink); }

    // Decode one 12-byte frame. Returns Hamming error count (-1 if no backend).
    int decodeFrame(const uint8_t frame12[12], int16_t out160[160]);

    void setRecording(bool on, const std::string& dir, uint32_t aesId = 0);
    bool recording() const { return record_; }
    uint64_t framesDecoded() const { return framesDecoded_; }

private:
    void writePcm(const int16_t* pcm, int n);
    void closeWav();

    struct Impl;
    Impl* impl_ = nullptr;
    PcmSink pcmSink_;
    std::vector<uint8_t> acc_;
    bool record_ = false;
    std::string recordDir_;
    uint32_t aesId_ = 0;
    FILE* wav_ = nullptr;
    uint32_t wavSamples_ = 0;
    uint64_t framesDecoded_ = 0;
};
