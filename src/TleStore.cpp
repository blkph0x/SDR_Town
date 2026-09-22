#include "TleStore.h"
#include "SatObserverConfig.h"
#include "HttpGet.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace {

const char* kUrls[] = {
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle",
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=weather&FORMAT=tle",
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle",
};

std::string cachePath() { return satcomDataDir() + "/tle_cache.txt"; }
std::string metaPath() { return satcomDataDir() + "/tle_meta.txt"; }

std::mutex gCacheIoMutex;

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void stripCarriageReturn(std::string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

bool isLine1(const std::string& line) {
    return line.size() >= 7 && line[0] == '1' && line[1] == ' ';
}

bool isLine2(const std::string& line) {
    return line.size() >= 7 && line[0] == '2' && line[1] == ' ';
}

std::string joinErrors(const std::vector<std::string>& errors) {
    std::string out;
    for (const auto& item : errors) {
        if (item.empty()) continue;
        if (!out.empty()) out += "; ";
        out += item;
    }
    return out;
}

bool writeAtomicTextFile(const std::string& target, const std::string& data,
                         std::string* error) {
    namespace fs = std::filesystem;
    const fs::path targetPath(target);
    const fs::path tempPath(target + ".tmp");
    const fs::path backupPath(target + ".bak");

    std::error_code ec;
    if (!targetPath.parent_path().empty()) fs::create_directories(targetPath.parent_path(), ec);
    ec.clear();
    fs::remove(tempPath, ec);

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot create temporary cache file " + tempPath.string();
            return false;
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            if (error) *error = "cannot write temporary cache file " + tempPath.string();
            out.close();
            fs::remove(tempPath, ec);
            return false;
        }
    }

    fs::remove(backupPath, ec);
    ec.clear();
    const bool hadTarget = fs::exists(targetPath, ec) && !ec;
    if (hadTarget) {
        fs::rename(targetPath, backupPath, ec);
        if (ec) {
            if (error) *error = "cannot rotate existing cache file " + targetPath.string() +
                                ": " + ec.message();
            fs::remove(tempPath, ec);
            return false;
        }
    }

    ec.clear();
    fs::rename(tempPath, targetPath, ec);
    if (ec) {
        const std::string detail = ec.message();
        std::error_code restoreError;
        if (hadTarget) fs::rename(backupPath, targetPath, restoreError);
        fs::remove(tempPath, restoreError);
        if (error) *error = "cannot install cache file " + targetPath.string() +
                            ": " + detail;
        return false;
    }

    fs::remove(backupPath, ec);
    return true;
}

int64_t cacheTimestamp() {
    int64_t refreshed = 0;
    {
        std::ifstream meta(metaPath());
        if (meta) meta >> refreshed;
    }
    if (refreshed > 0) return refreshed;

    namespace fs = std::filesystem;
    std::error_code ec;
    const auto fileTime = fs::last_write_time(cachePath(), ec);
    if (ec) return 0;
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
}

struct FetchResult {
    std::string url;
    std::string body;
    std::string error;
    bool ok = false;
};

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

int TleStore::parseTleText(const std::string& text, TleMap* output) {
    if (!output) return 0;

    std::istringstream input(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        stripCarriageReturn(line);
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(std::move(line));
    }

    TleMap parsed;
    size_t i = 0;
    while (i < lines.size()) {
        std::string name;
        std::string line1;
        std::string line2;

        if (isLine1(lines[i])) {
            if (i + 1 >= lines.size() || !isLine2(lines[i + 1])) {
                ++i;
                continue;
            }
            line1 = lines[i];
            line2 = lines[i + 1];
            i += 2;
        } else {
            if (i + 2 >= lines.size() || !isLine1(lines[i + 1]) || !isLine2(lines[i + 2])) {
                ++i;
                continue;
            }
            name = lines[i];
            line1 = lines[i + 1];
            line2 = lines[i + 2];
            i += 3;
        }

        const int id = noradFromLine1(line1);
        int line2Id = 0;
        try {
            line2Id = std::stoi(line2.substr(2, 5));
        } catch (...) {
            line2Id = 0;
        }
        if (id <= 0 || line2Id != id) continue;
        if (name.empty()) name = "NORAD " + std::to_string(id);

        TleSet tle;
        tle.noradId = id;
        tle.name = std::move(name);
        tle.line1 = std::move(line1);
        tle.line2 = std::move(line2);
        parsed[id] = std::move(tle);
    }

    if (parsed.empty()) return 0;
    *output = std::move(parsed);
    return static_cast<int>(output->size());
}

bool TleStore::writeCacheSnapshot(const TleMap& sets, int64_t refreshedUnix,
                                  std::string* error) {
    std::ostringstream cache;
    for (const auto& [id, tle] : sets) {
        (void)id;
        cache << tle.name << '\n' << tle.line1 << '\n' << tle.line2 << '\n';
    }

    std::lock_guard<std::mutex> ioLock(gCacheIoMutex);
    std::string cacheError;
    if (!writeAtomicTextFile(cachePath(), cache.str(), &cacheError)) {
        if (error) *error = cacheError;
        return false;
    }
    if (!writeAtomicTextFile(metaPath(), std::to_string(refreshedUnix), &cacheError)) {
        if (error) *error = cacheError;
        return false;
    }
    return true;
}

bool TleStore::loadCache() {
    try {
        std::ifstream in(cachePath(), std::ios::binary);
        if (!in) {
            std::lock_guard<std::mutex> lk(mutex_);
            lastError_ = "TLE cache not found: " + cachePath();
            return false;
        }

        std::ostringstream text;
        text << in.rdbuf();
        TleMap parsed;
        const int count = parseTleText(text.str(), &parsed);
        if (count <= 0) {
            std::lock_guard<std::mutex> lk(mutex_);
            lastError_ = "No valid TLEs in cache; existing in-memory TLEs were kept";
            return false;
        }

        const int64_t refreshed = cacheTimestamp();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            byNorad_ = std::move(parsed);
            refreshedUnix_ = refreshed;
            lastError_ = "Loaded " + std::to_string(byNorad_.size()) + " cached TLE sets";
        }
        return true;
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastError_ = std::string("TLE cache load failed; existing data kept: ") + ex.what();
        return false;
    } catch (...) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastError_ = "TLE cache load failed; existing data kept";
        return false;
    }
}

void TleStore::saveCache() const {
    TleMap snapshot;
    int64_t refreshed = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snapshot = byNorad_;
        refreshed = refreshedUnix_;
    }
    if (snapshot.empty()) return;
    std::string ignored;
    writeCacheSnapshot(snapshot, refreshed > 0 ? refreshed : unixNow(), &ignored);
}

bool TleStore::loadFromFile(const std::string& path, std::string* error) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            const std::string message = "cannot open " + path;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                lastError_ = message + "; existing TLEs kept";
            }
            if (error) *error = message;
            return false;
        }

        std::ostringstream text;
        text << in.rdbuf();
        TleMap parsed;
        const int count = parseTleText(text.str(), &parsed);
        if (count <= 0) {
            const std::string message = "no valid TLEs parsed from " + path;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                lastError_ = message + "; existing TLEs kept";
            }
            if (error) *error = message;
            return false;
        }

        const int64_t refreshed = unixNow();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            byNorad_ = parsed;
            refreshedUnix_ = refreshed;
            lastError_ = "Loaded " + std::to_string(byNorad_.size()) + " TLE sets from " + path;
        }

        std::string cacheError;
        if (!writeCacheSnapshot(parsed, refreshed, &cacheError)) {
            std::lock_guard<std::mutex> lk(mutex_);
            lastError_ += "; cache write failed: " + cacheError;
        }
        if (error) error->clear();
        return true;
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastError_ = "TLE file load failed; existing TLEs kept: " + message;
        }
        if (error) *error = message;
        return false;
    }
}

void TleStore::refreshFromNetworkAsync(std::function<void(bool, std::string)> done) {
    std::thread([done = std::move(done)]() mutable {
        bool ok = false;
        std::string error;
        try {
            ok = TleStore::instance().refreshFromNetwork(&error);
        } catch (const std::exception& ex) {
            error = ex.what();
        } catch (...) {
            error = "unexpected TLE refresh failure";
        }
        if (done) {
            try {
                done(ok, error);
            } catch (...) {
            }
        }
    }).detach();
}

bool TleStore::refreshFromNetwork(std::string* error) {
    std::vector<std::future<FetchResult>> futures;
    futures.reserve(std::size(kUrls));
    for (const char* rawUrl : kUrls) {
        const std::string url(rawUrl);
        futures.push_back(std::async(std::launch::async, [url]() {
            FetchResult result;
            result.url = url;
            result.ok = httpGetUrl(url, &result.body, &result.error, 8000);
            return result;
        }));
    }

    TleMap downloaded;
    std::vector<std::string> errors;
    size_t validFeeds = 0;
    for (auto& future : futures) {
        FetchResult result;
        try {
            result = future.get();
        } catch (const std::exception& ex) {
            errors.push_back(ex.what());
            continue;
        } catch (...) {
            errors.push_back("unknown CelesTrak request failure");
            continue;
        }

        if (!result.ok) {
            errors.push_back(result.error.empty() ? ("request failed: " + result.url) : result.error);
            continue;
        }

        TleMap parsedFeed;
        if (parseTleText(result.body, &parsedFeed) <= 0) {
            errors.push_back("invalid TLE response from " + result.url);
            continue;
        }
        ++validFeeds;
        for (auto& [id, tle] : parsedFeed) downloaded[id] = std::move(tle);
    }

    if (downloaded.empty()) {
        const std::string detail = joinErrors(errors);
        std::lock_guard<std::mutex> lk(mutex_);
        if (!byNorad_.empty()) {
            lastError_ = "TLE refresh failed; keeping " + std::to_string(byNorad_.size()) +
                         " cached sets" + (detail.empty() ? std::string{} : ": " + detail);
        } else {
            lastError_ = detail.empty() ? "TLE download failed" : detail;
        }
        if (error) *error = lastError_;
        return false;
    }

    TleMap merged;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        merged = byNorad_;
    }
    for (auto& [id, tle] : downloaded) merged[id] = std::move(tle);

    const int64_t refreshed = unixNow();
    std::string status = "Loaded " + std::to_string(merged.size()) +
                         " TLE sets from " + std::to_string(validFeeds) +
                         " CelesTrak feed" + (validFeeds == 1 ? "" : "s");
    if (!errors.empty()) status += "; some feeds failed: " + joinErrors(errors);

    {
        std::lock_guard<std::mutex> lk(mutex_);
        byNorad_ = merged;
        refreshedUnix_ = refreshed;
        lastError_ = status;
    }

    std::string cacheError;
    if (!writeCacheSnapshot(merged, refreshed, &cacheError)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastError_ += "; cache write failed: " + cacheError;
    }
    if (error) error->clear();
    return true;
}

bool TleStore::hasTle(int noradId) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return byNorad_.count(noradId) > 0;
}

TleSet TleStore::get(int noradId) const {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = byNorad_.find(noradId);
    return it == byNorad_.end() ? TleSet{} : it->second;
}

std::vector<TleSet> TleStore::all() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<TleSet> out;
    out.reserve(byNorad_.size());
    for (const auto& [id, tle] : byNorad_) {
        (void)id;
        out.push_back(tle);
    }
    return out;
}

size_t TleStore::size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return byNorad_.size();
}

int64_t TleStore::ageSec() const {
    std::lock_guard<std::mutex> lk(mutex_);
    if (refreshedUnix_ <= 0) return -1;
    return unixNow() - refreshedUnix_;
}

std::string TleStore::lastError() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return lastError_;
}

void TleStore::upsert(const TleSet& tle) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (tle.noradId > 0) byNorad_[tle.noradId] = tle;
}
