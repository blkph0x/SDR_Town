#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace p25dsp {

struct P25FilterKey {
    uint32_t inputRateMilliHz = 0;
    uint32_t outputRateMilliHz = 0;
    uint32_t cutoffMilliHz = 0;
    uint32_t transitionMilliHz = 0;
    uint16_t maxTaps = 0;
    uint8_t kind = 0; // 0=LPF, 1=RRC

    bool operator==(const P25FilterKey& other) const noexcept;
};

struct P25FilterKeyHash {
    size_t operator()(const P25FilterKey& key) const noexcept;
};

class P25FilterCache {
public:
    const std::vector<double>& lowpassTaps(double sampleRate,
                                           double cutoffHz,
                                           double transitionHz,
                                           int maxTaps);
    const std::vector<double>& rrcTaps(double sampleRate,
                                       double symbolRate,
                                       double alpha,
                                       int maxTaps);
    uint64_t designCalls() const noexcept { return m_designCalls; }

private:
    std::unordered_map<P25FilterKey, std::vector<double>, P25FilterKeyHash> m_cache;
    uint64_t m_designCalls = 0;

    static std::vector<double> designLowpassTaps(double sampleRate,
                                                 double cutoffHz,
                                                 double transitionHz,
                                                 int maxTaps);
    static std::vector<double> designRrcTaps(double sampleRate,
                                             double symbolRate,
                                             double alpha,
                                             int maxTaps);
};

} // namespace p25dsp
