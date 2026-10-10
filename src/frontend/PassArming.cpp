#include "frontend/PassArming.h"

#include <cmath>

bool biasRatingCoversLnb(double supplyMilliamp, double lnbMaxMilliamp) {
    return std::isfinite(supplyMilliamp) && std::isfinite(lnbMaxMilliamp) &&
        supplyMilliamp >= lnbMaxMilliamp && lnbMaxMilliamp >= 0.0;
}

ArmPlan planPassArm(const PassChecklist& check, StationMission mission) {
    ArmPlan plan;
    if (!check.leaseHeld) { plan.reject = "A current radio lease is required"; return plan; }
    if (!check.ifInSdrSpan) { plan.reject = "Corrected IF is outside the SDR span"; return plan; }
    if (!check.tleFreshOrNotRequired) { plan.reject = "TLE is older than the station limit"; return plan; }
    if (!check.biasCurrentOk) { plan.reject = "Bias-T current rating is below the LNB maximum"; return plan; }
    if (mission != StationMission::Manual && !check.rotatorReadyOrOverride) {
        plan.reject = "Rotator is not armed with a fresh position";
        return plan;
    }
    if (!check.powerConfirmed) { plan.reject = "Bias-T enable requires explicit confirmation"; return plan; }
    if (check.diseqcConfigured) plan.steps.push_back("diseqc");
    plan.steps.push_back("power-on");
    if (mission == StationMission::LeoTrack) plan.steps.push_back("track");
    else if (mission == StationMission::GeoPark) plan.steps.push_back("slew-park");
    else if (mission == StationMission::GeoBoxScan) plan.steps.push_back("box-scan");
    plan.steps.push_back("tune-if");
    plan.steps.push_back("start-worker");
    plan.accepted = true;
    return plan;
}

std::vector<std::string> planPassTeardown() {
    return {"stop-worker", "rotator-stop", "rotator-park", "power-off", "pass-summary"};
}
