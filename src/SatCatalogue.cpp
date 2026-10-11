#include "SatCatalogue.h"
#include "SatObserverConfig.h"
#include "SstvModes.h"
#include <QSaveFile>
#include <algorithm>
#include <cmath>

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
    auto sstv = [](const char* id, const char* label, double mhz, const char* reference) {
        auto d = dl(id, label, mhz, "NFM", "sstv");
        // DEC-0218: receive filter setting, not a claim of transmitted bandwidth.
        d.bandwidthHz = 20e3;
        d.reference = reference;
        d.verifiedUtc = "2026-10-11";
        return d;
    };
    constexpr auto ariss = "https://www.ariss.org/upcoming-sstv-events.html";
    constexpr auto images = "https://www.amsat.org/image-transmitting-satellites/";
    return {
        {"iss", "ISS (ZARYA)", 25544,
         {
             dl("iss-voice", "ISS FM Voice [Voice]", 145.800, "NFM", "voice"),
             dl("iss-aprs", "ISS APRS [Data]", 145.825, "APRS", "aprs"),
             sstv("iss-sstv", "ISS VHF SSTV (PD120 events)", 145.800, ariss),
         },
         true,
         "Amateur FM / packet / SSTV when scheduled"},
        {"iss-sstv-uhf", "ISS UHF SSTV (Robot36 events)", 25544,
         {sstv("iss-sstv-uhf", "ISS SSTV 437.550", 437.550, ariss)}, false,
         "Scheduled events only; Oct 2-6 2026 event has ended. Confirm ARISS schedule."},
        {"umka1", "UmKA-1 / RS40S", 57172,
         {sstv("umka1-sstv", "SSTV Robot36 events", 437.625, images)}, false,
         "SSTV shares telemetry frequency; confirm operator activation."},
        {"vizard-meteo", "VIZARD-meteo / RS38S", 57189,
         {sstv("vizard-sstv", "SSTV Robot36 events", 437.825, ariss)}, false,
         "SSTV when scheduled; not an always-on image service."},
        {"utmn2", "UTMN-2 / RS27S", 57203,
         {sstv("utmn2-sstv", "SSTV Robot36 events", 436.125, images)}, false,
         "Confirm current operator schedule."},
        {"sonate2", "SONATE-2", 59112,
         {sstv("sonate2-sstv", "SSTV Martin M1 events", 145.880, images)}, false,
         "Confirm university SSTV schedule."},
        {"qmrkwt2", "QMR-KWT-2 / RS95S", 67291,
         {sstv("qmrkwt2-sstv", "SSTV Robot36 events", 436.950, images)}, false,
         "SSTV when scheduled; same frequency also carries telemetry/repeater."},
        {"funcube1", "FUNcube-1 / AO-73 (telemetry, not SSTV)", 39444,
         {dl("funcube1-tlm", "1200 bps BPSK telemetry (unsupported)", 145.935, "USB", "data", false)},
         false, "https://funcube.org.uk/working-documents/downloads-summary/ - not an SSTV beacon."},
        {"galapagos", "GALAPAGOS-UTE-SWSU / HC1PX", 67279,
         {sstv("galapagos-sstv", "SSTV Robot36 events", 437.050, images)}, false,
         "Confirm current SSTV activation; frequency also carries telemetry."},
        {"sakhacube", "SakhaCube-Cholbon / RS18S", 67290,
         {sstv("sakhacube-sstv", "SSTV Robot36 events", 437.350, images)}, false,
         "https://www.yksa.space/projects/missions/sakhacube-cholbon - scheduled events only."},
        {"monitor3", "Monitor-3 / RS58S", 57180,
         {sstv("monitor3-sstv", "SSTV events", 435.290, "https://spacepi.space/satellites/monitor-3/")}, false,
         "Confirm current schedule; receive-only profile."},
        {"monitor4", "Monitor-4 / RS57S", 57182,
         {sstv("monitor4-sstv", "SSTV Robot family events", 436.080, "https://spacepi.space/satellites/monitor-4/")}, false,
         "Robot72/36/24 and B&W modes documented by operator; Auto image recommended."},
        {"lobachevsky", "Lobachevsky / RS83S", 67284,
         {sstv("lobachevsky-sstv", "Analog SSTV events (not SSDV)", 436.320, images)}, false,
         "https://sputnik.unn.ru/ - analog SSTV only; SSDV is a separate unsupported service."},
        {"noaa15", "NOAA 15 APT (retired TX — archive/practice)", 25338,
         {dl("n15-apt", "NOAA-15 APT 137.620 [APT]", 137.6200, "APT", "apt", false)},
         true,
         "Transmitter retired; keep for APT decode practice / archived IQ"},
        {"noaa18", "NOAA 18 APT (retired TX — archive/practice)", 28654,
         {dl("n18-apt", "NOAA-18 APT 137.9125 [APT]", 137.9125, "APT", "apt", false)},
         true,
         "Transmitter retired; keep for APT decode practice / archived IQ"},
        {"noaa19", "NOAA 19 APT (retired TX — archive/practice)", 33591,
         {dl("n19-apt", "NOAA-19 APT 137.100 [APT]", 137.1000, "APT", "apt", false)},
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
        if (!j.is_object()) return;
        if (j.contains("customProfiles") && j["customProfiles"].is_array()) {
            for (const auto& profile : j["customProfiles"]) {
                SatCatalogue parsed = fromJson({{"satellites", nlohmann::json::array({profile})}});
                for (const auto& e : parsed.entries()) if (e.custom) upsertSstvProfile(e);
            }
        }
        if (!j.contains("selected")) return;
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
        j["customProfiles"] = nlohmann::json::array();
        const auto catalogue = toJson();
        for (const auto& e : catalogue["satellites"]) if (e.value("custom", false)) j["customProfiles"].push_back(e);
        QSaveFile out(QString::fromStdString(satcomDataDir() + "/catalogue_selection.json"));
        const auto text = j.dump(2);
        if (out.open(QIODevice::WriteOnly) && out.write(text.data(), text.size()) == qint64(text.size())) out.commit();
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
                           {"armable", d.armable}, {"bandwidthHz", d.bandwidthHz},
                           {"imageMode", d.imageMode}, {"reference", d.reference}, {"verifiedUtc", d.verifiedUtc}});
        }
        arr.push_back({{"id", e.id},
                       {"name", e.name},
                       {"noradId", e.noradId},
                       {"selected", e.selected},
                       {"notes", e.notes},
                       {"custom", e.custom},
                       {"downlinks", dls}});
    }
    return {{"satellites", arr}};
}

SatCatalogue SatCatalogue::fromJson(const nlohmann::json& j) {
    SatCatalogue c;
    c.entries_ = builtIn();
    if (!j.is_object()) return c;
    if (j.contains("satellites") && j["satellites"].is_array()) {
        for (auto& e : c.entries_) e.selected = false;
        for (const auto& item : j["satellites"]) {
            if (!item.is_object()) continue;
            try {
                if (item.value("custom", false)) {
                    SatCatalogueEntry e;
                    e.id = item.at("id"); e.name = item.at("name"); e.noradId = item.at("noradId");
                    e.selected = item.value("selected", false); e.custom = true;
                    e.notes = item.value("notes", std::string{});
                    const auto& d = item.at("downlinks").at(0);
                    SatDownlink link;
                    link.id = d.at("id"); link.label = d.at("label"); link.freqHz = d.at("freqHz");
                    link.mode = d.at("mode"); link.role = "sstv"; link.bandwidthHz = d.at("bandwidthHz");
                    link.imageMode = d.value("imageMode", "auto"); link.reference = d.value("reference", "");
                    link.verifiedUtc = d.value("verifiedUtc", "");
                    e.downlinks.push_back(link);
                    c.upsertSstvProfile(e);
                } else c.setSelected(item.value("id", ""), item.value("selected", false));
            } catch (const nlohmann::json::exception&) { /* Reject malformed profile, retain built-ins. */ }
        }
    }
    if (j.contains("selected") && j["selected"].is_array()) {
        for (auto& e : c.entries_) e.selected = false;
        for (const auto& s : j["selected"]) {
            c.setSelected(s.get<std::string>(), true);
        }
    }
    return c;
}

bool SatCatalogue::upsertSstvProfile(const SatCatalogueEntry& e, std::string* error) {
    auto fail = [&](const char* why) { if (error) *error = why; return false; };
    const auto safeId = [](const std::string& id) {
        return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        });
    };
    if (!e.custom || !safeId(e.id) || e.id.rfind("custom-", 0) != 0 || e.name.empty() || e.name.size() > 120 ||
        e.noradId <= 0 || e.noradId > 99999 || e.downlinks.size() != 1 || e.notes.size() > 512)
        return fail("Supply a name, a TLE NORAD ID (1-99999) and one SSTV downlink.");
    const auto& d = e.downlinks.front();
    if (!safeId(d.id) || d.role != "sstv" || !d.armable || d.label.size() > 120 ||
        !std::isfinite(d.freqHz) || d.freqHz < 100e3 || d.freqHz > 6e9 ||
        !std::isfinite(d.bandwidthHz) || d.bandwidthHz < 6e3 || d.bandwidthHz > 50e3 ||
        (d.mode != "NFM" && d.mode != "USB" && d.mode != "LSB" && d.mode != "AM") ||
        d.reference.size() > 512 || d.verifiedUtc.size() > 32)
        return fail("Invalid receive frequency, RF mode or bandwidth (6-50 kHz).");
    if (d.imageMode != "auto" && (d.imageMode == "hamdrm" || !sstvModeById(d.imageMode)))
        return fail("Select Auto or a supported analog SSTV image mode.");
    auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& old) { return old.id == e.id; });
    if (found != entries_.end()) {
        if (!found->custom) return fail("Built-in profiles cannot be overwritten.");
        *found = e;
    } else {
        if (std::count_if(entries_.begin(), entries_.end(), [](const auto& old) { return old.custom; }) >= 32)
            return fail("Maximum 32 custom SSTV profiles.");
        entries_.push_back(e);
    }
    if (error) error->clear();
    return true;
}
