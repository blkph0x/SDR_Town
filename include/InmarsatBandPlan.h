#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct InmarsatChannel {
    std::string label;
    double freqHz = 0.0;
    std::string mode; // aero_msk | aero_oqpsk | aero_voice | egc
    int baud = 0;
};

struct InmarsatBandPlan {
    std::string id;
    std::string name;
    std::vector<std::string> aliases;
    double orbitalSlotDegE = 0.0;
    std::string region;
    std::string source;
    std::vector<InmarsatChannel> channels;

    nlohmann::json toJson() const;
};

class InmarsatBandPlanStore {
public:
    static InmarsatBandPlanStore& instance();

    // Load from data/inmarsat next to the executable, install prefix, or source tree.
    void reload(std::string* error = nullptr);
    const std::vector<InmarsatBandPlan>& plans() const { return plans_; }
    const InmarsatBandPlan* findById(const std::string& id) const;
    nlohmann::json catalogueJson() const;

private:
    InmarsatBandPlanStore();
    std::vector<InmarsatBandPlan> plans_;
};
