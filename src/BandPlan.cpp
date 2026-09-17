#include "BandPlan.h"
#include <QSettings>
#include <QUrl>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace {
using json = nlohmann::json;
const char* acma = "https://www.acma.gov.au/australian-radiofrequency-spectrum-plan";
const char* ofcom = "https://www.ofcom.org.uk/spectrum/frequencies/uk-fat";
const char* fcc = "https://www.fcc.gov/engineering-technology/policy-and-rules-division/general/radio-spectrum-allocation";
const char* marine = "https://navcen.uscg.gov/international-vhf-marine-radio-channels-freq";

void add(BandPlanProfile& p, const char* name, double low, double high, DemodMode mode,
         double bw, double lpf, double step, const char* decoder, const char* source, int priority = 0) {
    p.entries.push_back({name, low, high, mode, bw, lpf, step, decoder, source, priority});
}

BandPlanProfile national(const char* id, const char* region, const char* country, const char* source) {
    BandPlanProfile p{id, region, country, "National",
        "Partial: broadcast, selected aviation/marine/personal-radio services; not a legal allocation table",
        "Reviewed 2026-09-17", {}};
    add(p, "FM broadcast", std::string(id) == "US" ? 88e6 : 87.5e6, 108e6,
        DemodMode::WFM, 180000, 15000, std::string(id) == "US" ? 200000 : 100000,
        "RDS PI/PS/RT (WFM); numeric PTY", source);
    add(p, "Aeronautical navigation", 108e6, 117.975e6, DemodMode::AUTO, 0, 0, 0,
        "VOR/ILS - not voice", source);
    add(p, "Aeronautical communications (voice/data)", 117.975e6, 137e6,
        DemodMode::AM, 20000, 9000, 0, "AM voice; data requires signal identification", source);
    add(p, "VDL Mode 2 common signalling", 136.9625e6, 136.9875e6,
        DemodMode::AUTO, 0, 0, 0, "VDL2 (not implemented)", source, 10);
    add(p, "Marine VHF (voice/data)", 156e6, 162.05e6,
        DemodMode::AUTO, 0, 0, 0, "Mixed voice/DSC/AIS", marine);
    add(p, "Marine channel 16", 156.7875e6, 156.8125e6,
        DemodMode::NFM, 25000, 4500, 25000, "Analog voice", marine, 10);
    add(p, "Marine DSC channel 70", 156.5125e6, 156.5375e6,
        DemodMode::AUTO, 0, 0, 0, "DSC (not implemented)", marine, 10);
    add(p, "AIS 1", 161.9625e6, 161.9875e6, DemodMode::AUTO, 0, 0, 0,
        "AIS (not implemented)", marine, 10);
    add(p, "AIS 2", 162.0125e6, 162.0375e6, DemodMode::AUTO, 0, 0, 0,
        "AIS (not implemented)", marine, 10);
    return p;
}

std::vector<std::shared_ptr<const BandPlanProfile>> defaults() {
    auto au = national("AU", "ITU Region 3", "Australia", acma);
    add(au, "MW broadcast", 526500, 1606500, DemodMode::AM, 10000, 4500, 9000, "Analog voice", acma);
    const char* cb = "https://www.acma.gov.au/licences/citizen-band-radio-stations-class-licence";
    add(au, "27 MHz CB (AM/SSB)", 26.96e6, 27.41e6, DemodMode::AUTO, 0, 0, 0, "Mixed AM/SSB", cb);
    add(au, "UHF CB", 476.41875e6, 477.41875e6, DemodMode::NFM, 12500, 3000, 12500, "Analog voice", cb);
    // Schedule 1 item 5 and section 7(6): 25 kHz data channels, not voice.
    add(au, "UHF CB telemetry / telecommand", 476.9375e6, 476.9875e6,
        DemodMode::AUTO, 0, 0, 25000, "Data - identify signal", cb, 10);
    auto gb = national("GB", "ITU Region 1", "United Kingdom", ofcom);
    add(gb, "MW broadcast", 526500, 1606500, DemodMode::AM, 10000, 4500, 9000, "Analog voice", ofcom);
    add(gb, "PMR446 (analog/digital)", 446e6, 446.2e6, DemodMode::AUTO, 0, 0, 0,
        "NFM / DMR / dPMR - identify signal", "https://www.ofcom.org.uk/spectrum/radio-equipment/wta-exemptions-jul15");
    auto us = national("US", "ITU Region 2", "United States", fcc);
    add(us, "MW broadcast", 535000, 1705000, DemodMode::AM, 10000, 4500, 10000, "Analog voice", fcc);
    for (int i = 0; i < 7; ++i) {
        const double center = 162.4e6 + i * 25000;
        add(us, "NOAA weather radio", center - 12500, center + 12500,
            DemodMode::NFM, 25000, 4500, 25000, "Voice / SAME (SAME not implemented)",
            "https://www.weather.gov/marine/wxradio");
    }
    return {std::make_shared<const BandPlanProfile>(au), std::make_shared<const BandPlanProfile>(gb),
            std::make_shared<const BandPlanProfile>(us)};
}

std::string field(const json& j, const char* name, size_t maximum = 200) {
    const auto s = j.at(name).get<std::string>();
    if (s.empty() || s.size() > maximum || std::any_of(s.begin(), s.end(),
            [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::invalid_argument(std::string("Invalid band-plan field: ") + name);
    return s;
}
}

std::optional<BandPlanEntry> lookupBand(const BandPlanProfile& profile, double hz) {
    if (!std::isfinite(hz) || hz <= 0) return std::nullopt;
    std::optional<BandPlanEntry> best;
    bool ambiguous = false;
    for (const auto& entry : profile.entries) {
        if (hz < entry.startHz || hz >= entry.endHz) continue;
        const auto span = entry.endHz - entry.startHz;
        const auto bestSpan = best ? best->endHz - best->startHz : 0;
        if (!best || entry.priority > best->priority || (entry.priority == best->priority && span < bestSpan)) {
            best = entry;
            ambiguous = false;
        } else if (entry.priority == best->priority && span == bestSpan &&
                   (entry.mode != best->mode || entry.bandwidthHz != best->bandwidthHz ||
                    entry.lpfHz != best->lpfHz || entry.decoder != best->decoder)) {
            ambiguous = true;
        }
    }
    if (best && ambiguous) {
        best->name = "Overlapping services";
        best->mode = DemodMode::AUTO;
        best->bandwidthHz = best->lpfHz = best->stepHz = 0;
        best->decoder = "Ambiguous - identify signal";
    }
    return best;
}

std::vector<BandPlanEntry> visibleBandSections(const BandPlanProfile& profile, double low, double high) {
    if (!std::isfinite(low) || !std::isfinite(high) || high <= low) return {};
    std::vector<double> edges{low, high};
    for (const auto& e : profile.entries) if (e.endHz > low && e.startHz < high) {
        edges.push_back(std::max(low, e.startHz));
        edges.push_back(std::min(high, e.endHz));
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::vector<BandPlanEntry> sections;
    for (size_t i=1; i<edges.size(); ++i) {
        auto band = lookupBand(profile, edges[i-1]+(edges[i]-edges[i-1])/2);
        if (!band) continue;
        band->startHz=edges[i-1]; band->endHz=edges[i];
        if (!sections.empty() && sections.back().endHz==band->startHz &&
            sections.back().name==band->name && sections.back().mode==band->mode &&
            sections.back().decoder==band->decoder) sections.back().endHz=band->endHz;
        else sections.push_back(*band);
    }
    return sections;
}

BandPlanProfile parseBandPlan(const std::string& text) {
    if (text.size() > 262144) throw std::invalid_argument("Band plan exceeds 256 KiB");
    const auto j = json::parse(text, [](int depth, json::parse_event_t, json&) {
        if (depth > 12) throw std::invalid_argument("Band-plan JSON is too deeply nested");
        return true;
    });
    if (j.at("schema") != "sdr-town-bandplan-v1") throw std::invalid_argument("Unsupported band-plan schema");
    BandPlanProfile p{field(j, "id", 64), field(j, "region"), field(j, "country"),
        field(j, "location"), field(j, "coverage"), field(j, "revision"), {}};
    if (p.id == "AU" || p.id == "GB" || p.id == "US") throw std::invalid_argument("Use a unique local profile id");
    const auto& entries = j.at("entries");
    if (!entries.is_array() || entries.empty() || entries.size() > 1024)
        throw std::invalid_argument("Band plan must contain 1-1024 entries");
    for (const auto& e : entries) {
        BandPlanEntry b;
        b.name = field(e, "name"); b.source = field(e, "source", 1024); b.decoder = field(e, "decoder");
        const QUrl sourceUrl(QString::fromStdString(b.source), QUrl::StrictMode);
        if (!sourceUrl.isValid() || sourceUrl.scheme() != "https" || sourceUrl.host().isEmpty() ||
            !sourceUrl.userInfo().isEmpty()) throw std::invalid_argument("A source HTTPS URL is required");
        const auto mode = field(e, "mode", 8);
        const std::vector<std::pair<std::string, DemodMode>> modes = {{"AUTO", DemodMode::AUTO},
            {"NFM", DemodMode::NFM}, {"WFM", DemodMode::WFM}, {"AM", DemodMode::AM},
            {"USB", DemodMode::USB}, {"LSB", DemodMode::LSB}, {"CW", DemodMode::CW}};
        auto found = std::find_if(modes.begin(), modes.end(), [&](const auto& m) { return m.first == mode; });
        if (found == modes.end()) throw std::invalid_argument("Invalid demodulator name");
        b.mode = found->second;
        b.startHz = e.at("startHz").get<double>(); b.endHz = e.at("endHz").get<double>();
        b.bandwidthHz = e.at("bandwidthHz").get<double>(); b.lpfHz = e.at("lpfHz").get<double>();
        b.stepHz = e.at("stepHz").get<double>();
        if (e.contains("priority") && (!e.at("priority").is_number_integer() ||
                e.at("priority").get<double>() < -100 || e.at("priority").get<double>() > 100))
            throw std::invalid_argument("Band-plan priority must be an integer from -100 to 100");
        b.priority = e.value("priority", 0);
        for (double value : {b.startHz, b.endHz, b.bandwidthHz, b.lpfHz, b.stepHz})
            if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite band-plan number");
        if (b.startHz <= 0 || b.endHz <= b.startHz || b.endHz > 300e9 ||
            b.bandwidthHz < 0 || b.bandwidthHz > 500000 || b.lpfHz < 0 || b.lpfHz > 200000 ||
            b.stepHz < 0 || b.stepHz > 1e7 || b.priority < -100 || b.priority > 100 ||
            (b.mode != DemodMode::AUTO && (b.bandwidthHz < 500 || b.lpfHz < 100)))
            throw std::invalid_argument("Band-plan frequency/filter range is invalid");
        p.entries.push_back(std::move(b));
    }
    return p;
}

BandPlanCatalog& BandPlanCatalog::instance() { static BandPlanCatalog catalog; return catalog; }
BandPlanCatalog::BandPlanCatalog() : profiles_(defaults()), active_(profiles_.front()) {
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "SDR_Town", "SDR Town");
    const auto locals = settings.value("bandplan/localProfiles").toStringList();
    for (const auto& local : locals.mid(0, 32)) {
        try { importProfile(local.toStdString()); }
        catch (const std::exception&) { /* Reject invalid saved import; built-ins remain available. */ }
    }
    select(settings.value("bandplan/selected", "AU").toString().toStdString());
}
std::shared_ptr<const BandPlanProfile> BandPlanCatalog::active() const { return active_.load(); }
std::vector<std::shared_ptr<const BandPlanProfile>> BandPlanCatalog::profiles() const {
    std::lock_guard<std::mutex> lock(mutex_); return profiles_;
}
bool BandPlanCatalog::select(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : profiles_) if (p->id == id) { active_.store(p); return true; }
    return false;
}
void BandPlanCatalog::importProfile(const std::string& text) {
    auto p = std::make_shared<const BandPlanProfile>(parseBandPlan(text));
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const auto& old) { return old->id == p->id; });
    if (it == profiles_.end()) {
        if (profiles_.size() >= 35) throw std::invalid_argument("Maximum 32 local profiles");
        profiles_.push_back(p);
    } else *it = p;
    if (active()->id == p->id) active_.store(p);
}
std::optional<BandPlanEntry> findBandPlanForFrequency(double hz) {
    auto entry = lookupBand(*BandPlanCatalog::instance().active(), hz);
    if (entry && entry->mode == DemodMode::AUTO) return std::nullopt;
    return entry;
}
std::vector<BandPlanEntry> builtInBandPlans() { return BandPlanCatalog::instance().active()->entries; }

const char* bandPlanModeName(DemodMode mode) {
    switch (mode) {
    case DemodMode::AM: return "AM";
    case DemodMode::NFM: return "NFM";
    case DemodMode::WFM: return "WFM";
    case DemodMode::USB: return "USB";
    case DemodMode::LSB: return "LSB";
    case DemodMode::CW: return "CW";
    default: return "AUTO";
    }
}
std::string serializeBandPlan(const BandPlanProfile& p) {
    json j = {{"schema", "sdr-town-bandplan-v1"}, {"id", p.id}, {"region", p.region},
        {"country", p.country}, {"location", p.location}, {"coverage", p.coverage},
        {"revision", p.revision}, {"entries", json::array()}};
    for (const auto& e : p.entries) j["entries"].push_back({{"name", e.name},
        {"startHz", e.startHz}, {"endHz", e.endHz}, {"mode", bandPlanModeName(e.mode)},
        {"bandwidthHz", e.bandwidthHz}, {"lpfHz", e.lpfHz}, {"stepHz", e.stepHz},
        {"decoder", e.decoder}, {"source", e.source}, {"priority", e.priority}});
    return j.dump(2);
}
void BandPlanCatalog::persist() const {
    QStringList locals;
    for (const auto& p : profiles()) if (p->id != "AU" && p->id != "GB" && p->id != "US")
        locals.push_back(QString::fromStdString(serializeBandPlan(*p)));
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "SDR_Town", "SDR Town");
    settings.setValue("bandplan/localProfiles", locals);
    settings.setValue("bandplan/selected", QString::fromStdString(active()->id));
}
