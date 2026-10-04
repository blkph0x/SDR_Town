#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

// DEC-0190: preserve driver serialization without letting a busy RX loop
// reacquire ahead of an already waiting startup/retune/stop command.
class DriverIoMutex {
public:
    void lock() {
        std::unique_lock lock(mutex_);
        const auto ticket = next_++;
        ready_.wait(lock, [&] { return ticket == serving_; });
    }

    void unlock() {
        { std::lock_guard lock(mutex_); ++serving_; }
        ready_.notify_all();
    }

    // Diagnostic snapshot; does not reserve or alter admission order.
    uint64_t waiting() const {
        std::lock_guard lock(mutex_);
        const auto admitted = next_ - serving_;
        return admitted ? admitted - 1 : 0;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    uint64_t next_ = 0;
    uint64_t serving_ = 0;
};
