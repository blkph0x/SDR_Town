#include "SatPassPlanner.h"
#include "Sgp4.h"
#include "TleStore.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

double unixNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double jdFromUnix(double unixSec) {
    return 2440587.5 + unixSec / 86400.0;
}

} // namespace

SatPassPlanner& SatPassPlanner::instance() {
    static SatPassPlanner p;
    return p;
}

SatPassPlanner::SatPassPlanner() {
    observer_.load();
    catalogue_.load();
    TleStore::instance().loadCache();
    lastStatus_ = "Pass planner ready";
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
        lastStatus_ = "Observer updated";
    }
    refreshPasses(24.0);
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
    if (TleStore::instance().all().empty())
        TleStore::instance().loadCache();
}

bool SatPassPlanner::refreshTle(std::string* error) {
    ensureTleLoaded();
    std::string err;
    const bool ok = TleStore::instance().refreshFromNetwork(&err);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = ok ? "TLE refreshed from CelesTrak" : ("TLE refresh failed: " + err);
    }
    if (!ok && error) *error = err;
    if (ok) refreshPasses(24.0);
    notify();
    return ok;
}

void SatPassPlanner::refreshTleAsync(std::function<void(bool, std::string)> done) {
    TleStore::instance().refreshFromNetworkAsync([this, done = std::move(done)](bool ok, std::string err) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = ok ? "TLE refreshed from CelesTrak" : ("TLE refresh failed: " + err);
        }
        if (ok) refreshPasses(24.0);
        notify();
        if (done) done(ok, err);
    });
}

void SatPassPlanner::predictLocked(double hoursAhead) {
    passes_.clear();
    const auto obs = observer_;
    const auto selected = catalogue_.selectedEntries();
    const double t0 = unixNow();
    const double t1 = t0 + hoursAhead * 3600.0;
    const double step = 30.0; // seconds

    for (const auto& sat : selected) {
        const TleSet tle = TleStore::instance().get(sat.noradId);
        if (tle.noradId == 0) continue;
        Sgp4::Elements el{};
        if (!Sgp4::parseTle(tle.line1, tle.line2, &el)) continue;

        // Primary downlink for listing (first); arm picks specific.
        if (sat.downlinks.empty()) continue;

        bool inPass = false;
        SatPassInfo cur{};
        double maxEl = -90.0;

        for (double t = t0; t <= t1; t += step) {
            const double jd = jdFromUnix(t);
            const double mins = Sgp4::minutesSinceEpoch(el, jd);
            const auto st = Sgp4::propagate(el, mins);
            if (!st.ok) continue;
            double elDeg = 0, az = 0, range = 0, rr = 0;
            Sgp4::lookAnglesTeme(jd, st.r, st.v, obs.latDeg, obs.lonDeg, obs.altM, &elDeg, &az, &range, &rr);
            const bool above = elDeg >= obs.minElevationDeg;
            if (above && !inPass) {
                inPass = true;
                cur = {};
                cur.satId = sat.id;
                cur.satName = sat.name;
                cur.noradId = sat.noradId;
                cur.downlinkId = sat.downlinks.front().id;
                cur.downlinkLabel = sat.downlinks.front().label;
                cur.role = sat.downlinks.front().role;
                cur.mode = sat.downlinks.front().mode;
                cur.freqHz = sat.downlinks.front().freqHz;
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
    if (!dl) {
        for (const auto& d : sat->downlinks) {
            if (d.armable) {
                dl = &d;
                break;
            }
        }
    }
    if (!dl && !sat->downlinks.empty()) dl = &sat->downlinks.front();
    if (!dl) {
        if (error) *error = "No downlink";
        return false;
    }
    if (!dl->armable) {
        if (error)
            *error = "This downlink is a bookmark only (no decoder yet — e.g. Meteor LRPT later)";
        return false;
    }

    // Prefer next upcoming pass for this sat
    double aos = 0, los = 0;
    for (const auto& p : passes_) {
        if (p.satId == satId && p.losUnix > unixNow()) {
            aos = p.aosUnix;
            los = p.losUnix;
            break;
        }
    }
    if (los <= 0) {
        // Arm anyway for Doppler now (may be below horizon)
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
        // Auto-disarm shortly after LOS
        armed_.armed = false;
        lastStatus_ = "Pass ended — disarmed";
        return false;
    }

    if (!armed_.autoTrack) return false;
    // Only retune when above horizon (or within 2 min of AOS)
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
        {"armed", armed},
        {"tleAgeSec", s.tleAgeSec},
        {"lastStatus", s.lastStatus},
        {"predicting", s.predicting},
    };
}
