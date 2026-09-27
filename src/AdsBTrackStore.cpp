#include "AdsBTrackStore.h"
#include "HttpGet.h"
#include "ModeS.h"
#include "SatPassPlanner.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <QSettings>

namespace {

double unixNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

AdsBTrackStore& AdsBTrackStore::instance() {
    static AdsBTrackStore s;
    return s;
}

AdsBTrackStore::AdsBTrackStore() {
    networkEnabled_ = QSettings("SDR_Town", "SDR Town").value("aircraft/networkEnabled", true).toBool();
    const auto obs = SatPassPlanner::instance().observer();
    centerLat_ = obs.latDeg;
    centerLon_ = obs.lonDeg;
}

AdsBTrackStore::~AdsBTrackStore() = default;

void AdsBTrackStore::setUpdateCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    updateCb_ = std::move(cb);
}

void AdsBTrackStore::notify() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = updateCb_;
    }
    if (cb) cb();
}

void AdsBTrackStore::setObserver(double latDeg, double lonDeg, double radiusNm) {
    std::lock_guard<std::mutex> lk(mutex_);
    centerLat_ = latDeg;
    centerLon_ = lonDeg;
    radiusNm_ = std::max(20.0, radiusNm);
}

void AdsBTrackStore::observer(double* lat, double* lon, double* radiusNm) const {
    std::lock_guard<std::mutex> lk(mutex_);
    if (lat) *lat = centerLat_;
    if (lon) *lon = centerLon_;
    if (radiusNm) *radiusNm = radiusNm_;
}

std::string AdsBTrackStore::hexIcao(uint32_t icao) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06X", icao & 0xFFFFFFu);
    return buf;
}

std::string AdsBTrackStore::photoUrlFor(const AircraftTrack& t) {
    if (!t.typeCode.empty()) {
        return std::string("https://cdn.planespotters.net/img/airliners/400x/") + t.typeCode + ".jpg";
    }
    if (!t.icaoHex.empty()) {
        return std::string("https://api.planespotters.net/pub/photos/hex/") + t.icaoHex;
    }
    return {};
}

void AdsBTrackStore::pruneLocked(double now) {
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if (now - it->second.lastSeenUnix > 120.0) it = tracks_.erase(it);
        else ++it;
    }
    for (auto it = networkTracks_.begin(); it != networkTracks_.end();) {
        if (now - it->second.lastSeenUnix > 120.0) it = networkTracks_.erase(it);
        else ++it;
    }
    for (auto it = cpr_.begin(); it != cpr_.end();) {
        if (now - std::max(it->second.tE, it->second.tO) > 120.0) it = cpr_.erase(it);
        else ++it;
    }
}

void AdsBTrackStore::ingestAdscPosition(uint32_t icao, double latDeg, double lonDeg,
                                        const std::string& icaoHex,
                                        const std::string& callsign) {
    if (icao == 0 || !std::isfinite(latDeg) || !std::isfinite(lonDeg)) return;
    if (std::abs(latDeg) > 90.0 || std::abs(lonDeg) > 180.0) return;
    const double now = unixNow();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pruneLocked(now);
        auto& t = tracks_[icao];
        t.icao = icao;
        t.icaoHex = icaoHex.empty() ? hexIcao(icao) : icaoHex;
        if (!callsign.empty()) t.callsign = callsign;
        t.latDeg = latDeg;
        t.lonDeg = lonDeg;
        t.positionValid = true;
        t.fromAdsc = true;
        t.lastSeenUnix = now;
        if (t.photoUrl.empty()) t.photoUrl = photoUrlFor(t);
        lastStatus_ = "ADS-C position " + t.icaoHex;
    }
    notify();
}

void AdsBTrackStore::ingestModeSFrame(const uint8_t* msg14) {
    if (!msg14 || !ModeS::crcOk(msg14, 14)) return;
    const int df = ModeS::downlinkFormat(msg14);
    if (df != 17 && df != 18) return;
    const uint32_t icao = ModeS::icaoAddress(msg14);
    const double now = unixNow();
    const int tc = ModeS::typeCode(msg14);

    std::lock_guard<std::mutex> lk(mutex_);
    pruneLocked(now);
    ++localFrames_;
    ++localCrcOk_;
    auto& t = tracks_[icao];
    t.icao = icao;
    t.icaoHex = hexIcao(icao);
    t.fromLocal = true;
    t.lastSeenUnix = now;

    if (tc >= 1 && tc <= 4) {
        auto id = ModeS::decodeIdentity(msg14);
        if (id.valid) t.callsign = id.callsign;
    } else if (tc >= 9 && tc <= 18) {
        const bool isOdd = (msg14[6] & 0x04) != 0;
        const int latCpr = ((msg14[6] & 3) << 15) | (msg14[7] << 7) | (msg14[8] >> 1);
        const int lonCpr = ((msg14[8] & 1) << 16) | (msg14[9] << 8) | msg14[10];
        auto& c = cpr_[icao];
        if (isOdd) {
            c.hasOdd = true;
            c.latO = latCpr;
            c.lonO = lonCpr;
            c.tO = now;
        } else {
            c.hasEven = true;
            c.latE = latCpr;
            c.lonE = lonCpr;
            c.tE = now;
        }
        if (c.hasEven && c.hasOdd && std::abs(c.tE - c.tO) < 10.0) {
            auto pos = ModeS::decodeCprPair(c.tE >= c.tO, c.latE, c.lonE, true, c.latO, c.lonO);
            if (!pos.valid) {
                pos = ModeS::decodeCprLocal(isOdd, latCpr, lonCpr, centerLat_, centerLon_);
            }
            if (pos.valid) {
                t.latDeg = pos.latDeg;
                t.lonDeg = pos.lonDeg;
                t.positionValid = true;
            }
        }
    } else if (tc >= 19 && tc <= 22) {
        auto vel = ModeS::decodeVelocity(msg14);
        if (vel.valid) {
            t.gsKt = vel.groundSpeedKt;
            t.trackDeg = vel.trackDeg;
            t.verticalRateFpm = vel.verticalRateFpm;
        }
    }
    if (t.photoUrl.empty()) t.photoUrl = photoUrlFor(t);
    lastStatus_ = "Local ADS-B frames";
}

void AdsBTrackStore::processMagnitude(const float* mag, size_t n, double sampleRateHz) {
    // Cap work: Mode-S preamble scan is O(n); never accept huge GUI-thread windows.
    if (!mag || n == 0) return;
    const size_t maxN = 16384;
    if (n > maxN) {
        mag = mag + (n - maxN);
        n = maxN;
    }
    auto frames = ModeS::extractFramesFromMagnitude(mag, n, sampleRateHz);
    for (const auto& f : frames) {
        if (f.size() == 14) ingestModeSFrame(f.data());
    }
    // Do NOT notify here — callers refresh UI on their own timer to avoid feedback loops.
}

bool AdsBTrackStore::refreshNetwork(std::string* error) {
    const auto generation = networkGeneration();
    if (!networkEnabled()) {
        if (error) *error = "Internet aircraft disabled";
        return false;
    }
    double lat = 0, lon = 0, nm = 120.0;
    observer(&lat, &lon, &nm);
    const double dlat = nm / 60.0;
    const double cosLat = std::max(0.2, std::cos(lat * 3.14159265358979323846 / 180.0));
    const double dlon = nm / (60.0 * cosLat);
    char url[512];
    std::snprintf(url, sizeof(url),
                  "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
                  lat - dlat, lon - dlon, lat + dlat, lon + dlon);
    std::string body, err;
    if (!httpGetUrl(url, &body, &err, 20000)) {
        setNetworkError(err, generation);
        if (error) *error = err;
        return false;
    }
    const bool accepted = mergeNetworkJson(body, generation);
    if (!accepted && error) *error = "Aircraft response invalid or request cancelled";
    return accepted;
}

void AdsBTrackStore::mergeNetworkJson(const std::string& body) {
    mergeNetworkJson(body, networkGeneration());
}

bool AdsBTrackStore::mergeNetworkJson(const std::string& body, uint64_t generation) {
    try {
        // DEC-0161: bound untrusted responses and parse before modifying source state.
        if (body.size() > 2 * 1024 * 1024) throw std::runtime_error("response too large");
        auto j = nlohmann::json::parse(body);
        if (!j.is_object() || !j.contains("states") ||
            !(j["states"].is_array() || j["states"].is_null())) return false;
        const double now = unixNow();
        std::map<uint32_t, AircraftTrack> incoming;
        for (const auto& st : j["states"]) {
            // OpenSky REST state vector: lon=5, lat=6, squawk=14 (not 16).
            if (!st.is_array() || st.size() < 12 || !st[0].is_string()) continue;
            const auto key = st[0].get<std::string>();
            if (key.size() != 6 || key.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) continue;
            if (!st[5].is_number() || !st[6].is_number()) continue;
            AircraftTrack t;
            t.icao = static_cast<uint32_t>(std::stoul(key, nullptr, 16));
            if (!t.icao) continue;
            t.icaoHex = hexIcao(t.icao);
            t.lonDeg = st[5].get<double>();
            t.latDeg = st[6].get<double>();
            if (!std::isfinite(t.latDeg) || !std::isfinite(t.lonDeg) ||
                std::abs(t.latDeg) > 90 || std::abs(t.lonDeg) > 180) continue;
            t.fromNetwork = t.positionValid = true;
            t.lastSeenUnix = now;
            if (st[1].is_string()) {
                t.callsign = st[1].get<std::string>().substr(0, 32);
                while (!t.callsign.empty() && t.callsign.back() == ' ') t.callsign.pop_back();
            }
            if (st[7].is_number()) t.altFt = st[7].get<double>() * 3.28084;
            if (st[9].is_number()) t.gsKt = st[9].get<double>() * 1.94384;
            if (st[10].is_number()) t.trackDeg = st[10].get<double>();
            if (st[11].is_number()) t.verticalRateFpm = st[11].get<double>() * 196.85;
            if (st.size() > 14 && st[14].is_string()) t.squawk = st[14].get<std::string>().substr(0, 8);
            t.photoUrl = photoUrlFor(t);
            incoming[t.icao] = std::move(t);
            if (incoming.size() >= 4000) break;
        }
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!networkEnabled_ || generation != networkGeneration_) return false;
            networkTracks_ = std::move(incoming);
            networkUnix_ = int64_t(now);
            networkOnline_ = true;
            pruneLocked(now);
            lastStatus_ = "OpenSky enrichment OK";
        }
        notify();
        return true;
    } catch (...) {
        setNetworkError("OpenSky parse failed", generation);
        return false;
    }
}

void AdsBTrackStore::setNetworkError(const std::string& err) {
    setNetworkError(err, networkGeneration());
}

void AdsBTrackStore::setNetworkError(const std::string& err, uint64_t generation) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!networkEnabled_ || generation != networkGeneration_) return;
        networkOnline_ = false;
        lastStatus_ = err.empty() ? "OpenSky error" : err;
    }
    notify();
}

void AdsBTrackStore::setNetworkEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (enabled == networkEnabled_) return;
        networkEnabled_ = enabled;
        ++networkGeneration_;
        networkTracks_.clear();
        networkOnline_ = false;
        networkUnix_ = 0;
        lastStatus_ = enabled ? "Internet aircraft enabled" : "Internet aircraft disabled; network tracks removed";
        QSettings("SDR_Town", "SDR Town").setValue("aircraft/networkEnabled", enabled);
    }
    notify();
}

bool AdsBTrackStore::networkEnabled() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return networkEnabled_;
}

uint64_t AdsBTrackStore::networkGeneration() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return networkGeneration_;
}

std::map<uint32_t, AircraftTrack> AdsBTrackStore::visibleTracksLocked(double now) const {
    std::map<uint32_t, AircraftTrack> result;
    if (networkEnabled_) for (const auto& [id, track] : networkTracks_)
        if (now - track.lastSeenUnix <= 120.0) result[id] = track;
    for (const auto& [id, local] : tracks_) {
        if (now - local.lastSeenUnix > 120.0) continue;
        // Keep RF fields authoritative and stored separately. A network position
        // may fill a missing local position, but must never masquerade as RF.
        auto it = result.find(id);
        if (it == result.end() || local.positionValid) result[id] = local;
        else {
            it->second.fromLocal = local.fromLocal;
            it->second.fromAdsc = local.fromAdsc;
            if (!local.callsign.empty()) it->second.callsign = local.callsign;
        }
    }
    return result;
}

AircraftMapSnapshot AdsBTrackStore::snapshot() const {
    AircraftMapSnapshot s;
    std::lock_guard<std::mutex> lk(mutex_);
    s.centerLat = centerLat_;
    s.centerLon = centerLon_;
    s.radiusNm = radiusNm_;
    s.localFrames = localFrames_;
    s.localCrcOk = localCrcOk_;
    s.lastStatus = lastStatus_;
    s.networkOnline = networkOnline_;
    s.networkEnabled = networkEnabled_;
    if (networkUnix_ > 0) s.networkAgeSec = int64_t(unixNow()) - networkUnix_;
    for (const auto& kv : visibleTracksLocked(unixNow())) {
        if (kv.second.positionValid) s.tracks.push_back(kv.second);
    }
    if (s.tracks.size() > 400) s.tracks.resize(400);
    return s;
}

AircraftTrack AdsBTrackStore::trackByIcao(uint32_t icao) const {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto visible = visibleTracksLocked(unixNow());
    auto it = visible.find(icao);
    if (it == visible.end()) return {};
    return it->second;
}

nlohmann::json AdsBTrackStore::statusJson() const {
    const auto s = snapshot();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : s.tracks) {
        arr.push_back({{"icao", t.icaoHex},
                       {"callsign", t.callsign},
                       {"lat", t.latDeg},
                       {"lon", t.lonDeg},
                       {"altFt", t.altFt},
                       {"gsKt", t.gsKt},
                       {"trackDeg", t.trackDeg},
                       {"vrateFpm", t.verticalRateFpm},
                       {"squawk", t.squawk},
                       {"type", t.typeCode},
                       {"route", t.route},
                       {"photoUrl", t.photoUrl},
                       {"fromLocal", t.fromLocal},
                       {"fromNetwork", t.fromNetwork},
                       {"fromAdsc", t.fromAdsc},
                       {"lastSeenUnix", t.lastSeenUnix}});
    }
    return {{"tracks", arr},
            {"centerLat", s.centerLat},
            {"centerLon", s.centerLon},
            {"radiusNm", s.radiusNm},
            {"networkAgeSec", s.networkAgeSec},
            {"networkOnline", s.networkOnline},
            {"networkEnabled", s.networkEnabled},
            {"localFrames", s.localFrames},
            {"localCrcOk", s.localCrcOk},
            {"lastStatus", s.lastStatus}};
}
