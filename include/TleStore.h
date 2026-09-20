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

    // Load cache from disk. Returns true if any TLEs present.
    bool loadCache();
    void saveCache() const;

    // Fetch CelesTrak group files over HTTPS (blocking; call from CLI or a worker).
    bool refreshFromNetwork(std::string* error = nullptr);
    void refreshFromNetworkAsync(std::function<void(bool ok, std::string error)> done);

    bool loadFromFile(const std::string& path, std::string* error = nullptr);

    bool hasTle(int noradId) const;
    TleSet get(int noradId) const;
    std::vector<TleSet> all() const;

    // Seconds since last successful refresh (or file mtime). -1 if unknown.
    int64_t ageSec() const;
    std::string lastError() const;

    // Inject TLE for tests.
    void upsert(const TleSet& tle);

private:
    TleStore() = default;
    int parseTleText(const std::string& text);
    static int noradFromLine1(const std::string& line1);

    mutable std::mutex mutex_;
    std::map<int, TleSet> byNorad_;
    int64_t refreshedUnix_ = 0;
    std::string lastError_;
};
