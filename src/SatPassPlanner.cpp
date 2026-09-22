#include "SatPassPlanner.h"
#include "Sgp4.h"
#include "TleStore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kWgs84Akm = 6378.137;
constexpr double kWgs84E2 = 6.69437999014e-3;

double unixNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double jdFromUnix(double unixSec) {
    return 2440587.5 + unixSec / 86400.0;
}

double normalizeLongitude(double lonDeg) {
    while (lonDeg > 180.0) lonDeg -= 360.0;
    while (lonDeg < -180.0) lonDeg += 360.0;
    return lonDeg;
}

void ecefToGeodetic(const double rEcef[3], double* latDeg, double* lonDeg, double* altKm) {
    const double x = rEcef[0];
    const double y = rEcef[1];
    const double z = rEcef[2];
    const double p = std::hypot(x, y);
    double lat = std::atan2(z, std::max(1.0e-9, p) * (1.0 - kWgs84E2));
    double alt = 0.0;
    for (int i = 0; i < 8; ++i) {
        const double sinLat = std::sin(lat);
        const double n = kWgs84Akm / std::sqrt(1.0 - kWgs84E2 * sinLat * sinLat);
        const double cosLat = std::cos(lat);
        alt = std::abs(cosLat) > 1.0e-9 ? p / cosLat - n : std::abs(z) - n * (1.0 - kWgs84E2);
        const double denom = std::max(1.0e-9, n + alt);
        lat = std::atan2(z, std::max(1.0e-9, p) * (1.0 - kWgs84E2 * n / denom));
    }
    if (latDeg) *latDeg = std::clamp(lat * kRadToDeg, -90.0, 90.0);
    if (lonDeg) *lonDeg = normalizeLongitude(std::atan2(y, x) * kRadToDeg);
    if (altKm) *altKm = alt;
}

int downlinkPriority(const SatDownlink& d) {
    if (!d.armable) return 100;
    if (d.role == "sstv") return 0;
    if (d.role == "apt") return 1;
    if (d.role == "aprs") return 2;
    if (d.role == "data") return 3;
    if (d.role == "voice") return 4;
    return 5;
}

const SatDownlink* preferredDownlink(const SatCatalogueEntry& sat) {
    const SatDownlink* best = nullptr;
    int bestPriority = std::numeric_limits<int>::max();
    for (const auto& d : sat.downlinks) {
        const int priority = downlinkPriority(d);
        if (priority < bestPriority) {
            best = &d;
            bestPriority = priority;
        }
    }
    if (!best && !sat.downlinks.empty()) best = &sat.downlinks.front();
    return best;
}

} // namespace

SatPassPlanner& SatPassPlanner::instance() {
    static SatPassPlanner p;
    return p;
}

SatPassPlanner::SatPassPlanner() {
    observer_.load();
    catalogue_.load();
    const bool loaded = TleStore::instance().loadCache();
    const size_t count = TleStore::instance().size();
    if (count > 0) predictLocked(24.0);
    if (loaded || count > 0) {
        lastStatus_ = "Loaded " + std::to_string(count) + " cached TLE sets (" +
                      std::to_string(passes_.size()) + " passes)";
    } else {
        lastStatus_ = "Pass planner ready — no valid TLE cache";
    }
}

SatPassPlanner::~SatPassPlanner() = default;

void SatPassPlanner::notify() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = updateCb_;
    }
    if (cb) cb();
}

void SatPassPlanner::setUpdateCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    updateCb_ = std::move(cb);
}

void SatPassPlanner::setObserver(const SatObserverConfig& obs) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        observer_ = obs;
        observer_.save();
        lastStatus_ = "Saving observer location";
    }
    refreshPasses(24.0);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = "Location saved: " + std::to_string(observer_.latDeg) + ", " +
                      std::to_string(observer_.lonDeg) + " (" +
                      std::to_string(passes_.size()) + " passes)";
    }
    notify();
}

SatObserverConfig SatPassPlanner::observer() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return observer_;
}

SatCatalogue SatPassPlanner::catalogue() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return catalogue_;
}

void SatPassPlanner::setCatalogueSelection(const std::vector<std::string>& selectedIds) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& e : catalogue_.entries()) e.selected = false;
        for (const auto& id : selectedIds) catalogue_.setSelected(id, true);
        catalogue_.save();
        lastStatus_ = "Catalogue selection saved";
    }
    refreshPasses(24.0);
}

void SatPassPlanner::ensureTleLoaded() {
    if (TleStore::instance().size() == 0) TleStore::instance().loadCache();
}

bool SatPassPlanner::refreshTle(std::string* error) {
    ensureTleLoaded();
    if (TleStore::instance().size() > 0) refreshPasses(24.0);

    std::string networkError;
    const bool networkOk = TleStore::instance().refreshFromNetwork(&networkError);
    const size_t count = TleStore::instance().size();
    const bool usable = networkOk || count > 0;
    if (usable) refreshPasses(24.0);

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (networkOk) {
            lastStatus_ = "TLE refreshed: " + std::to_string(count) + " sets";
        } else if (count > 0) {
            lastStatus_ = "TLE network refresh failed; using " + std::to_string(count) +
                          " cached sets" +
                          (networkError.empty() ? std::string{} : ": " + networkError);
        } else {
            lastStatus_ = "TLE refresh failed" +
                          (networkError.empty() ? std::string{} : ": " + networkError);
        }
    }

    if (error) {
        if (usable) error->clear();
        else *error = networkError;
    }
    notify();
    return usable;
}

void SatPassPlanner::refreshTleAsync(std::function<void(bool, std::string)> done) {
    ensureTleLoaded();
    const size_t cachedCount = TleStore::instance().size();
    if (cachedCount > 0) {
        refreshPasses(24.0);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = "Using " + std::to_string(cachedCount) +
                          " cached TLE sets while checking CelesTrak";
        }
        notify();
    }

    TleStore::instance().refreshFromNetworkAsync(
        [this, done = std::move(done)](bool networkOk, std::string networkError) mutable {
            const size_t count = TleStore::instance().size();
            const bool usable = networkOk || count > 0;
            if (usable) refreshPasses(24.0);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (networkOk) {
                    lastStatus_ = "TLE refreshed: " + std::to_string(count) + " sets";
                } else if (count > 0) {
                    lastStatus_ = "TLE network refresh failed; using " +
                                  std::to_string(count) + " cached sets" +
                                  (networkError.empty() ? std::string{} : ": " + networkError);
                } else {
                    lastStatus_ = "TLE refresh failed" +
                                  (networkError.empty() ? std::string{} : ": " + networkError);
                }
            }
            notify();
            if (done) done(usable, usable ? std::string{} : networkError);
        });
}

std::vector<SatCurrentPosition> SatPassPlanner::currentPositionsLocked(double unixSec) const {
    std::vector<SatCurrentPosition> out;
    const double jd = jdFromUnix(unixSec);
    const auto selected = catalogue_.selectedEntries();
    out.reserve(selected.size());

    for (const auto& sat : selected) {
        SatCurrentPosition pos;
        pos.satId = sat.id;
        pos.satName = sat.name;
        pos.noradId = sat.noradId;
        if (const SatDownlink* dl = preferredDownlink(sat)) {
            pos.downlinkId = dl->id;
            pos.downlinkLabel = dl->label;
            pos.role = dl->role;
            pos.mode = dl->mode;
            pos.freqHz = dl->freqHz;
            pos.armable = dl->armable;
        }

        const TleSet tle = TleStore::instance().get(sat.noradId);
        Sgp4::Elements el{};
        if (tle.noradId <= 0 || !Sgp4::parseTle(tle.line1, tle.line2, &el)) {
            out.push_back(std::move(pos));
            continue;
        }

        const auto state = Sgp4::propagate(el, Sgp4::minutesSinceEpoch(el, jd));
        if (!state.ok) {
            out.push_back(std::move(pos));
            continue;
        }

        double rEcef[3]{}, vEcef[3]{};
        Sgp4::temeToEcef(jd, state.r, state.v, rEcef, vEcef);
        ecefToGeodetic(rEcef, &pos.latitudeDeg, &pos.longitudeDeg, &pos.altitudeKm);
        Sgp4::lookAngles(rEcef, vEcef,
                         observer_.latDeg, observer_.lonDeg, observer_.altM,
                         &pos.elevationDeg, &pos.azimuthDeg, &pos.rangeKm, &pos.rangeRateKmS);
        pos.tleValid = std::isfinite(pos.latitudeDeg) && std::isfinite(pos.longitudeDeg) &&
                       std::isfinite(pos.altitudeKm) && std::isfinite(pos.elevationDeg);
        pos.inRange = pos.tleValid && pos.elevationDeg >= observer_.minElevationDeg;
        out.push_back(std::move(pos));
    }
    return out;
}

void SatPassPlanner::predictLocked(double hoursAhead) {
    passes_.clear();
    const auto obs = observer_;
    const auto selected = catalogue_.selectedEntries();
    const double t0 = unixNow();
    const double t1 = t0 + hoursAhead * 3600.0;
    const double step = 30.0;

    for (const auto& sat : selected) {
        const TleSet tle = TleStore::instance().get(sat.noradId);
        if (tle.noradId == 0) continue;
        Sgp4::Elements el{};
        if (!Sgp4::parseTle(tle.line1, tle.line2, &el)) continue;

        const SatDownlink* dl = preferredDownlink(sat);
        if (!dl || !dl->armable) continue;

        bool inPass = false;
        SatPassInfo cur{};
        double maxEl = -90.0;

        for (double t = t0; t <= t1; t += step) {
            const double jd = jdFromUnix(t);
            const double mins = Sgp4::minutesSinceEpoch(el, jd);
            const auto st = Sgp4::propagate(el, mins);
            if (!st.ok) continue;
            double elDeg = 0, az = 0, range = 0, rr = 0;
            Sgp4::lookAnglesTeme(jd, st.r, st.v, obs.latDeg, obs.lonDeg, obs.altM,
                                 &elDeg, &az, &range, &rr);
            const bool above = elDeg >= obs.minElevationDeg;
            if (above && !inPass) {
                inPass = true;
                cur = {};
                cur.satId = sat.id;
                cur.satName = sat.name;
                cur.noradId = sat.noradId;
                cur.downlinkId = dl->id;
                cur.downlinkLabel = dl->label;
                cur.role = dl->role;
                cur.mode = dl->mode;
                cur.freqHz = dl->freqHz;
                cur.aosUnix = t;
                maxEl = elDeg;
            } else if (above && inPass) {
                maxEl = std::max(maxEl, elDeg);
            } else if (!above && inPass) {
                cur.losUnix = t;
                cur.maxElDeg = maxEl;
                cur.durationSec = cur.losUnix - cur.aosUnix;
                if (cur.durationSec >= 60.0) passes_.push_back(cur);
                inPass = false;
            }
        }
        if (inPass) {
            cur.losUnix = t1;
            cur.maxElDeg = maxEl;
            cur.durationSec = cur.losUnix - cur.aosUnix;
            if (cur.durationSec >= 60.0) passes_.push_back(cur);
        }
    }

    std::sort(passes_.begin(), passes_.end(),
              [](const SatPassInfo& a, const SatPassInfo& b) { return a.aosUnix < b.aosUnix; });
    if (passes_.size() > 80) passes_.resize(80);
}

void SatPassPlanner::refreshPasses(double hoursAhead) {
    predicting_ = true;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        ensureTleLoaded();
        predictLocked(hoursAhead);
        lastStatus_ = "Passes updated (" + std::to_string(passes_.size()) + ")";
    }
    predicting_ = false;
    notify();
}

bool SatPassPlanner::arm(const std::string& satId, const std::string& downlinkId, bool autoTrack,
                         std::string* error) {
    std::lock_guard<std::mutex> lk(mutex_);
    const SatCatalogueEntry* sat = nullptr;
    for (const auto& e : catalogue_.entries()) {
        if (e.id == satId) { sat = &e; break; }
    }
    if (!sat) {
        if (error) *error = "Unknown satellite";
        return false;
    }
    const SatDownlink* dl = nullptr;
    for (const auto& d : sat->downlinks) {
        if (!downlinkId.empty() && d.id == downlinkId) {
            dl = &d;
            break;
        }
    }
    if (!dl) dl = preferredDownlink(*sat);
    if (!dl) {
        if (error) *error = "No downlink";
        return false;
    }
    if (!dl->armable) {
        if (error)
            *error = "This downlink is a bookmark only (no decoder yet — e.g. Meteor LRPT later)";
        return false;
    }

    double aos = 0, los = 0;
    for (const auto& p : passes_) {
        if (p.satId == satId && p.losUnix > unixNow()) {
            aos = p.aosUnix;
            los = p.losUnix;
            break;
        }
    }
    if (los <= 0) {
        aos = unixNow();
        los = unixNow() + 900.0;
    }

    armed_ = {};
    armed_.armed = true;
    armed_.autoTrack = autoTrack;
    armed_.satId = satId;
    armed_.downlinkId = dl->id;
    armed_.role = dl->role;
    armed_.mode = dl->mode;
    armed_.freqHz = dl->freqHz;
    armed_.aosUnix = aos;
    armed_.losUnix = los;
    armed_.tunedHz = dl->freqHz;
    lastStatus_ = "Armed " + sat->name + " " + dl->label;
    return true;
}

void SatPassPlanner::disarm() {
    std::lock_guard<std::mutex> lk(mutex_);
    armed_ = {};
    lastStatus_ = "Disarmed";
}

void SatPassPlanner::setAutoTrack(bool on) {
    std::lock_guard<std::mutex> lk(mutex_);
    armed_.autoTrack = on;
}

bool SatPassPlanner::tickAutoTrack(double* outTunedHz) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!armed_.armed) return false;

    const double now = unixNow();
    int norad = 0;
    for (const auto& e : catalogue_.entries()) {
        if (e.id == armed_.satId) { norad = e.noradId; break; }
    }
    const TleSet tle = TleStore::instance().get(norad);
    Sgp4::Elements el{};
    if (tle.noradId == 0 || !Sgp4::parseTle(tle.line1, tle.line2, &el)) {
        armed_.dopplerHz = 0;
        armed_.tunedHz = armed_.freqHz;
        if (outTunedHz) *outTunedHz = armed_.tunedHz;
        return armed_.autoTrack;
    }

    const double jd = jdFromUnix(now);
    const auto st = Sgp4::propagate(el, Sgp4::minutesSinceEpoch(el, jd));
    double elDeg = 0, az = 0, range = 0, rr = 0;
    if (st.ok) {
        Sgp4::lookAnglesTeme(jd, st.r, st.v, observer_.latDeg, observer_.lonDeg, observer_.altM,
                             &elDeg, &az, &range, &rr);
    }
    armed_.elevationDeg = elDeg;
    const double doppler = Sgp4::dopplerShiftHz(armed_.freqHz, rr);
    armed_.dopplerHz = doppler;
    armed_.tunedHz = armed_.freqHz + doppler;

    if (now > armed_.losUnix + 60.0) {
        armed_.armed = false;
        lastStatus_ = "Pass ended — disarmed";
        return false;
    }

    if (!armed_.autoTrack) return false;
    if (elDeg < -2.0 && now + 120.0 < armed_.aosUnix) return false;
    if (outTunedHz) *outTunedHz = armed_.tunedHz;
    return true;
}

SatPassPlannerSnapshot SatPassPlanner::snapshot(double hoursAhead) const {
    SatPassPlannerSnapshot s;
    std::lock_guard<std::mutex> lk(mutex_);
    s.observer = observer_;
    s.catalogue = catalogue_;
    s.passes = passes_;
    s.positions = currentPositionsLocked(unixNow());
    s.armed = armed_;
    s.tleAgeSec = TleStore::instance().ageSec();
    s.lastStatus = lastStatus_;
    s.predicting = predicting_;
    (void)hoursAhead;
    return s;
}

nlohmann::json SatPassPlanner::statusJson() const {
    const auto s = snapshot();
    nlohmann::json passes = nlohmann::json::array();
    for (const auto& p : s.passes) {
        passes.push_back({
            {"satId", p.satId},
            {"satName", p.satName},
            {"noradId", p.noradId},
            {"downlinkId", p.downlinkId},
            {"downlinkLabel", p.downlinkLabel},
            {"role", p.role},
            {"mode", p.mode},
            {"freqHz", p.freqHz},
            {"freqMHz", p.freqHz / 1e6},
            {"aosUnix", p.aosUnix},
            {"losUnix", p.losUnix},
            {"maxElDeg", p.maxElDeg},
            {"durationSec", p.durationSec},
        });
    }
    nlohmann::json positions = nlohmann::json::array();
    for (const auto& p : s.positions) {
        positions.push_back({
            {"satId", p.satId},
            {"satName", p.satName},
            {"noradId", p.noradId},
            {"tleValid", p.tleValid},
            {"latitudeDeg", p.latitudeDeg},
            {"longitudeDeg", p.longitudeDeg},
            {"altitudeKm", p.altitudeKm},
            {"elevationDeg", p.elevationDeg},
            {"azimuthDeg", p.azimuthDeg},
            {"rangeKm", p.rangeKm},
            {"rangeRateKmS", p.rangeRateKmS},
            {"inRange", p.inRange},
            {"armable", p.armable},
            {"downlinkId", p.downlinkId},
            {"downlinkLabel", p.downlinkLabel},
            {"role", p.role},
            {"mode", p.mode},
            {"freqHz", p.freqHz},
        });
    }
    nlohmann::json armed = {
        {"armed", s.armed.armed},
        {"autoTrack", s.armed.autoTrack},
        {"satId", s.armed.satId},
        {"downlinkId", s.armed.downlinkId},
        {"role", s.armed.role},
        {"mode", s.armed.mode},
        {"freqHz", s.armed.freqHz},
        {"freqMHz", s.armed.freqHz / 1e6},
        {"aosUnix", s.armed.aosUnix},
        {"losUnix", s.armed.losUnix},
        {"dopplerHz", s.armed.dopplerHz},
        {"elevationDeg", s.armed.elevationDeg},
        {"tunedHz", s.armed.tunedHz},
        {"tunedMHz", s.armed.tunedHz / 1e6},
    };
    return {
        {"observer", s.observer.toJson()},
        {"catalogue", s.catalogue.toJson()},
        {"passes", passes},
        {"positions", positions},
        {"armed", armed},
        {"tleAgeSec", s.tleAgeSec},
        {"lastStatus", s.lastStatus},
        {"predicting", s.predicting},
    };
}

nlohmann::json SatPassPlanner::publicStatusJson() const {
    auto j = statusJson();
    bool configured = false;
    double minEl = 10.0;
    if (j.contains("observer") && j["observer"].is_object()) {
        configured = std::abs(j["observer"].value("latDeg", 0.0)) > 1e-9 ||
                     std::abs(j["observer"].value("lonDeg", 0.0)) > 1e-9;
        minEl = j["observer"].value("minElevationDeg", 10.0);
    }
    j["observer"] = {{"configured", configured}, {"minElevationDeg", minEl}};
    j.erase("positions");
    return j;
}
