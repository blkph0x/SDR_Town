#include "TleStore.h"
#include "SatObserverConfig.h"
#include "HttpGet.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace {

const char* kUrls[] = {
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle",
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=weather&FORMAT=tle",
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle",
};

std::string cachePath() { return satcomDataDir() + "/tle_cache.txt"; }
std::string metaPath() { return satcomDataDir() + "/tle_meta.txt"; }

} // namespace

TleStore& TleStore::instance() {
    static TleStore s;
    return s;
}

int TleStore::noradFromLine1(const std::string& line1) {
    if (line1.size() < 7) return 0;
    try {
        return std::stoi(line1.substr(2, 5));
    } catch (...) {
        return 0;
    }
}

int TleStore::parseTleText(const std::string& text) {
    std::istringstream in(text);
    std::string name, l1, l2;
    int count = 0;
    while (std::getline(in, name)) {
        if (name.empty()) continue;
        if (!std::getline(in, l1) || !std::getline(in, l2)) break;
        if (!l1.empty() && l1.back() == '\r') l1.pop_back();
        if (!l2.empty() && l2.back() == '\r') l2.pop_back();
        if (!name.empty() && name.back() == '\r') name.pop_back();
        // 3-line or 2-line: if name starts with "1 ", it's line1
        if (!name.empty() && name[0] == '1') {
            l2 = l1;
            l1 = name;
            name = "SAT";
        }
        const int id = noradFromLine1(l1);
        if (id <= 0 || l2.empty() || l2[0] != '2') continue;
        TleSet t;
        t.noradId = id;
        t.name = name;
        t.line1 = l1;
        t.line2 = l2;
        byNorad_[id] = t;
        ++count;
    }
    return count;
}

bool TleStore::loadCache() {
    std::lock_guard<std::mutex> lk(mutex_);
    byNorad_.clear();
    try {
        std::ifstream in(cachePath());
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        const int n = parseTleText(ss.str());
        std::ifstream meta(metaPath());
        if (meta) meta >> refreshedUnix_;
        return n > 0;
    } catch (...) {
        return false;
    }
}

void TleStore::saveCache() const {
    std::lock_guard<std::mutex> lk(mutex_);
    try {
        std::ofstream out(cachePath());
        for (const auto& kv : byNorad_) {
            out << kv.second.name << "\n" << kv.second.line1 << "\n" << kv.second.line2 << "\n";
        }
        std::ofstream meta(metaPath());
        meta << refreshedUnix_;
    } catch (...) {
    }
}

bool TleStore::loadFromFile(const std::string& path, std::string* error) {
    try {
        std::ifstream in(path);
        if (!in) {
            if (error) *error = "cannot open " + path;
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::lock_guard<std::mutex> lk(mutex_);
        byNorad_.clear();
        const int n = parseTleText(ss.str());
        if (n <= 0) {
            lastError_ = "no TLEs parsed from " + path;
            if (error) *error = lastError_;
            return false;
        }
        refreshedUnix_ = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        lastError_.clear();
        try {
            std::ofstream out(cachePath());
            for (const auto& kv : byNorad_) {
                out << kv.second.name << "\n" << kv.second.line1 << "\n" << kv.second.line2 << "\n";
            }
            std::ofstream meta(metaPath());
            meta << refreshedUnix_;
        } catch (...) {
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

void TleStore::refreshFromNetworkAsync(std::function<void(bool, std::string)> done) {
    std::thread([done = std::move(done)]() {
        std::string err;
        const bool ok = TleStore::instance().refreshFromNetwork(&err);
        if (done) done(ok, err);
    }).detach();
}

bool TleStore::refreshFromNetwork(std::string* error) {
    std::string combined;
    std::string lastErr;
    for (const char* url : kUrls) {
        std::string body, err;
        if (!httpGetUrl(url, &body, &err, 20000)) {
            lastErr = err;
            continue;
        }
        combined += body;
        combined.push_back('\n');
    }

    std::lock_guard<std::mutex> lk(mutex_);
    if (combined.empty()) {
        lastError_ = lastErr.empty() ? "TLE download failed" : lastErr;
        if (error) *error = lastError_;
        return false;
    }
    byNorad_.clear();
    const int n = parseTleText(combined);
    if (n <= 0) {
        lastError_ = "No TLEs parsed from CelesTrak";
        if (error) *error = lastError_;
        return false;
    }
    refreshedUnix_ = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    lastError_.clear();
    // unlock before save to avoid deadlock — saveCache locks again
    // so save inline:
    try {
        std::ofstream out(cachePath());
        for (const auto& kv : byNorad_) {
            out << kv.second.name << "\n" << kv.second.line1 << "\n" << kv.second.line2 << "\n";
        }
        std::ofstream meta(metaPath());
        meta << refreshedUnix_;
    } catch (...) {
    }
    return true;
}

bool TleStore::hasTle(int noradId) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return byNorad_.count(noradId) > 0;
}

TleSet TleStore::get(int noradId) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = byNorad_.find(noradId);
    if (it == byNorad_.end()) return {};
    return it->second;
}

std::vector<TleSet> TleStore::all() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<TleSet> out;
    out.reserve(byNorad_.size());
    for (const auto& kv : byNorad_) out.push_back(kv.second);
    return out;
}

int64_t TleStore::ageSec() const {
    std::lock_guard<std::mutex> lk(mutex_);
    if (refreshedUnix_ <= 0) return -1;
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    return now - refreshedUnix_;
}

std::string TleStore::lastError() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return lastError_;
}

void TleStore::upsert(const TleSet& tle) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (tle.noradId > 0) byNorad_[tle.noradId] = tle;
}
