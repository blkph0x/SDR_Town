#include "SatcomAsyncLog.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace SatcomLog {
namespace {

const char* typeName(EventType t) {
    switch (t) {
    case EventType::Start: return "START";
    case EventType::Stop: return "STOP";
    case EventType::Skip: return "SKIP";
    case EventType::Lock: return "LOCK";
    case EventType::Unlock: return "UNLOCK";
    case EventType::RecordStart: return "REC_START";
    case EventType::RecordStop: return "REC_STOP";
    case EventType::DecodeOk: return "DECODE_OK";
    case EventType::DecodeFail: return "DECODE_FAIL";
    case EventType::Info:
    default: return "INFO";
    }
}

} // namespace

AsyncLog::AsyncLog(size_t capacity)
    : capacity_(capacity < 64 ? 64 : capacity)
{
    ring_.resize(capacity_);
    uiLines_.reserve(uiCap_);
}

AsyncLog::~AsyncLog() { stop(); }

void AsyncLog::setLogDirectory(const std::string& dir) { logDir_ = dir; }

void AsyncLog::start() {
    if (run_.exchange(true)) return;
    writer_ = std::thread(&AsyncLog::writerLoop, this);
}

void AsyncLog::stop() {
    if (!run_.exchange(false)) return;
    if (writer_.joinable()) writer_.join();
}

bool AsyncLog::tryPush(EventType type, double freqHz, const char* text) {
    Event ev;
    ev.type = type;
    ev.seq = seq_.fetch_add(1, std::memory_order_relaxed) + 1;
    ev.freqHz = freqHz;
    if (text) {
        std::strncpy(ev.text, text, sizeof(ev.text) - 1);
        ev.text[sizeof(ev.text) - 1] = '\0';
    }

    uint32_t head = head_.load(std::memory_order_relaxed);
    uint32_t next = (head + 1) % static_cast<uint32_t>(capacity_);
    uint32_t tail = tail_.load(std::memory_order_acquire);
    if (next == tail) {
        // Drop oldest.
        tail_.store((tail + 1) % static_cast<uint32_t>(capacity_), std::memory_order_release);
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    ring_[head % capacity_] = ev;
    head_.store(next, std::memory_order_release);
    appendUiLine(ev);
    return true;
}

void AsyncLog::appendUiLine(const Event& ev) {
    std::ostringstream oss;
    oss << typeName(ev.type) << " " << std::fixed << std::setprecision(5) << (ev.freqHz / 1e6)
        << "MHz " << ev.text;
    std::lock_guard<std::mutex> lk(uiMutex_);
    uiLines_.push_back(oss.str());
    if (uiLines_.size() > uiCap_) {
        uiLines_.erase(uiLines_.begin(), uiLines_.begin() + static_cast<std::ptrdiff_t>(uiLines_.size() - uiCap_));
    }
}

std::vector<std::string> AsyncLog::recentLines(size_t maxLines) const {
    std::lock_guard<std::mutex> lk(uiMutex_);
    if (uiLines_.empty() || maxLines == 0) return {};
    const size_t n = std::min(maxLines, uiLines_.size());
    return std::vector<std::string>(uiLines_.end() - static_cast<std::ptrdiff_t>(n), uiLines_.end());
}

void AsyncLog::writerLoop() {
    namespace fs = std::filesystem;
    std::ofstream out;
    std::string openPath;
    auto ensureFile = [&]() {
        if (logDir_.empty()) return;
        std::error_code ec;
        fs::create_directories(logDir_, ec);
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char name[64];
        std::snprintf(name, sizeof(name), "satcom_%04d%02d%02d.log",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        const std::string path = (fs::path(logDir_) / name).string();
        if (path != openPath || !out.is_open()) {
            out.close();
            out.open(path, std::ios::app | std::ios::binary);
            openPath = path;
        }
    };

    while (run_.load(std::memory_order_acquire)) {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        ensureFile();
        while (tail != head) {
            const Event& ev = ring_[tail % capacity_];
            if (out.is_open()) {
                char line[256];
                const int n = std::snprintf(line, sizeof(line), "%u %s %.0f %s\n",
                                            ev.seq, typeName(ev.type), ev.freqHz, ev.text);
                if (n > 0) {
                    out.write(line, n);
                    bytesOnDisk_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
                }
            }
            written_.fetch_add(1, std::memory_order_relaxed);
            tail = (tail + 1) % static_cast<uint32_t>(capacity_);
            head = head_.load(std::memory_order_acquire);
        }
        tail_.store(tail, std::memory_order_release);
        if (out.is_open()) out.flush();
    }
    if (out.is_open()) out.close();
}

} // namespace SatcomLog
