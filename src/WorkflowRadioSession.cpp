#include "WorkflowRadioSession.h"
#include <QUuid>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <spdlog/spdlog.h>

WorkflowRadioSession::WorkflowRadioSession(DeviceManager& manager, const std::string& key,
    DeviceManager::DeviceLeaseOwner owner, double frequencyHz, const std::function<bool()>& cancel,
    double sampleRateHz, double bandwidthHz)
    : manager_(manager) {
    const auto devices = manager.getDevices();
    size_t index = size_t(-1);
    for (size_t i = 0; i < devices.size(); ++i) if (devices[i].stableKey == key) {
        if (index != size_t(-1)) throw std::runtime_error("Ambiguous radio identity; configure unique SDR serials");
        index = i;
    }
    if (index == size_t(-1)) throw std::runtime_error("Selected radio is unavailable; refresh the device list");
    const auto& device = devices[index];
    if (device.isDiversityComposite) throw std::runtime_error("Dedicated workflow routing of diversity composites is not yet qualified");
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0 ||
        (device.minFreq > 0 && frequencyHz < device.minFreq) || (device.maxFreq > 0 && frequencyHz > device.maxFreq))
        throw std::runtime_error("Frequency is outside the selected radio's range");
    std::string error;
    token_ = manager.claimDevice(index, owner, QUuid::createUuid().toString(QUuid::Id128).toStdString(), &error, key);
    if (!token_) throw std::runtime_error(error);
    try {
        if (sampleRateHz > 0 && !manager.configureDeviceCapture(token_, sampleRateHz, bandwidthHz, &error))
            throw std::runtime_error(error);
        if (!manager.tuneDevice(token_, frequencyHz, &error) || !manager.startDevice(token_, &error))
            throw std::runtime_error(error.empty() ? "Could not start selected radio" : error);
        // Same bounded hardware-open allowance as InmarsatEngine, not a DSP timer.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel && cancel()) throw std::runtime_error("Radio startup cancelled");
            if (!manager.ownsDevice(token_)) throw std::runtime_error("Radio ownership changed during startup");
            const auto state = manager.getRuntimeStateLabel(index);
            if (state == "live hardware" && manager.getCenterTuneAppliedSeq(index) == manager.getCenterTuneRequestSeq(index)) {
                spdlog::info("Workflow radio ready: workflow={} dev={} lease={} frequencyHz={} rateHz={}",
                    DeviceManager::leaseOwnerName(owner), index, token_.id, frequencyHz, manager.getCurrentSampleRate(index));
                return;
            }
            if (state.find("failed") != std::string::npos || state.find("stuck") != std::string::npos || state == "simulated/stub")
                throw std::runtime_error("Radio did not open as live hardware: " + state);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error("Timed out opening selected radio");
    } catch (...) {
        manager.stopDevice(token_);
        throw;
    }
}

WorkflowRadioSession::~WorkflowRadioSession() {
    try { manager_.stopDevice(token_); }
    catch (const std::exception& e) { spdlog::error("Workflow radio shutdown: {}", e.what()); }
}
bool WorkflowRadioSession::valid() const { return manager_.ownsDevice(token_); }
