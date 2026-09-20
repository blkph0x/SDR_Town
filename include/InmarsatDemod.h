#pragma once

#include <complex>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class InmarsatDemodMode {
    AeroMsk600 = 0,
    AeroMsk1200 = 1,
    AeroOqpsk10500 = 2,
    AeroVoice8400 = 3,
    EgcBpsk1200 = 4
};

struct InmarsatDemodStats {
    bool locked = false;
    double ebnoDb = 0.0;
    double freqOffsetHz = 0.0;
    uint64_t bitsOut = 0;
    uint64_t framesOut = 0;
};

// Clean-room DDC + PMSK / continuous OQPSK / EGC BPSK front-end.
// Outputs packed soft/hard bytes for higher-layer ACARS / AMBE / EGC parsers.
class InmarsatDemod {
public:
    using ByteSink = std::function<void(const uint8_t* data, size_t len)>;

    void reset(InmarsatDemodMode mode, double sampleRateHz, double channelOffsetHz);
    void setMode(InmarsatDemodMode mode);
    void setChannelOffset(double offsetHz);

    // Process interleaved IQ (complex float). channelOffsetHz relative to capture CF.
    void process(const std::complex<float>* iq, size_t n);

    void setByteSink(ByteSink sink) { sink_ = std::move(sink); }
    InmarsatDemodStats stats() const { return stats_; }

    static InmarsatDemodMode modeFromBaud(int baud, bool egc);
    static double symbolRate(InmarsatDemodMode m);

private:
    void ddcAndDecimate(const std::complex<float>* iq, size_t n,
                        std::vector<std::complex<float>>& out);
    void demodPmsk(const std::vector<std::complex<float>>& baseband);
    void demodOqpsk(const std::vector<std::complex<float>>& baseband);
    void demodBpsk(const std::vector<std::complex<float>>& baseband);
    void emitBits(const uint8_t* bits, size_t nBits);

    InmarsatDemodMode mode_ = InmarsatDemodMode::AeroOqpsk10500;
    double sampleRateHz_ = 2.048e6;
    double channelOffsetHz_ = 0.0;
    double phase_ = 0.0;
    double ncoPhase_ = 0.0;
    double costasPhase_ = 0.0;
    double costasFreq_ = 0.0;
    double symbolPhase_ = 0.0;
    std::complex<float> prevSample_{1.f, 0.f};
    std::vector<std::complex<float>> iirState_;
    std::vector<uint8_t> bitBuf_;
    std::vector<uint8_t> byteAcc_;
    int bitCount_ = 0;
    ByteSink sink_;
    InmarsatDemodStats stats_;
};
