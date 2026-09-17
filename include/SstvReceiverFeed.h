#pragma once
#include "SstvLiveInput.h"

// DEC-0099: allocation/control on worker, try-lock-only publication on RX.
class SstvReceiverFeed final {
public:
    std::shared_ptr<SstvLiveInput> attach();
    void detach(const std::shared_ptr<SstvLiveInput>& input);
    void finish(const std::shared_ptr<SstvLiveInput>& input);
    void publish(const FmMultiplexBlock& block,uint64_t sourceId);
    void discontinuity();
    std::optional<SstvInputStats> stats() const; // Control/diagnostics only, may lock.
private:
    mutable std::mutex mutex_;
    std::shared_ptr<SstvLiveInput> input_;
    std::atomic<bool> attached_{false},missed_{false};
};
