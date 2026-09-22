#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct TleSet {
    int noradId = 0;
    std::string name;
    std::string line1;
    std::string line2;
};

class TleStore {
public:
    static TleStore& instance();

    // Load cache from disk. A failed load never clears already-valid in-memory TLEs.
    bool loadCache();
    void saveCache() const;

    // Fetch CelesTrak group files over HTTPS (blocking; call from CLI or a worker).
    // Existing cache data is retained if every network request fails or returns invalid data.
    bool refreshFromNetwork(std::string* error = nullptr);
    void refreshFromNetworkAsync(std::function<void(bool ok, std::string error)> done);

    // Load an explicit TLE file. A malformed file never destroys the active store.
    bool loadFromFile(const std::string& path, std::string* error = nullptr);

    bool hasTle(int noradId) const;
    TleSet get(int noradId) const;
    std::vector<TleSet> all() const;
    size_t size() const;

    // Seconds since last successful refresh (or cache file modification time). -1 if unknown.
    int64_t ageSec() const;
    std::string lastError() const;

    // Inject TLE for tests.
    void upsert(const TleSet& tle);

private:
    TleStore() = default;
    using TleMap = std::map<int, TleSet>;

    static int parseTleText(const std::string& text, TleMap* output);
    static int noradFromLine1(const std::string& line1);
    static bool writeCacheSnapshot(const TleMap& sets, int64_t refreshedUnix,
                                   std::string* error = nullptr);

    mutable std::mutex mutex_;
    TleMap byNorad_;
    int64_t refreshedUnix_ = 0;
    std::string lastError_;
};
