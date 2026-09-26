#pragma once
#include "InmarsatDemod.h"
#include "InmarsatAero.h"
#include <nlohmann/json.hpp>

// One instance per source, owned by its worker. Live and replay use this entry.
class InmarsatPipeline {
public:
    InmarsatPipeline();
    ~InmarsatPipeline();
    InmarsatPipeline(InmarsatPipeline&&) noexcept;
    InmarsatPipeline& operator=(InmarsatPipeline&&) noexcept;
    void setMessageSink(InmarsatAero::MessageSink sink);
    void setPcmSink(InmarsatAero::PcmSink sink);
    void process(const std::complex<float>* iq, size_t count, uint64_t startSample,
                 double rate, double centerHz, double channelHz,
                 InmarsatDemodMode mode, bool discontinuity);
    InmarsatDemodStats stats() const;
    nlohmann::json report() const;
    InmarsatConstellation constellation() const;
private:
    struct Native;
    std::unique_ptr<Native> native_;
    InmarsatDemod demod_;
    bool started_ = false;
    double rate_ = 0, center_ = 0, channel_ = 0;
    InmarsatDemodMode mode_ = InmarsatDemodMode::AeroOqpsk10500;
    uint64_t nextSample_ = 0, samples_ = 0, blocks_ = 0, resets_ = 0, gaps_ = 0;
    uint64_t symbols_ = 0, rawBlocks_ = 0;
    double totalMs_ = 0, maxMs_ = 0, peak_ = 0, sumPower_ = 0;
    // DEC-0139: worker-owned block clocks, no logging or allocation per sample.
    double validationMs_=0, setupMs_=0, probeMs_=0, channelizerMs_=0, modemMs_=0;
    double inputSeconds_=0, lastBlockMs_=0, lastInputMs_=0;
    uint64_t overBudgetBlocks_=0;
};
