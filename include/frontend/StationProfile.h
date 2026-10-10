#pragma once

#include "frontend/FrontEndPower.h"
#include "frontend/LnbConversion.h"
#include "frontend/PassArming.h"

#include <string>

struct StationProfile {
    std::string name = "station";
    double dishCm = 0.0;
    double minElevationDeg = 10.0;
    int tleMaxAgeHours = 72;
    double biasSupplyMa = 0.0;
    BiasBackend biasBackend = BiasBackend::None;
    StationMission mission = StationMission::Manual;
    bool magneticNorth = false;
    double backlashDeg = 0.0;
    double parkAz = 0.0;
    double parkEl = 0.0;
    double boxSpanAzDeg = 2.0;
    double boxSpanElDeg = 1.0;
    double boxStepDeg = 0.5;
    int radioIndex = -1;
    double trueRfHz = 11.7e9;
    bool horizontal = false;
    bool highBand = false;
    LnbProfile lnb;
};

std::string stationProfileToJson(const StationProfile& profile);
bool stationProfileFromJson(const std::string& json, StationProfile* profile, std::string* error);
