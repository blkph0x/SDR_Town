#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <deque>
#include <mutex>
#include <map>
#include <string>
#include <vector>
#include <chrono>

inline double inmarsatMonotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

enum class InmarsatMsgKind {
    Acars = 0,
    Su = 1,
    CAssign = 2,
    Egc = 3,
    Voice = 4,
    Status = 5
};

struct InmarsatMessage {
    double receivedMonotonic = 0; // Local-only receipt; never serialized or supplied by RF.
    InmarsatMsgKind kind = InmarsatMsgKind::Status;
    double unixTime = 0.0;
    double freqHz = 0.0;
    uint32_t aesId = 0;
    uint8_t gesId = 0;
    std::string icaoHex;
    bool classicAeroIdentity = false;
    std::string label;
    std::string text;
    std::string applicationProtocol;
    std::string applicationStatus;
    std::string applicationText;
    double latDeg = 0.0;
    double lonDeg = 0.0;
    bool hasPosition = false;
    bool validated = false;
    double altitudeFt = 0.0;
    double positionSecondsPastHour = 0.0;
    bool positionHasTimestamp = false;
    bool hasGroundVector = false;
    double groundTrackDeg = 0, groundSpeedKnots = 0;
    std::string registration;
    std::string callsign;
    double voiceRxHz = 0.0;
    double voiceTxHz = 0.0;

    nlohmann::json toJson() const;
    static const char* kindName(InmarsatMsgKind k);
};

struct InmarsatAircraft {
    InmarsatMessage identity;
    uint64_t messages = 0;
    double positionTime = 0;
    double lastSeenMonotonic = 0, positionMonotonic = 0;
};

class InmarsatMessageStore {
public:
    // Replay/channel pipelines own isolated stores; the singleton is live UI state.
    InmarsatMessageStore() = default;
    static InmarsatMessageStore& instance();

    void push(InmarsatMessage msg);
    std::vector<InmarsatMessage> recent(size_t limit = 100) const;
    std::vector<InmarsatMessage> positions() const;
    std::vector<InmarsatAircraft> aircraft() const;
    std::vector<InmarsatAircraft> trackingAircraft() const;
    void clearAircraft();
    nlohmann::json recentJson(size_t limit = 100, size_t offset = 0) const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::deque<InmarsatMessage> msgs_;
    std::map<uint32_t,InmarsatMessage> positions_; // DEC-0135: independent of message-log eviction.
    std::map<uint32_t,InmarsatAircraft> aircraft_; // DEC-0138: bounded, independent identity history.
    static constexpr size_t kMaxPositions = 256;
    static constexpr size_t kMax = 500;
};
