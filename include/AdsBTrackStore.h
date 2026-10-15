#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class AircraftSource { None, LocalAdsb, OpenSky, Adsc };

struct AircraftObservation {
    AircraftSource source = AircraftSource::None;
    double unixTime = 0.0;
    double monotonicTime = 0.0;
    double ageSec = -1.0;
    // OpenSky exposes contact time, not individual velocity/identity times.
    std::string timeBasis;
};

struct AircraftClockSample {
    double unixTime = 0.0;
    double monotonicTime = 0.0;
};

struct AircraftTrack {
    uint32_t icao = 0;
    std::string icaoHex;
    std::string callsign;
    double latDeg = std::numeric_limits<double>::quiet_NaN();
    double lonDeg = std::numeric_limits<double>::quiet_NaN();
    double altFt = std::numeric_limits<double>::quiet_NaN();
    bool altitudeValid = false;
    std::string altitudeReference;
    double gsKt = std::numeric_limits<double>::quiet_NaN();
    bool groundSpeedValid = false;
    double trackDeg = std::numeric_limits<double>::quiet_NaN();
    bool trackValid = false;
    double verticalRateFpm = std::numeric_limits<double>::quiet_NaN();
    bool verticalRateValid = false;
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
    AircraftObservation presence;
    AircraftObservation identity;
    AircraftObservation position;
    AircraftObservation altitude;
    AircraftObservation groundSpeed;
    AircraftObservation heading;
    AircraftObservation verticalRate;
    AircraftObservation squawkObservation;
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
    bool networkEnabled = true;
};

class AdsBTrackStore {
public:
    static AdsBTrackStore& instance();
    using Clock = std::function<AircraftClockSample()>;
    // DEC-0223/F08: replay/tests supply both clocks; expiration never uses UTC.
    explicit AdsBTrackStore(const std::string& sessionId = {}, Clock clock = {});
    ~AdsBTrackStore();

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
    bool mergeNetworkJson(const std::string& body, uint64_t generation);
    void setNetworkError(const std::string& err);
    void setNetworkError(const std::string& err, uint64_t generation);
    bool refreshNetwork(std::string* error = nullptr);
    // DEC-0161: disabling invalidates even replies from before an off/on cycle.
    void setNetworkEnabled(bool enabled);
    bool networkEnabled() const;
    uint64_t networkGeneration() const;

    AircraftMapSnapshot snapshot() const;
    AircraftTrack trackByIcao(uint32_t icao) const;
    // Shared list/detail wire contract. Callers still handle an unknown ICAO.
    static nlohmann::json trackJson(const AircraftTrack& track);
    nlohmann::json statusJson() const;
    void observer(double* lat, double* lon, double* radiusNm) const;

    void setUpdateCallback(std::function<void()> cb);

private:
    std::string settingsPrefix_;
    Clock clock_;
    AircraftClockSample clockOrigin_;

    void pruneLocked(double now);
    void notify();
    static std::string hexIcao(uint32_t icao);
    static std::string photoUrlFor(const AircraftTrack& t);
    std::map<uint32_t, AircraftTrack> visibleTracksLocked(double now) const;

    mutable std::mutex mutex_;
    std::map<uint32_t, AircraftTrack> tracks_;
    std::map<uint32_t, AircraftTrack> adscTracks_;
    std::map<uint32_t, AircraftTrack> networkTracks_;
    struct CprBuf {
        bool hasEven = false, hasOdd = false;
        int latE = 0, lonE = 0, latO = 0, lonO = 0;
        double tE = 0, tO = 0;
    };
    std::map<uint32_t, CprBuf> cpr_;

    double centerLat_ = -33.87;
    double centerLon_ = 151.21;
    double radiusNm_ = 120.0;
    double networkMonotonic_ = 0.0;
    bool hasNetworkReceipt_ = false;
    uint64_t localFrames_ = 0;
    uint64_t localCrcOk_ = 0;
    std::string lastStatus_ = "Aircraft map idle";
    bool networkOnline_ = false;
    bool networkEnabled_ = true;
    uint64_t networkGeneration_ = 1;

    std::function<void()> updateCb_;
};
