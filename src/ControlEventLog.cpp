#include "ControlEventLog.h"
#include <algorithm>

ControlEventLog::ControlEventLog(size_t capacity)
    : capacity_(std::max<size_t>(1, capacity))
{
    events_.reserve(std::min<size_t>(capacity_, 256));
}

void ControlEventLog::push(ControlEvent event)
{
    std::lock_guard lock(mutex_);
    if (events_.size() >= capacity_) events_.erase(events_.begin());
    events_.push_back(std::move(event));
    ++nextIndex_;
}

void ControlEventLog::clear()
{
    std::lock_guard lock(mutex_);
    events_.clear();
}

size_t ControlEventLog::size() const
{
    std::lock_guard lock(mutex_);
    return events_.size();
}

std::vector<ControlEvent> ControlEventLog::snapshot() const
{
    std::lock_guard lock(mutex_);
    return events_;
}

uint64_t ControlEventLog::drainSince(uint64_t afterIndex, std::vector<ControlEvent>& out) const
{
    std::lock_guard lock(mutex_);
    out.clear();
    if (nextIndex_ == 0 || afterIndex >= nextIndex_) return nextIndex_;
    const uint64_t available = nextIndex_;
    const uint64_t oldestIndex = available > events_.size() ? available - events_.size() : 0;
    uint64_t start = std::max(afterIndex, oldestIndex);
    for (uint64_t idx = start; idx < available; ++idx) {
        const size_t pos = static_cast<size_t>(idx - oldestIndex);
        if (pos < events_.size()) out.push_back(events_[pos]);
    }
    return available;
}

uint64_t ControlEventLog::nextIndex() const
{
    std::lock_guard lock(mutex_);
    return nextIndex_;
}

const char* ControlEventLog::kindName(ControlEvent::Kind kind)
{
    switch (kind) {
    case ControlEvent::Kind::DtmfDigit: return "dtmf_digit";
    case ControlEvent::Kind::DtmfSequence: return "dtmf_sequence";
    case ControlEvent::Kind::CtcssChange: return "ctcss";
    case ControlEvent::Kind::DcsChange: return "dcs";
    case ControlEvent::Kind::CarrierOpen: return "carrier_open";
    case ControlEvent::Kind::CarrierClose: return "carrier_close";
    }
    return "unknown";
}

const char* ControlEventLog::channelName(ControlEvent::Channel channel)
{
    switch (channel) {
    case ControlEvent::Channel::Tuned: return "tuned";
    case ControlEvent::Channel::Output: return "output";
    case ControlEvent::Channel::Input: return "input";
    }
    return "unknown";
}
