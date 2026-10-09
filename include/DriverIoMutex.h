#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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

    // DEC-0207: succeed only when the lock is free and no ticket is waiting.
    // Failure must not take a ticket, or a wedged holder plus an abandoned
    // waiter would stall every later lock().
    bool try_lock() {
        std::lock_guard lock(mutex_);
        if (next_ != serving_)
            return false;
        ++next_;
        return true;
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

// DEC-0208: one FIFO lock per USB identity so RSPdx+RTL (and other combos)
// can readStream in parallel. Same serial still serializes RX tune vs read
// and TX writeStream vs stopTx try_lock. Callers hold the shared_ptr for the
// lock duration so drop/reuse of a key cannot destroy a mutex under a locker.
class DriverIoMutexTable {
public:
    std::shared_ptr<DriverIoMutex> mutexFor(const std::string& key) {
        const std::string& use = key.empty() ? kUnkeyed : key;
        std::lock_guard lock(tableMutex_);
        auto& slot = byKey_[use];
        if (!slot)
            slot = std::make_shared<DriverIoMutex>();
        return slot;
    }

    size_t size() const {
        std::lock_guard lock(tableMutex_);
        return byKey_.size();
    }

private:
    static inline const std::string kUnkeyed{"unkeyed"};
    mutable std::mutex tableMutex_;
    std::unordered_map<std::string, std::shared_ptr<DriverIoMutex>> byKey_;
};
