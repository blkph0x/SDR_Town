#pragma once
#include "SstvLiveInput.h"
#include <memory>
#include <optional>
#include <string_view>

// DEC-0117: worker-owned, non-tuning RF acquisition. Auto requires classic VIS;
// manual routes also support headerless/extended-VIS image acquisition.
class SstvRfRouter final {
public:
    SstvRfRouter(double sampleRate, double centerHz, double targetHz,
                 std::string_view mode = "auto", uint64_t sourceId = 0);
    ~SstvRfRouter();
    static bool supports(std::string_view mode);
    size_t maxInputSamples() const;
    void process(const std::vector<std::complex<float>>& iq, uint64_t firstSample,
                 uint64_t epoch, bool gap = false);
    std::optional<SstvInputEvent> pop();
    std::string_view selectedMode() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
