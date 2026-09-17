#pragma once
#include <memory>
#include <span>
#include <vector>

// DEC-0097: single worker owner, never RX/audio callback. Reset at every gap.
class SstvRateConverter final {
public:
    static constexpr unsigned outputRate=48000;
    SstvRateConverter();
    ~SstvRateConverter();
    void reset();
    void start(double inputRate);
    std::vector<float> process(std::span<const float> samples);
    double effectiveInputRate() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
