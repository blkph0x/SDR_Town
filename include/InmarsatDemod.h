#pragma once

#include <complex>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

enum class InmarsatDemodMode {
    AeroMsk600 = 0,
    AeroMsk1200 = 1,
    AeroOqpsk10500 = 2,
    AeroVoice8400 = 3,
    EgcBpsk1200 = 4
};

struct InmarsatDemodStats {
    // Physical-layer carrier/coherence indication only. This is deliberately
    // separate from protocol lock: no unique-word/FEC implementation exists in
    // this front-end yet, so locked and framesOut remain false/zero.
    bool carrierDetected = false;
    bool locked = false;
    double quality = 0.0;       // 0..1 PSK/MSK moment coherence
    double ebnoDb = 0.0;        // reserved until a validated estimator exists
    double freqOffsetHz = 0.0;  // carrier-loop estimate where available
    uint64_t symbolsOut = 0;
    uint64_t bitsOut = 0;
    uint64_t rawBlocksOut = 0;
    uint64_t framesOut = 0;     // validated protocol frames only
};

// Clean-room DDC plus conservative PMSK / OQPSK / BPSK physical-layer probe.
// Raw bit blocks are counted for diagnostics but are not delivered to ACARS,
// message or voice-follow layers until framing, deinterleaving and FEC can
// validate them.
class InmarsatDemod {
public:
    using ByteSink = std::function<void(const uint8_t* data, size_t len)>;

    void reset(InmarsatDemodMode mode, double sampleRateHz, double channelOffsetHz);
    void setMode(InmarsatDemodMode mode);
    void setChannelOffset(double offsetHz);

    // Process IQ as complex float. channelOffsetHz is relative to capture CF.
    void process(const std::complex<float>* iq, size_t n);

    // Compatibility hook retained for existing callers. The sink is ignored
    // deliberately: emitting unframed physical-layer bytes as protocol data is
    // unsafe and previously produced false ACARS/message/voice events.
    void setByteSink(ByteSink) { sink_ = {}; }
    InmarsatDemodStats stats() const { return stats_; }

    static InmarsatDemodMode modeFromBaud(int baud, bool egc);
    static double symbolRate(InmarsatDemodMode m);

private:
    void configureRates();
    void ddcAndDecimate(const std::complex<float>* iq, size_t n,
                        std::vector<std::complex<float>>& out);
    void demodPmsk(const std::vector<std::complex<float>>& baseband);
    void demodOqpsk(const std::vector<std::complex<float>>& baseband);
    void demodBpsk(const std::vector<std::complex<float>>& baseband);
    void updateCarrierQuality(const std::complex<float>& symbol, int momentOrder);
    void emitBits(const uint8_t* bits, size_t nBits);
    void clearRawAssembler();

    InmarsatDemodMode mode_ = InmarsatDemodMode::AeroOqpsk10500;
    double sampleRateHz_ = 2.048e6;
    double channelOffsetHz_ = 0.0;
    double ncoPhase_ = 0.0;
    double costasPhase_ = 0.0;
    double costasFreq_ = 0.0;
    double symbolPhase_ = 0.0;
    double basebandRateHz_ = 0.0;
    int decimation_ = 1;
    int decimationPhase_ = 0;
    std::complex<float> lowpassState_{0.f, 0.f};
    std::complex<float> prevSample_{1.f, 0.f};
    std::complex<double> symbolAccumulator_{0.0, 0.0};
    double differentialAccumulator_ = 0.0;
    uint32_t symbolAccumulatorCount_ = 0;
    std::complex<double> momentAverage_{0.0, 0.0};
    double signalPowerAverage_ = 0.0;
    uint32_t carrierGoodSymbols_ = 0;
    uint32_t carrierBadSymbols_ = 0;

    uint8_t packedByte_ = 0;
    int packedBitCount_ = 0;
    std::vector<uint8_t> rawBlock_;
    ByteSink sink_;
    InmarsatDemodStats stats_;
};
