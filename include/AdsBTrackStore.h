#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AircraftTrack {
    uint32_t icao = 0;
    std::string icaoHex;
    std::string callsign;
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double altFt = 0.0;
    double gsKt = 0.0;
    double trackDeg = 0.0;
    double verticalRateFpm = 0.0;
    std::string squawk;
    std::string reg;
    std::string typeCode;
    std::string route;
    std::string photoUrl;
    double lastSeenUnix = 0.0;
    bool fromLocal = false;
    bool fromNetwork = false;
    bool fromAdsc = false;
    bool positionValid = false;
};

struct AircraftMapSnapshot {
    std::vector<AircraftTrack> tracks;
    double centerLat = 0.0;
    double centerLon = 0.0;
    double radiusNm = 100.0;
    int64_t networkAgeSec = -1;
    uint64_t localFrames = 0;
    uint64_t localCrcOk = 0;
    std::string lastStatus;
    bool networkOnline = false;
};

class AdsBTrackStore {
public:
    static AdsBTrackStore& instance();

    void setObserver(double latDeg, double lonDeg, double radiusNm = 120.0);

    // Local Mode-S (must be called off the GUI thread; keep windows small).
    void ingestModeSFrame(const uint8_t* msg14);
    void processMagnitude(const float* mag, size_t n, double sampleRateHz);

    // Inmarsat ADS-C position report.
    void ingestAdscPosition(uint32_t icao, double latDeg, double lonDeg,
                            const std::string& icaoHex = {},
                            const std::string& callsign = {});

    // OpenSky JSON merge (GUI NAM or blocking httpGetUrl from CLI/worker).
    void mergeNetworkJson(const std::string& body);
    void setNetworkError(const std::string& err);
    bool refreshNetwork(std::string* error = nullptr);

    AircraftMapSnapshot snapshot() const;
    AircraftTrack trackByIcao(uint32_t icao) const;
    nlohmann::json statusJson() const;
    void observer(double* lat, double* lon, double* radiusNm) const;

    void setUpdateCallback(std::function<void()> cb);

private:
    AdsBTrackStore();
    ~AdsBTrackStore();

    void pruneLocked(double now);
    void notify();
    static std::string hexIcao(uint32_t icao);
    static std::string photoUrlFor(const AircraftTrack& t);

    mutable std::mutex mutex_;
    std::map<uint32_t, AircraftTrack> tracks_;
    struct CprBuf {
        bool hasEven = false, hasOdd = false;
        int latE = 0, lonE = 0, latO = 0, lonO = 0;
        double tE = 0, tO = 0;
    };
    std::map<uint32_t, CprBuf> cpr_;

    double centerLat_ = -33.87;
    double centerLon_ = 151.21;
    double radiusNm_ = 120.0;
    int64_t networkUnix_ = 0;
    uint64_t localFrames_ = 0;
    uint64_t localCrcOk_ = 0;
    std::string lastStatus_ = "Aircraft map idle";
    bool networkOnline_ = false;

    std::function<void()> updateCb_;
};
