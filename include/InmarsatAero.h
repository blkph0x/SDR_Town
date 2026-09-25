#pragma once
#include "InmarsatMessageStore.h"
#include <complex>
#include <functional>
#include <memory>
#include <span>
#include <vector>

// Local visual feedback only. Never serialize symbol arrays into remote logs.
struct InmarsatConstellation {
    std::vector<std::complex<float>> points;
    uint64_t sequence = 0;
    uint64_t ageSamples = 0; // 48 kHz modem input, not wall-clock/replay speed.
};

struct InmarsatAeroStats {
    uint64_t input48k=0, softBits=0, crcOk=0, crcBad=0, cFrames=0, rejectedCFrames=0;
    uint64_t voiceWords=0, pcmSamples=0, codecErrors=0, codecRepeats=0, codecMutes=0, positions=0;
    bool locked=false;
    double mse=0, ebno=0;
    uint32_t aes=0;
    uint64_t lastVoiceSample=0;
    uint64_t speechFrames=0; // Valid C-frames containing non-muted voice words.
};
// Single-worker owner; direct signals never cross threads. Recreate on source gap.
class InmarsatAero {
public:
    using MessageSink=std::function<void(const InmarsatMessage&)>;
    using PcmSink=std::function<void(std::span<const int16_t>, uint32_t)>;
    explicit InmarsatAero(int bitRate, bool burst=false);
    ~InmarsatAero();
    InmarsatAero(const InmarsatAero&)=delete;
    void processIf(std::span<const int16_t> samples);
    void setMessageSink(MessageSink sink);
    void setPcmSink(PcmSink sink);
    InmarsatAeroStats stats() const;
    InmarsatConstellation constellation() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// DEC-0121: stateful anti-alias filtering and resampling, followed by an 8 kHz
// real IF for the unchanged upstream 48 kHz modem. No analog listening filters.
class InmarsatChannelizer {
public:
    InmarsatChannelizer(double inputRate, double offset);
    ~InmarsatChannelizer();
    std::vector<int16_t> process(std::span<const std::complex<float>> samples);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
