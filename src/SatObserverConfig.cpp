#include "SatObserverConfig.h"

#include <QDir>
#include <QStandardPaths>

#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

std::string satcomDataDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base + "/satcom");
    return (base + "/satcom").toStdString();
}

nlohmann::json SatObserverConfig::toJson() const {
    return {
        {"latDeg", latDeg},
        {"lonDeg", lonDeg},
        {"altM", altM},
        {"minElevationDeg", minElevationDeg},
        {"configured", configured},
    };
}

SatObserverConfig SatObserverConfig::fromJson(const nlohmann::json& j) {
    SatObserverConfig c;
    if (!j.is_object()) return c;
    c.latDeg = j.value("latDeg", 0.0);
    c.lonDeg = j.value("lonDeg", 0.0);
    c.altM = j.value("altM", 0.0);
    c.minElevationDeg = j.value("minElevationDeg", 10.0);
    c.configured = j.value("configured", c.latDeg != 0.0 || c.lonDeg != 0.0);
    if (!c.valid()) { c.configured = false; return SatObserverConfig{}; }
    if (c.latDeg > 90.0) c.latDeg = 90.0;
    if (c.latDeg < -90.0) c.latDeg = -90.0;
    if (c.lonDeg > 180.0) c.lonDeg = 180.0;
    if (c.lonDeg < -180.0) c.lonDeg = -180.0;
    if (c.minElevationDeg < 0.0) c.minElevationDeg = 0.0;
    if (c.minElevationDeg > 90.0) c.minElevationDeg = 90.0;
    return c;
}

void SatObserverConfig::load() {
    try {
        std::ifstream in(satcomDataDir() + "/observer.json");
        if (!in) return;
        nlohmann::json j;
        in >> j;
        *this = fromJson(j);
    } catch (...) {
    }
}

void SatObserverConfig::save() const {
    try {
        std::ofstream out(satcomDataDir() + "/observer.json");
        out << toJson().dump(2);
    } catch (...) {
    }
}

bool SatObserverConfig::parseLatLonToken(const std::string& token, bool isLat, double* outDeg) {
    if (!outDeg || token.empty()) return false;
    std::string s;
    s.reserve(token.size());
    for (char ch : token) {
        if (!std::isspace(static_cast<unsigned char>(ch))) s.push_back(ch);
    }
    if (s.empty()) return false;

    int hemi = 0; // +1 N/E, -1 S/W
    char last = static_cast<char>(std::toupper(static_cast<unsigned char>(s.back())));
    if (last == 'N' || last == 'S' || last == 'E' || last == 'W') {
        if (isLat && (last == 'E' || last == 'W')) return false;
        if (!isLat && (last == 'N' || last == 'S')) return false;
        if (last == 'S' || last == 'W') hemi = -1;
        else hemi = 1;
        s.pop_back();
    }
    if (s.empty()) return false;

    try {
        size_t consumed = 0;
        double v = std::stod(s, &consumed);
        if (consumed != s.size() || !std::isfinite(v)) return false;
        if (hemi != 0) {
            v = std::abs(v) * static_cast<double>(hemi);
        }
        if (isLat) {
            if (v < -90.0 || v > 90.0) return false;
        } else {
            if (v < -180.0 || v > 180.0) return false;
        }
        *outDeg = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool SatObserverConfig::valid() const {
    return std::isfinite(latDeg) && std::abs(latDeg) <= 90 &&
           std::isfinite(lonDeg) && std::abs(lonDeg) <= 180 &&
           std::isfinite(altM) && altM >= -500 && altM <= 10000 &&
           std::isfinite(minElevationDeg) && minElevationDeg >= 0 && minElevationDeg < 90;
}
