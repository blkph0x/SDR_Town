#pragma once
#include "InmarsatMessageStore.h"
#include <set>

// GUI-owner model. RF input is a value snapshot from the mutex-protected store;
// network callbacks and rendering run on the same Qt thread, never a DSP thread.
class InmarsatTracking {
public:
    // DEC-0164: explicit display/resource policy, not a radio protocol timeout.
    static constexpr double identityTtl = 1200, rfTtl = 1200, onlineTtl = 300;
    static constexpr double onlineFresh = 60, predictionLimit = 120;
    void setRf(std::vector<InmarsatAircraft> aircraft, double now);
    void setOnlineEnabled(bool enabled);
    bool onlineEnabled() const { return enabled_; }
    std::vector<uint32_t> eligibleIds(double now) const;
    // False means malformed envelope. Invalid rows are individually rejected.
    bool acceptOnline(const nlohmann::json& body, const std::set<uint32_t>& requested,
                      double now, double utc);
    nlohmann::json report(const nlohmann::json& activity, double now, bool estimates) const;
    nlohmann::json counters() const;
private:
    struct Position {
        double lat=0, lon=0, alt=0, observed=0, speed=0, track=0;
        bool hasAltitude=false, motion=false;
    };
    std::map<uint32_t, InmarsatAircraft> rf_;
    std::map<uint32_t, Position> online_;
    bool enabled_=false;
    uint64_t accepted_=0, rejected_=0, duplicate_=0;
};
