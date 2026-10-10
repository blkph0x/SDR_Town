#pragma once

#include <string>
#include <vector>

enum class StationMission { LeoTrack, GeoPark, GeoBoxScan, Manual };

struct PassChecklist {
    bool leaseHeld = false;
    bool ifInSdrSpan = false;
    bool tleFreshOrNotRequired = false;
    bool biasCurrentOk = false;
    bool rotatorReadyOrOverride = false;
    bool powerConfirmed = false;
    bool diseqcConfigured = false;
};

struct ArmPlan {
    bool accepted = false;
    std::string reject;
    std::vector<std::string> steps;
};

ArmPlan planPassArm(const PassChecklist& check, StationMission mission);
std::vector<std::string> planPassTeardown();
bool biasRatingCoversLnb(double supplyMilliamp, double lnbMaxMilliamp);
