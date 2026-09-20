#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

struct SatDownlink {
    std::string id;     // e.g. "iss-sstv"
    std::string label;  // "ISS SSTV"
    double freqHz = 0.0;
    std::string mode = "NFM"; // NFM / APT / APRS / USB
    std::string role;        // voice|aprs|sstv|apt|data
    bool armable = true;     // false = bookmark only (no decoder yet)
};

struct SatCatalogueEntry {
    std::string id; // stable key e.g. "iss"
    std::string name;
    int noradId = 0;
    std::vector<SatDownlink> downlinks;
    bool selected = false;
    std::string notes; // honesty / schedule hints
};

class SatCatalogue {
public:
    static std::vector<SatCatalogueEntry> builtIn();
    static SatCatalogue loadOrDefault();

    void load();
    void save() const;

    const std::vector<SatCatalogueEntry>& entries() const { return entries_; }
    std::vector<SatCatalogueEntry>& entries() { return entries_; }

    void setSelected(const std::string& id, bool on);
    void setSelectedNorad(const std::vector<int>& noradIds);
    std::vector<SatCatalogueEntry> selectedEntries() const;

    nlohmann::json toJson() const;
    static SatCatalogue fromJson(const nlohmann::json& j);

private:
    std::vector<SatCatalogueEntry> entries_;
};
