#pragma once

#include "SatCatalogue.h"
#include "SatObserverConfig.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SatPassInfo {
    std::string satId;
    std::string satName;
    int noradId = 0;
    std::string downlinkId;
    std::string downlinkLabel;
    std::string role;
    std::string mode;
    double freqHz = 0.0;
    double aosUnix = 0.0;
    double losUnix = 0.0;
    double maxElDeg = 0.0;
    double durationSec = 0.0;
};

struct SatArmedState {
    bool armed = false;
    bool autoTrack = true;
    std::string satId;
    std::string downlinkId;
    std::string role;
    std::string mode;
    double freqHz = 0.0;
    double aosUnix = 0.0;
    double losUnix = 0.0;
    double dopplerHz = 0.0;
    double elevationDeg = 0.0;
    double tunedHz = 0.0;
};

struct SatPassPlannerSnapshot {
    SatObserverConfig observer;
    SatCatalogue catalogue;
    std::vector<SatPassInfo> passes;
    SatArmedState armed;
    int64_t tleAgeSec = -1;
    std::string lastStatus;
    bool predicting = false;
};

// Owns observer/catalogue selection, TLE age, pass list, Doppler arm state.
class SatPassPlanner {
public:
    static SatPassPlanner& instance();

    void setObserver(const SatObserverConfig& obs);
    SatObserverConfig observer() const;

    SatCatalogue catalogue() const;
    void setCatalogueSelection(const std::vector<std::string>& selectedIds);

    bool refreshTle(std::string* error = nullptr);
    void refreshTleAsync(std::function<void(bool ok, std::string error)> done);
    void ensureTleLoaded();

    // Recompute upcoming passes for selected sats (hours ahead).
    void refreshPasses(double hoursAhead = 24.0);

    bool arm(const std::string& satId, const std::string& downlinkId, bool autoTrack = true,
             std::string* error = nullptr);
    void disarm();
    void setAutoTrack(bool on);

    // Call ~1 Hz from engine/UI timer: updates Doppler and returns tuned Hz if auto-track active.
    // Returns true if caller should retune device to outTunedHz.
    bool tickAutoTrack(double* outTunedHz);

    SatPassPlannerSnapshot snapshot(double hoursAhead = 24.0) const;
    nlohmann::json statusJson() const;

    void setUpdateCallback(std::function<void()> cb);

private:
    SatPassPlanner();
    ~SatPassPlanner();

    void predictLocked(double hoursAhead);
    void notify();

    mutable std::mutex mutex_;
    SatObserverConfig observer_;
    SatCatalogue catalogue_;
    std::vector<SatPassInfo> passes_;
    SatArmedState armed_;
    std::string lastStatus_;
    std::function<void()> updateCb_;
    std::atomic<bool> predicting_{false};
};
