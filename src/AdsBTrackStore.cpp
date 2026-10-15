#include "AdsBTrackStore.h"
#include "HttpGet.h"
#include "ModeS.h"
#include "SatPassPlanner.h"
#include "WorkflowSessionId.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <QSettings>

namespace {

AircraftClockSample clockNow() {
    return {std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(),
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()};
}

// DEC-0223/F08: retain the existing 120 s retention policy, independently for
// every field and source. Unrelated traffic must not renew an observation.
constexpr double kRetentionSec = 120.0;

AircraftObservation observation(AircraftSource source, const AircraftClockSample& now) {
    return {source, now.unixTime, now.monotonicTime, 0.0, "receipt"};
}

bool fresh(const AircraftObservation& field, double now) {
    return field.source != AircraftSource::None && now >= field.monotonicTime &&
           now - field.monotonicTime <= kRetentionSec;
}

bool newer(const AircraftObservation& candidate, const AircraftObservation& current) {
    return candidate.source != AircraftSource::None &&
           (current.source == AircraftSource::None ||
            candidate.monotonicTime >= current.monotonicTime);
}

const char* sourceName(AircraftSource source) {
    switch (source) {
    case AircraftSource::LocalAdsb: return "local_adsb";
    case AircraftSource::OpenSky: return "opensky";
    case AircraftSource::Adsc: return "adsc";
    default: return "unknown";
    }
}

nlohmann::json fieldJson(const AircraftObservation& field) {
    if (field.source == AircraftSource::None) return nullptr;
    return {{"source", sourceName(field.source)}, {"unixTime", field.unixTime},
            {"ageSec", field.ageSec}, {"timeBasis", field.timeBasis}};
}

} // namespace

AdsBTrackStore& AdsBTrackStore::instance() {
    static AdsBTrackStore s;
    return s;
}

AdsBTrackStore::AdsBTrackStore(const std::string& sessionId, Clock clock)
    : clock_(clock ? std::move(clock) : Clock(clockNow)) {
    clockOrigin_ = clock_();
    const auto id = normalizedWorkflowSessionId(sessionId);
    settingsPrefix_ = id.empty() ? "aircraft/" : "aircraft/sessions/" + id + "/";
    networkEnabled_ = QSettings("SDR_Town", "SDR Town").value(QString::fromStdString(settingsPrefix_) + "networkEnabled", id.empty()).toBool();
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
    for (auto* tracks : {&tracks_, &adscTracks_, &networkTracks_}) {
        for (auto it = tracks->begin(); it != tracks->end();) {
            if (!fresh(it->second.presence, now)) it = tracks->erase(it);
            else ++it;
        }
    }
    for (auto it = cpr_.begin(); it != cpr_.end();) {
        if (now - std::max(it->second.tE, it->second.tO) > kRetentionSec) it = cpr_.erase(it);
        else ++it;
    }
}

void AdsBTrackStore::ingestAdscPosition(uint32_t icao, double latDeg, double lonDeg,
                                        const std::string& icaoHex,
                                        const std::string& callsign) {
    if (icao == 0 || icao > 0xFFFFFFu || !std::isfinite(latDeg) || !std::isfinite(lonDeg)) return;
    if (std::abs(latDeg) > 90.0 || std::abs(lonDeg) > 180.0) return;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto now = clock_();
        pruneLocked(now.monotonicTime);
        auto& t = adscTracks_[icao];
        t.icao = icao;
        t.icaoHex = icaoHex.empty() ? hexIcao(icao) : icaoHex;
        if (!callsign.empty()) {
            t.callsign = callsign;
            t.identity = observation(AircraftSource::Adsc, now);
        }
        t.latDeg = latDeg;
        t.lonDeg = lonDeg;
        t.positionValid = true;
        t.position = observation(AircraftSource::Adsc, now);
        t.fromAdsc = true;
        t.presence = observation(AircraftSource::Adsc, now);
        t.lastSeenUnix = now.unixTime;
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
    if (!icao) return;
    const int tc = ModeS::typeCode(msg14);

    std::lock_guard<std::mutex> lk(mutex_);
    const auto now = clock_();
    pruneLocked(now.monotonicTime);
    ++localFrames_;
    ++localCrcOk_;
    auto& t = tracks_[icao];
    t.icao = icao;
    t.icaoHex = hexIcao(icao);
    t.fromLocal = true;
    t.lastSeenUnix = now.unixTime;
    t.presence = observation(AircraftSource::LocalAdsb, now);

    if (tc >= 1 && tc <= 4) {
        auto id = ModeS::decodeIdentity(msg14);
        if (id.valid) {
            t.callsign = id.callsign;
            t.identity = observation(AircraftSource::LocalAdsb, now);
        }
    } else if ((tc >= 9 && tc <= 18) || (tc >= 20 && tc <= 22)) {
        // DEC-0197: both barometric and geometric-altitude reports carry CPR.
        const bool isOdd = (msg14[6] & 0x04) != 0;
        const int latCpr = ((msg14[6] & 3) << 15) | (msg14[7] << 7) | (msg14[8] >> 1);
        const int lonCpr = ((msg14[8] & 1) << 16) | (msg14[9] << 8) | msg14[10];
        auto& c = cpr_[icao];
        if (isOdd) {
            c.hasOdd = true;
            c.latO = latCpr;
            c.lonO = lonCpr;
            c.tO = now.monotonicTime;
        } else {
            c.hasEven = true;
            c.latE = latCpr;
            c.lonE = lonCpr;
            c.tE = now.monotonicTime;
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
                t.position = observation(AircraftSource::LocalAdsb, now);
            }
        }
    } else if (tc == 19) {
        auto vel = ModeS::decodeVelocity(msg14);
        if (vel.valid) {
            t.gsKt = vel.groundSpeedKt;
            t.groundSpeedValid = std::isfinite(t.gsKt) && t.gsKt >= 0.0;
            t.groundSpeed = observation(AircraftSource::LocalAdsb, now);
            t.trackDeg = vel.trackDeg;
            // A zero horizontal vector has no direction, even though atan2(0,0)
            // happens to return zero in the existing decoder.
            t.trackValid = t.gsKt > 0.0 && std::isfinite(vel.trackDeg) && vel.trackDeg >= 0 && vel.trackDeg < 360;
            t.heading = t.trackValid ? observation(AircraftSource::LocalAdsb, now) : AircraftObservation{};
        }
        // ModeS::decodeVelocity already decodes this value, but its aggregate
        // valid flag is horizontal-only and its missing vertical rate is zero.
        // Preserve the wire presence independently, including vertical-only frames.
        const int subtype = msg14[4] & 7;
        const int rate = ((msg14[8] & 7) << 6) | ((msg14[9] >> 2) & 0x3F);
        if ((subtype == 1 || subtype == 2) && rate != 0) {
            t.verticalRateFpm = (rate - 1) * 64.0 * ((msg14[8] & 8) ? -1.0 : 1.0);
            t.verticalRateValid = true;
            t.verticalRate = observation(AircraftSource::LocalAdsb, now);
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
        std::unique_lock<std::mutex> lk(mutex_);
        if (!networkEnabled_ || generation != networkGeneration_) return false;
        const auto now = clock_();
        // Convert remote UTC observations against one steady-clock anchor. OS
        // wall-clock corrections must neither revive nor expire received data.
        const double sourceNow = clockOrigin_.unixTime + now.monotonicTime - clockOrigin_.monotonicTime;
        std::map<uint32_t, AircraftTrack> incoming;
        for (const auto& st : j["states"]) {
            // https://openskynetwork.github.io/opensky-api/rest.html#response
            // time_position=3, last_contact=4; neither HTTP receipt nor identity
            // traffic establishes that the coordinates were observed again.
            if (!st.is_array() || st.size() < 12 || !st[0].is_string()) continue;
            const auto key = st[0].get<std::string>();
            if (key.size() != 6 || key.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) continue;
            auto number = [&](size_t index) {
                return index < st.size() && st[index].is_number() && std::isfinite(st[index].get<double>());
            };
            if (!number(4) || st[4].get<double>() <= 0.0 || st[4].get<double>() > sourceNow) continue;
            AircraftTrack t;
            t.icao = static_cast<uint32_t>(std::stoul(key, nullptr, 16));
            if (!t.icao) continue;
            t.icaoHex = hexIcao(t.icao);
            const auto old = networkTracks_.find(t.icao);
            const AircraftTrack* previous = old == networkTracks_.end() ? nullptr : &old->second;
            auto remoteObservation = [&](size_t index, const char* basis,
                                         const AircraftObservation* prior) {
                AircraftObservation field;
                if (!number(index)) return field;
                const double stamp = st[index].get<double>();
                if (stamp <= 0.0 || stamp > sourceNow) return field;
                field = {AircraftSource::OpenSky, stamp,
                         now.monotonicTime - (sourceNow - stamp), 0.0, basis};
                // Repeated source timestamps never get younger after a UTC jump.
                if (prior && prior->source != AircraftSource::None && stamp <= prior->unixTime)
                    field.monotonicTime = std::min(field.monotonicTime, prior->monotonicTime);
                return field;
            };
            t.presence = remoteObservation(4, "last_contact", previous ? &previous->presence : nullptr);
            if (!fresh(t.presence, now.monotonicTime)) continue;
            t.fromNetwork = true;
            t.lastSeenUnix = t.presence.unixTime;
            if ((!st[5].is_null() && !number(5)) || (!st[6].is_null() && !number(6))) continue;
            if (number(5) && number(6)) {
                t.lonDeg = st[5].get<double>();
                t.latDeg = st[6].get<double>();
                if (std::abs(t.latDeg) > 90 || std::abs(t.lonDeg) > 180) continue;
                t.position = remoteObservation(3, "time_position", previous ? &previous->position : nullptr);
                t.positionValid = t.position.source != AircraftSource::None && t.position.unixTime <= t.presence.unixTime;
                if (!t.positionValid) t.position = {};
            }
            // OpenSky supplies no per-field timestamp for these values. Start a
            // conservative age at last_contact; unchanged cached values retain
            // their first observation instead of being renewed by other traffic.
            auto stateObservation = [&](bool unchanged, const AircraftObservation* prior) {
                if (unchanged && prior && prior->source != AircraftSource::None) return *prior;
                return t.presence;
            };
            if (st[1].is_string()) {
                t.callsign = st[1].get<std::string>().substr(0, 32);
                while (!t.callsign.empty() && t.callsign.back() == ' ') t.callsign.pop_back();
                if (!t.callsign.empty()) t.identity = stateObservation(previous && previous->callsign == t.callsign,
                                                                      previous ? &previous->identity : nullptr);
            }
            if (number(7)) {
                t.altFt = st[7].get<double>() * 3.28084;
                t.altitudeReference = "barometric";
            } else if (number(13)) {
                t.altFt = st[13].get<double>() * 3.28084;
                t.altitudeReference = "geometric";
            }
            if (std::isfinite(t.altFt)) {
                t.altitudeValid = true;
                t.altitude = stateObservation(previous && previous->altFt == t.altFt &&
                    previous->altitudeReference == t.altitudeReference, previous ? &previous->altitude : nullptr);
            }
            if (number(9) && st[9].get<double>() >= 0) {
                t.gsKt = st[9].get<double>() * 1.94384;
                t.groundSpeedValid = std::isfinite(t.gsKt);
                if (t.groundSpeedValid) t.groundSpeed = stateObservation(previous && previous->gsKt == t.gsKt,
                                                                       previous ? &previous->groundSpeed : nullptr);
            }
            if (number(10)) {
                t.trackDeg = st[10].get<double>();
                t.trackValid = t.trackDeg >= 0 && t.trackDeg < 360;
                if (t.trackValid) t.heading = stateObservation(previous && previous->trackDeg == t.trackDeg,
                                                               previous ? &previous->heading : nullptr);
            }
            if (number(11)) {
                t.verticalRateFpm = st[11].get<double>() * 196.85;
                t.verticalRateValid = std::isfinite(t.verticalRateFpm);
                if (t.verticalRateValid) t.verticalRate = stateObservation(previous && previous->verticalRateFpm == t.verticalRateFpm,
                                                                          previous ? &previous->verticalRate : nullptr);
            }
            if (st.size() > 14 && st[14].is_string()) {
                t.squawk = st[14].get<std::string>().substr(0, 8);
                if (!t.squawk.empty()) t.squawkObservation = stateObservation(previous && previous->squawk == t.squawk,
                                                                             previous ? &previous->squawkObservation : nullptr);
            }
            t.photoUrl = photoUrlFor(t);
            incoming[t.icao] = std::move(t);
            if (incoming.size() >= 4000) break;
        }
        networkTracks_ = std::move(incoming);
        networkMonotonic_ = now.monotonicTime;
        hasNetworkReceipt_ = true;
        networkOnline_ = true;
        pruneLocked(now.monotonicTime);
        lastStatus_ = "OpenSky enrichment OK";
        lk.unlock();
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
        hasNetworkReceipt_ = false;
        lastStatus_ = enabled ? "Internet aircraft enabled" : "Internet aircraft disabled; network tracks removed";
        QSettings("SDR_Town", "SDR Town").setValue(QString::fromStdString(settingsPrefix_) + "networkEnabled", enabled);
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
    // DEC-0223/F08: choose the newest observation of each field, not the newest
    // message about the aircraft. Only exact ties prefer local ADS-B over ADS-C
    // over OpenSky. Source stores remain independent for immediate network-off.
    for (const auto* tracks : {&networkTracks_, &adscTracks_, &tracks_}) {
        if (tracks == &networkTracks_ && !networkEnabled_) continue;
        for (const auto& [id, candidate] : *tracks) {
            if (!fresh(candidate.presence, now)) continue;
            auto& out = result[id];
            out.icao = id;
            out.icaoHex = candidate.icaoHex;
            out.photoUrl = candidate.photoUrl;
            auto take = [&](AircraftObservation AircraftTrack::* field, auto copy) {
                if (newer(candidate.*field, out.*field)) {
                    out.*field = candidate.*field;
                    copy();
                }
            };
            take(&AircraftTrack::presence, [&] { out.lastSeenUnix = candidate.lastSeenUnix; });
            take(&AircraftTrack::identity, [&] { out.callsign = candidate.callsign; });
            take(&AircraftTrack::position, [&] {
                out.latDeg = candidate.latDeg; out.lonDeg = candidate.lonDeg;
                out.positionValid = candidate.positionValid;
            });
            take(&AircraftTrack::altitude, [&] {
                out.altFt = candidate.altFt; out.altitudeValid = candidate.altitudeValid;
                out.altitudeReference = candidate.altitudeReference;
            });
            take(&AircraftTrack::groundSpeed, [&] {
                out.gsKt = candidate.gsKt; out.groundSpeedValid = candidate.groundSpeedValid;
            });
            take(&AircraftTrack::heading, [&] {
                out.trackDeg = candidate.trackDeg; out.trackValid = candidate.trackValid;
            });
            take(&AircraftTrack::verticalRate, [&] {
                out.verticalRateFpm = candidate.verticalRateFpm;
                out.verticalRateValid = candidate.verticalRateValid;
            });
            take(&AircraftTrack::squawkObservation, [&] { out.squawk = candidate.squawk; });
        }
    }
    for (auto& [id, out] : result) {
        for (auto* field : {&out.presence, &out.identity, &out.position, &out.altitude,
                            &out.groundSpeed, &out.heading, &out.verticalRate, &out.squawkObservation})
            if (field->source != AircraftSource::None) field->ageSec = std::max(0.0, now - field->monotonicTime);
        out.positionValid = out.positionValid && fresh(out.position, now);
        out.altitudeValid = out.altitudeValid && fresh(out.altitude, now);
        out.groundSpeedValid = out.groundSpeedValid && fresh(out.groundSpeed, now);
        out.trackValid = out.trackValid && fresh(out.heading, now);
        out.verticalRateValid = out.verticalRateValid && fresh(out.verticalRate, now);
        const double missing = std::numeric_limits<double>::quiet_NaN();
        if (!out.positionValid) out.latDeg = out.lonDeg = missing;
        if (!out.altitudeValid) { out.altFt = missing; out.altitudeReference.clear(); }
        if (!out.groundSpeedValid) out.gsKt = missing;
        if (!out.trackValid) out.trackDeg = missing;
        if (!out.verticalRateValid) out.verticalRateFpm = missing;
        if (!fresh(out.identity, now)) out.callsign.clear();
        if (!fresh(out.squawkObservation, now)) out.squawk.clear();
        // Existing map colors label the POSITION source, never unrelated local
        // identity traffic. Detailed provenance is available on every field.
        const auto source = out.positionValid ? out.position.source : out.presence.source;
        out.fromLocal = source == AircraftSource::LocalAdsb;
        out.fromNetwork = source == AircraftSource::OpenSky;
        out.fromAdsc = source == AircraftSource::Adsc;
    }
    return result;
}

AircraftMapSnapshot AdsBTrackStore::snapshot() const {
    AircraftMapSnapshot s;
    std::lock_guard<std::mutex> lk(mutex_);
    const double now = clock_().monotonicTime;
    s.centerLat = centerLat_;
    s.centerLon = centerLon_;
    s.radiusNm = radiusNm_;
    s.localFrames = localFrames_;
    s.localCrcOk = localCrcOk_;
    s.lastStatus = lastStatus_;
    s.networkOnline = networkOnline_ && hasNetworkReceipt_ && now - networkMonotonic_ <= kRetentionSec;
    s.networkEnabled = networkEnabled_;
    if (hasNetworkReceipt_) s.networkAgeSec = int64_t(std::max(0.0, now - networkMonotonic_));
    for (const auto& kv : visibleTracksLocked(now)) {
        if (kv.second.positionValid) s.tracks.push_back(kv.second);
    }
    if (s.tracks.size() > 400) s.tracks.resize(400);
    return s;
}

AircraftTrack AdsBTrackStore::trackByIcao(uint32_t icao) const {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto visible = visibleTracksLocked(clock_().monotonicTime);
    auto it = visible.find(icao);
    if (it == visible.end()) return {};
    return it->second;
}

nlohmann::json AdsBTrackStore::trackJson(const AircraftTrack& t) {
    return {{"icao", t.icaoHex},
                       {"callsign", t.callsign},
                       {"lat", t.positionValid ? nlohmann::json(t.latDeg) : nlohmann::json(nullptr)},
                       {"lon", t.positionValid ? nlohmann::json(t.lonDeg) : nlohmann::json(nullptr)},
                       {"positionValid", t.positionValid},
                       {"altFt", t.altitudeValid ? nlohmann::json(t.altFt) : nlohmann::json(nullptr)},
                       {"altitudeValid", t.altitudeValid},
                       {"altitudeReference", t.altitudeReference},
                       {"gsKt", t.groundSpeedValid ? nlohmann::json(t.gsKt) : nlohmann::json(nullptr)},
                       {"groundSpeedValid", t.groundSpeedValid},
                       {"trackDeg", t.trackValid ? nlohmann::json(t.trackDeg) : nlohmann::json(nullptr)},
                       {"trackValid", t.trackValid},
                       {"vrateFpm", t.verticalRateValid ? nlohmann::json(t.verticalRateFpm) : nlohmann::json(nullptr)},
                       {"verticalRateValid", t.verticalRateValid},
                       {"squawk", t.squawk},
                       {"type", t.typeCode},
                       {"route", t.route},
                       {"photoUrl", t.photoUrl},
                       {"fromLocal", t.fromLocal},
                       {"fromNetwork", t.fromNetwork},
                       {"fromAdsc", t.fromAdsc},
                       {"lastSeenUnix", t.lastSeenUnix},
                       {"fields", {{"presence", fieldJson(t.presence)}, {"identity", fieldJson(t.identity)},
                                   {"position", fieldJson(t.position)}, {"altitude", fieldJson(t.altitude)},
                                   {"groundSpeed", fieldJson(t.groundSpeed)}, {"heading", fieldJson(t.heading)},
                                   {"verticalRate", fieldJson(t.verticalRate)}, {"squawk", fieldJson(t.squawkObservation)}}}};
}

nlohmann::json AdsBTrackStore::statusJson() const {
    const auto s = snapshot();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : s.tracks) arr.push_back(trackJson(t));
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
