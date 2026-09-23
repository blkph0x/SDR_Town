#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class AudioEngine;

struct SatcomHostCallbacks {
    std::function<bool(size_t deviceIndex, std::string* error)> beginReceiverTakeover;
    std::function<void()> endReceiverTakeover;
    std::function<AudioEngine*(std::string* error)> acquireAudioEngine;
    std::function<void(const std::vector<float>& powerDb,
                       double centerHz,
                       double sampleRateHz)> publishSpectrum;
    std::function<void(const std::string& status)> publishStatus;
};

// Small process-local bridge between the standalone-capable Satcom engine and
// the GUI host. The engine remains usable in CLI/tests without MainWindow, but
// the GUI installs these callbacks so Satcom shares receiver ownership, audio
// routing, status and spectrum with the main SDR Town application.
class SatcomHostServices {
public:
    static SatcomHostServices& instance() {
        static SatcomHostServices services;
        return services;
    }

    void install(SatcomHostCallbacks callbacks) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_ = std::move(callbacks);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_ = {};
    }

    bool installed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<bool>(callbacks_.beginReceiverTakeover) ||
               static_cast<bool>(callbacks_.endReceiverTakeover) ||
               static_cast<bool>(callbacks_.acquireAudioEngine) ||
               static_cast<bool>(callbacks_.publishSpectrum) ||
               static_cast<bool>(callbacks_.publishStatus);
    }

    bool beginReceiverTakeover(size_t deviceIndex, std::string* error = nullptr) {
        std::function<bool(size_t, std::string*)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callbacks_.beginReceiverTakeover;
        }
        if (!callback) {
            if (error) error->clear();
            return true;
        }
        try {
            return callback(deviceIndex, error);
        } catch (const std::exception& ex) {
            if (error) *error = std::string("Satcom host takeover: ") + ex.what();
        } catch (...) {
            if (error) *error = "Satcom host takeover failed";
        }
        return false;
    }

    void endReceiverTakeover() noexcept {
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callbacks_.endReceiverTakeover;
        }
        if (!callback) return;
        try {
            callback();
        } catch (...) {
        }
    }

    AudioEngine* acquireAudioEngine(std::string* error = nullptr) {
        std::function<AudioEngine*(std::string*)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callbacks_.acquireAudioEngine;
        }
        if (!callback) {
            if (error) *error = "SDR Town host audio is not installed";
            return nullptr;
        }
        try {
            return callback(error);
        } catch (const std::exception& ex) {
            if (error) *error = std::string("Satcom host audio: ") + ex.what();
        } catch (...) {
            if (error) *error = "Satcom host audio failed";
        }
        return nullptr;
    }

    void publishSpectrum(const std::vector<float>& powerDb,
                         double centerHz,
                         double sampleRateHz) noexcept {
        std::function<void(const std::vector<float>&, double, double)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callbacks_.publishSpectrum;
        }
        if (!callback) return;
        try {
            callback(powerDb, centerHz, sampleRateHz);
        } catch (...) {
        }
    }

    void publishStatus(const std::string& status) noexcept {
        std::function<void(const std::string&)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callbacks_.publishStatus;
        }
        if (!callback) return;
        try {
            callback(status);
        } catch (...) {
        }
    }

private:
    SatcomHostServices() = default;

    mutable std::mutex mutex_;
    SatcomHostCallbacks callbacks_;
};
