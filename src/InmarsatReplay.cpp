#include "InmarsatReplay.h"
#include "InmarsatPipeline.h"
#include "InmarsatDiagnostics.h"
#include <cmath>
#include <stdexcept>

nlohmann::json InmarsatReplaySnapshot::toJson() const {
    auto result = pipeline;
    result["state"] = state.toStdString();
    result["running"] = running;
    result["error"] = error.toStdString();
    result["logError"] = logError.toStdString();
    result["logPath"] = logPath.toStdString();
    result["session"] = session.toStdString();
    result["format"] = info.format.toStdString();
    result["rateHz"] = info.sampleRateHz;
    result["positionSamples"] = position;
    result["totalSamples"] = info.sampleCount;
    result["positionSeconds"] = info.sampleRateHz ? position / info.sampleRateHz : 0;
    result["durationSeconds"] = info.sampleRateHz ? info.sampleCount / info.sampleRateHz : 0;
    return result;
}
InmarsatReplay::~InmarsatReplay() { stop(); }
void InmarsatReplay::start(const InmarsatReplayOptions& options) {
    stop();
    {
        std::lock_guard<std::mutex> guard(mutex_);
        snapshot_ = {};
        snapshot_.state = "opening";
        snapshot_.running = true;
        stop_ = paused_ = false;
        seek_.reset();
    }
    try { thread_ = std::thread(&InmarsatReplay::worker, this, options); }
    catch (...) {
        std::lock_guard<std::mutex> guard(mutex_);
        snapshot_.state = "error";
        snapshot_.running = false;
        snapshot_.error = "Could not start replay worker";
        throw;
    }
}
void InmarsatReplay::stop() {
    { std::lock_guard<std::mutex> guard(mutex_); stop_ = true; }
    changed_.notify_all();
    if (thread_.joinable()) thread_.join();
}
void InmarsatReplay::pause(bool paused) {
    { std::lock_guard<std::mutex> guard(mutex_); paused_ = paused; }
    changed_.notify_all();
}
bool InmarsatReplay::seek(double seconds) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto& info = snapshot_.info;
    if (!snapshot_.running || !std::isfinite(seconds) || seconds < 0 ||
        info.sampleRateHz <= 0 || seconds > info.sampleCount / info.sampleRateHz) return false;
    seek_ = std::min(info.sampleCount, static_cast<uint64_t>(seconds * info.sampleRateHz));
    changed_.notify_all();
    return true;
}
InmarsatReplaySnapshot InmarsatReplay::snapshot() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return snapshot_;
}
void InmarsatReplay::worker(InmarsatReplayOptions options) {
    InmarsatDiagnostics diagnostics;
    InmarsatPipeline pipeline;
    InmarsatIqFile reader;
    QString finalState = "complete", error;
    auto lastLog = std::chrono::steady_clock::now();
    try {
        diagnostics.open(options.logDirectory, "replay", options.shareDiagnostics);
        diagnostics.write("open", {{"format", options.input.format.toStdString()},
            {"inputPath", options.path.toStdString()}, {"realTime", options.realTime}, {"state", "opening"}});
        reader.open(options.path, options.input);
        if (!std::isfinite(options.channelHz) || options.channelHz < 0)
            throw std::runtime_error("Invalid replay channel frequency");
        {
            std::lock_guard<std::mutex> guard(mutex_);
            snapshot_.info = reader.info();
            snapshot_.state = "playing";
            snapshot_.logPath = diagnostics.path();
            snapshot_.session = diagnostics.session();
        }
        auto anchor = std::chrono::steady_clock::now();
        uint64_t anchorSample = 0;
        bool resetPacing = false;
        while (true) {
            {
                std::unique_lock<std::mutex> guard(mutex_);
                if (stop_) { finalState = "stopped"; break; }
                if (seek_) {
                    reader.seek(*seek_);
                    snapshot_.position = *seek_;
                    seek_.reset();
                    resetPacing = true;
                }
                if (paused_) {
                    snapshot_.state = "paused";
                    resetPacing = true;
                    changed_.wait(guard, [this] { return stop_ || !paused_ || seek_.has_value(); });
                    continue;
                }
                snapshot_.state = "playing";
                if (resetPacing) {
                    anchor = std::chrono::steady_clock::now();
                    anchorSample = reader.position();
                    resetPacing = false;
                }
                if (options.realTime) {
                    const auto due = anchor + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>((reader.position() - anchorSample) / reader.info().sampleRateHz));
                    if (changed_.wait_until(guard, due, [this] { return stop_ || paused_ || seek_.has_value(); })) continue;
                }
            }
            // No catch-up drops: real-time mode paces reads but never skips samples.
            const auto block = reader.read();
            if (block.samples.empty()) break;
            pipeline.process(block.samples.data(), block.samples.size(), block.startSample,
                reader.info().sampleRateHz, block.centerHz,
                options.channelHz > 0 ? options.channelHz : block.centerHz, options.mode, block.discontinuity);
            {
                std::lock_guard<std::mutex> guard(mutex_);
                snapshot_.position = reader.position();
                snapshot_.pipeline = pipeline.report();
                snapshot_.logError = diagnostics.error();
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - lastLog >= std::chrono::seconds(1)) {
                diagnostics.write("progress", snapshot().toJson());
                lastLog = now;
            }
        }
    } catch (const std::exception& e) {
        finalState = "error";
        error = QString::fromUtf8(e.what());
    } catch (...) {
        finalState = "error";
        error = "Unexpected Inmarsat replay failure";
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        snapshot_.state = finalState;
        snapshot_.error = error;
        snapshot_.logPath = diagnostics.path();
        snapshot_.session = diagnostics.session();
        snapshot_.pipeline = pipeline.report();
    }
    if (!diagnostics.path().isEmpty()) {
        auto summary = snapshot().toJson();
        summary["errorCode"] = error.isEmpty() ? "none" : "replay_failed";
        summary["realTime"] = options.realTime;
        try { diagnostics.write("summary", summary, true); }
        catch (...) { error = "Could not finish Inmarsat diagnostic report"; }
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        snapshot_.logError = diagnostics.error();
        if (!error.isEmpty()) snapshot_.error = error;
        snapshot_.running = false;
    }
}
