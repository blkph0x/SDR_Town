#include "SatCatalogue.h"
#include "SatObserverConfig.h"

#include <fstream>

namespace {

SatDownlink dl(const char* id, const char* label, double mhz, const char* mode, const char* role,
               bool armable = true) {
    SatDownlink d;
    d.id = id;
    d.label = label;
    d.freqHz = mhz * 1e6;
    d.mode = mode;
    d.role = role;
    d.armable = armable;
    return d;
}

} // namespace

std::vector<SatCatalogueEntry> SatCatalogue::builtIn() {
    return {
        {"iss", "ISS (ZARYA)", 25544,
         {
             dl("iss-voice", "ISS FM Voice [Voice]", 145.800, "NFM", "voice"),
             dl("iss-aprs", "ISS APRS [Data]", 145.825, "APRS", "aprs"),
             dl("iss-sstv", "ISS SSTV [SSTV]", 145.800, "NFM", "sstv"),
         },
         true,
         "Amateur FM / packet / SSTV when scheduled"},
        {"noaa15", "NOAA 15 APT (retired TX — archive/practice)", 25338,
         {dl("n15-apt", "NOAA-15 APT 137.620 [APT]", 137.6200, "APT", "apt")},
         true,
         "Transmitter retired; keep for APT decode practice / archived IQ"},
        {"noaa18", "NOAA 18 APT (retired TX — archive/practice)", 28654,
         {dl("n18-apt", "NOAA-18 APT 137.9125 [APT]", 137.9125, "APT", "apt")},
         true,
         "Transmitter retired; keep for APT decode practice / archived IQ"},
        {"noaa19", "NOAA 19 APT (retired TX — archive/practice)", 33591,
         {dl("n19-apt", "NOAA-19 APT 137.100 [APT]", 137.1000, "APT", "apt")},
         true,
         "Transmitter retired; keep for APT decode practice / archived IQ"},
        {"meteor", "Meteor-M N2-3 (bookmark only)", 57166,
         {dl("meteor-vhf", "Meteor VHF note [Data — no LRPT yet]", 137.1000, "NFM", "data", false)},
         false,
         "Frequency bookmark only — LRPT/SatDump image decode not in this release"},
        {"so50", "SO-50 (SaudiSat)", 27607,
         {dl("so50-fm", "SO-50 FM [Voice]", 436.795, "NFM", "voice")},
         true,
         "FM amateur voice repeater"},
        {"ao91", "AO-91 (Fox-1B)", 43017,
         {dl("ao91-fm", "AO-91 FM [Voice]", 145.960, "NFM", "voice")},
         true,
         "FM amateur voice"},
        {"io117", "IO-117 (GreenCube)", 56224,
         {dl("io117-data", "IO-117 435.310 [Data]", 435.310, "NFM", "data", false)},
         false,
         "Public downlink bookmark — dedicated digipeater decode later"},
        {"iss-uhf", "ISS UHF crossband note", 25544,
         {dl("iss-uhf", "ISS UHF 437.800 [Voice]", 437.800, "NFM", "voice")},
         false,
         "Occasional UHF; confirm current ARISS schedule before arming"},
    };
}

SatCatalogue SatCatalogue::loadOrDefault() {
    SatCatalogue c;
    c.load();
    return c;
}

void SatCatalogue::load() {
    entries_ = builtIn();
    try {
        std::ifstream in(satcomDataDir() + "/catalogue_selection.json");
        if (!in) return;
        nlohmann::json j;
        in >> j;
        if (!j.is_object() || !j.contains("selected")) return;
        std::vector<std::string> sel;
        for (const auto& s : j["selected"]) sel.push_back(s.get<std::string>());
        for (auto& e : entries_) {
            e.selected = false;
            for (const auto& id : sel) {
                if (e.id == id) {
                    e.selected = true;
                    break;
                }
            }
        }
    } catch (...) {
    }
}

void SatCatalogue::save() const {
    try {
        nlohmann::json j;
        j["selected"] = nlohmann::json::array();
        for (const auto& e : entries_) {
            if (e.selected) j["selected"].push_back(e.id);
        }
        std::ofstream out(satcomDataDir() + "/catalogue_selection.json");
        out << j.dump(2);
    } catch (...) {
    }
}

void SatCatalogue::setSelected(const std::string& id, bool on) {
    for (auto& e : entries_) {
        if (e.id == id) {
            e.selected = on;
            break;
        }
    }
}

void SatCatalogue::setSelectedNorad(const std::vector<int>& noradIds) {
    for (auto& e : entries_) {
        e.selected = false;
        for (int n : noradIds) {
            if (e.noradId == n) {
                e.selected = true;
                break;
            }
        }
    }
}

std::vector<SatCatalogueEntry> SatCatalogue::selectedEntries() const {
    std::vector<SatCatalogueEntry> out;
    for (const auto& e : entries_)
        if (e.selected) out.push_back(e);
    return out;
}

nlohmann::json SatCatalogue::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries_) {
        nlohmann::json dls = nlohmann::json::array();
        for (const auto& d : e.downlinks) {
            dls.push_back({{"id", d.id},
                           {"label", d.label},
                           {"freqHz", d.freqHz},
                           {"freqMHz", d.freqHz / 1e6},
                           {"mode", d.mode},
                           {"role", d.role},
                           {"armable", d.armable}});
        }
        arr.push_back({{"id", e.id},
                       {"name", e.name},
                       {"noradId", e.noradId},
                       {"selected", e.selected},
                       {"notes", e.notes},
                       {"downlinks", dls}});
    }
    return {{"satellites", arr}};
}

SatCatalogue SatCatalogue::fromJson(const nlohmann::json& j) {
    SatCatalogue c = loadOrDefault();
    if (!j.is_object()) return c;
    if (j.contains("selected") && j["selected"].is_array()) {
        for (auto& e : c.entries_) e.selected = false;
        for (const auto& s : j["selected"]) {
            c.setSelected(s.get<std::string>(), true);
        }
    }
    return c;
}
