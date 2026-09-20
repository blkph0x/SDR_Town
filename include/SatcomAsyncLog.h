#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Fixed-capacity SPSC-style ring for satcom events. Hot path never allocates or formats.
namespace SatcomLog {

enum class EventType : uint8_t {
    Start = 1,
    Stop = 2,
    Skip = 3,
    Lock = 4,
    Unlock = 5,
    RecordStart = 6,
    RecordStop = 7,
    DecodeOk = 8,
    DecodeFail = 9,
    Info = 10
};

struct Event {
    EventType type = EventType::Info;
    uint32_t seq = 0;
    double freqHz = 0.0;
    // Fixed payload — no heap on push.
    char text[160]{};
};

class AsyncLog {
public:
    explicit AsyncLog(size_t capacity = 4096);
    ~AsyncLog();

    AsyncLog(const AsyncLog&) = delete;
    AsyncLog& operator=(const AsyncLog&) = delete;

    void setLogDirectory(const std::string& dir);
    void start();
    void stop();

    // Hot path: copy POD into ring. Returns false if dropped (ring full → drop oldest).
    bool tryPush(EventType type, double freqHz, const char* text);

    uint64_t eventsWritten() const { return written_.load(std::memory_order_relaxed); }
    uint64_t eventsDropped() const { return dropped_.load(std::memory_order_relaxed); }
    uint64_t bytesOnDisk() const { return bytesOnDisk_.load(std::memory_order_relaxed); }

    // UI snapshot: newest-first up to maxLines (allocates; call off DSP thread).
    std::vector<std::string> recentLines(size_t maxLines = 200) const;

private:
    void writerLoop();
    void appendUiLine(const Event& ev);

    size_t capacity_ = 0;
    std::vector<Event> ring_;
    std::atomic<uint32_t> head_{0}; // next write
    std::atomic<uint32_t> tail_{0}; // next read
    std::atomic<uint32_t> seq_{0};
    std::atomic<bool> run_{false};
    std::thread writer_;
    std::string logDir_;
    std::atomic<uint64_t> written_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> bytesOnDisk_{0};

    mutable std::mutex uiMutex_;
    std::vector<std::string> uiLines_;
    size_t uiCap_ = 500;
};

} // namespace SatcomLog
