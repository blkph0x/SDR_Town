#pragma once
#include "SstvLiveInput.h"
#include <memory>
#include <optional>
#include <string_view>
#include <functional>
#include <span>

// Worker-owned, non-tuning RF acquisition. Auto requires classic/extended VIS;
// manual routes also support headerless image acquisition (DEC-0217).
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
    // Fresh audio only, independent of the decoder's retained VIS pre-roll.
    using Monitor = std::function<void(std::span<const float>, std::string_view)>;
    void setMonitor(std::string_view provisionalMode, Monitor monitor);
    void resetForGap(); // Explicit capture loss: discard history and publish a gap before new PCM.
private:
    struct State;
    std::unique_ptr<State> state_;
};
