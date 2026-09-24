#pragma once
#include "InmarsatIqFile.h"
#include "InmarsatDemod.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

struct InmarsatReplayOptions {
    QString path;
    InmarsatIqOptions input;
    InmarsatDemodMode mode = InmarsatDemodMode::AeroOqpsk10500;
    double channelHz = 0; // zero follows capture center; no guessed RF frequency
    bool realTime = true;
    bool shareDiagnostics = false;
    QString logDirectory;
};
struct InmarsatReplaySnapshot {
    QString state = "idle", error, logPath, logError, session;
    InmarsatIqInfo info;
    uint64_t position = 0;
    nlohmann::json pipeline = nlohmann::json::object();
    bool running = false;
    nlohmann::json toJson() const;
};

// Control calls are serialized on the GUI/CLI owner; worker owns file + decoder.
class InmarsatReplay {
public:
    ~InmarsatReplay();
    void start(const InmarsatReplayOptions& options);
    void stop();
    void pause(bool paused);
    bool seek(double seconds);
    InmarsatReplaySnapshot snapshot() const;
private:
    void worker(InmarsatReplayOptions options);
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    bool stop_ = false, paused_ = false;
    std::optional<uint64_t> seek_;
    InmarsatReplaySnapshot snapshot_;
};
