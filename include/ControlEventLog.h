#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Thread-safe ring of receive-only repeater/control observation events.
// DSP pushes; GUI and status JSON poll snapshots. Never transmits.

struct ControlEvent {
    enum class Kind {
        DtmfDigit,
        DtmfSequence,
        CtcssChange,
        DcsChange,
        CarrierOpen,
        CarrierClose
    };
    enum class Channel { Tuned, Output, Input };

    int64_t ms = 0;
    Kind kind = Kind::DtmfDigit;
    Channel channel = Channel::Tuned;
    double freqHz = 0;
    std::string detail;
};

class ControlEventLog {
public:
    explicit ControlEventLog(size_t capacity = 256);
    void push(ControlEvent event);
    void clear();
    size_t size() const;
    std::vector<ControlEvent> snapshot() const;
    // Events with index > afterIndex (monotonic). Returns new high-water index.
    uint64_t drainSince(uint64_t afterIndex, std::vector<ControlEvent>& out) const;
    uint64_t nextIndex() const;

    static const char* kindName(ControlEvent::Kind kind);
    static const char* channelName(ControlEvent::Channel channel);

private:
    mutable std::mutex mutex_;
    size_t capacity_ = 256;
    std::vector<ControlEvent> events_;
    uint64_t nextIndex_ = 0;
};
