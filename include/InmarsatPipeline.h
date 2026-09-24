#pragma once
#include "InmarsatDemod.h"
#include <nlohmann/json.hpp>

// One instance per source, owned by its worker. Live and replay use this entry.
class InmarsatPipeline {
public:
    void process(const std::complex<float>* iq, size_t count, uint64_t startSample,
                 double rate, double centerHz, double channelHz,
                 InmarsatDemodMode mode, bool discontinuity);
    InmarsatDemodStats stats() const { return demod_.stats(); }
    nlohmann::json report() const;
private:
    InmarsatDemod demod_;
    bool started_ = false;
    double rate_ = 0, center_ = 0, channel_ = 0;
    InmarsatDemodMode mode_ = InmarsatDemodMode::AeroOqpsk10500;
    uint64_t nextSample_ = 0, samples_ = 0, blocks_ = 0, resets_ = 0, gaps_ = 0;
    uint64_t symbols_ = 0, rawBlocks_ = 0;
    double totalMs_ = 0, maxMs_ = 0, peak_ = 0, sumPower_ = 0;
};
