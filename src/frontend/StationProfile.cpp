#include "frontend/StationProfile.h"

#include <nlohmann/json.hpp>

namespace {
const char* missionName(StationMission mission) {
    switch (mission) {
    case StationMission::LeoTrack: return "leo-track";
    case StationMission::GeoPark: return "geo-park";
    case StationMission::GeoBoxScan: return "geo-box";
    default: return "manual";
    }
}
StationMission missionFrom(const std::string& name) {
    if (name == "leo-track") return StationMission::LeoTrack;
    if (name == "geo-park") return StationMission::GeoPark;
    if (name == "geo-box") return StationMission::GeoBoxScan;
    return StationMission::Manual;
}
const char* biasName(BiasBackend backend) {
    switch (backend) {
    case BiasBackend::SdrInternal: return "sdr-internal";
    case BiasBackend::External: return "external";
    default: return "none";
    }
}
BiasBackend biasFrom(const std::string& name) {
    if (name == "sdr-internal") return BiasBackend::SdrInternal;
    if (name == "external") return BiasBackend::External;
    return BiasBackend::None;
}
}

std::string stationProfileToJson(const StationProfile& profile) {
    nlohmann::json j = {
        {"name", profile.name},
        {"dishCm", profile.dishCm},
        {"minElevationDeg", profile.minElevationDeg},
        {"tleMaxAgeHours", profile.tleMaxAgeHours},
        {"biasSupplyMa", profile.biasSupplyMa},
        {"biasBackend", biasName(profile.biasBackend)},
        {"mission", missionName(profile.mission)},
        {"magneticNorth", profile.magneticNorth},
        {"backlashDeg", profile.backlashDeg},
        {"parkAz", profile.parkAz},
        {"parkEl", profile.parkEl},
        {"trueRfHz", profile.trueRfHz},
        {"horizontal", profile.horizontal},
        {"highBand", profile.highBand},
        {"lnb", {
            {"loLowHz", profile.lnb.loLowHz},
            {"loHighHz", profile.lnb.loHighHz},
            {"inverted", profile.lnb.inverted},
            {"noiseFigureDb", profile.lnb.noiseFigureDb},
            {"noiseTempK", profile.lnb.noiseTempK},
            {"calibrationOffsetDb", profile.lnb.calibrationOffsetDb},
            {"maxCurrentMa", profile.lnb.maxCurrentMa},
            {"toneSelectsHighBand", profile.lnb.toneSelectsHighBand},
            {"loKind", profile.lnb.loKind},
            {"diseqcHex", profile.lnb.diseqcHex},
            {"noiseClaimCaution", claimedNoiseFigureNeedsCaution(profile.lnb.noiseFigureDb)},
        }},
    };
    return j.dump(2);
}

bool stationProfileFromJson(const std::string& json, StationProfile* profile, std::string* error) {
    if (!profile) return false;
    nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object()) {
        if (error) *error = "Station profile is not an object";
        return false;
    }
    StationProfile next;
    next.name = j.value("name", next.name);
    next.dishCm = j.value("dishCm", next.dishCm);
    next.minElevationDeg = j.value("minElevationDeg", next.minElevationDeg);
    next.tleMaxAgeHours = j.value("tleMaxAgeHours", next.tleMaxAgeHours);
    next.biasSupplyMa = j.value("biasSupplyMa", next.biasSupplyMa);
    next.biasBackend = biasFrom(j.value("biasBackend", std::string("none")));
    next.mission = missionFrom(j.value("mission", std::string("manual")));
    next.magneticNorth = j.value("magneticNorth", false);
    next.backlashDeg = j.value("backlashDeg", 0.0);
    next.parkAz = j.value("parkAz", 0.0);
    next.parkEl = j.value("parkEl", 0.0);
    next.trueRfHz = j.value("trueRfHz", next.trueRfHz);
    next.horizontal = j.value("horizontal", false);
    next.highBand = j.value("highBand", false);
    if (j.contains("lnb") && j["lnb"].is_object()) {
        const auto& lnb = j["lnb"];
        next.lnb.loLowHz = lnb.value("loLowHz", next.lnb.loLowHz);
        next.lnb.loHighHz = lnb.value("loHighHz", next.lnb.loHighHz);
        next.lnb.inverted = lnb.value("inverted", next.lnb.inverted);
        next.lnb.noiseFigureDb = lnb.value("noiseFigureDb", next.lnb.noiseFigureDb);
        next.lnb.noiseTempK = lnb.value("noiseTempK", next.lnb.noiseTempK);
        next.lnb.calibrationOffsetDb = lnb.value("calibrationOffsetDb", 0.0);
        next.lnb.maxCurrentMa = lnb.value("maxCurrentMa", next.lnb.maxCurrentMa);
        next.lnb.toneSelectsHighBand = lnb.value("toneSelectsHighBand", true);
        next.lnb.loKind = lnb.value("loKind", std::string("unknown"));
        next.lnb.diseqcHex = lnb.value("diseqcHex", std::string());
    }
    *profile = next;
    if (error) error->clear();
    return true;
}
