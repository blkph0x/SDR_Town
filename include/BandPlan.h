#pragma once
#include "Demod.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct BandPlanEntry {
    std::string name;
    double startHz = 0, endHz = 0; // [start, end); channel entries include receive width
    DemodMode mode = DemodMode::AUTO;
    double bandwidthHz = 0, lpfHz = 0, stepHz = 0;
    std::string decoder; // descriptive hint only, never a trust/activation flag
    std::string source;
    int priority = 0;
};

struct BandPlanProfile {
    std::string id, region, country, location, coverage, revision;
    std::vector<BandPlanEntry> entries;
};

std::optional<BandPlanEntry> lookupBand(const BandPlanProfile& profile, double frequencyHz);
std::vector<BandPlanEntry> visibleBandSections(const BandPlanProfile& profile, double lowHz, double highHz);
BandPlanProfile parseBandPlan(const std::string& jsonText); // throws on invalid import
std::string serializeBandPlan(const BandPlanProfile& profile);
const char* bandPlanModeName(DemodMode mode);

class BandPlanCatalog {
public:
    static BandPlanCatalog& instance();
    std::shared_ptr<const BandPlanProfile> active() const;
    std::vector<std::shared_ptr<const BandPlanProfile>> profiles() const;
    bool select(const std::string& id);
    void importProfile(const std::string& jsonText);
    void persist() const;
private:
    BandPlanCatalog();
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<const BandPlanProfile>> profiles_;
    std::atomic<std::shared_ptr<const BandPlanProfile>> active_;
};

// Value results remain valid across a concurrent GUI profile change.
std::optional<BandPlanEntry> findBandPlanForFrequency(double frequencyHz);
std::vector<BandPlanEntry> builtInBandPlans();
