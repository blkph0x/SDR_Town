#include "InmarsatBandPlan.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <fstream>
#include <spdlog/spdlog.h>

namespace {

std::vector<std::string> candidateDirs() {
    std::vector<std::string> out;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        out.push_back((appDir + "/data/inmarsat").toStdString());
        out.push_back((appDir + "/../data/inmarsat").toStdString());
        out.push_back((appDir + "/../../data/inmarsat").toStdString());
    }
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!data.isEmpty()) out.push_back((data + "/inmarsat").toStdString());
#ifdef SDR_TOWN_SOURCE_DIR
    out.push_back(std::string(SDR_TOWN_SOURCE_DIR) + "/data/inmarsat");
#endif
    // Common source relative from build trees
    out.push_back("data/inmarsat");
    out.push_back("../data/inmarsat");
    out.push_back("../../data/inmarsat");
    return out;
}

InmarsatBandPlan parsePlan(const nlohmann::json& j) {
    InmarsatBandPlan p;
    p.id = j.value("id", "");
    p.name = j.value("name", p.id);
    p.orbitalSlotDegE = j.value("orbitalSlotDegE", 0.0);
    p.region = j.value("region", "");
    p.source = j.value("source", "");
    if (j.contains("aliases") && j["aliases"].is_array()) {
        for (const auto& a : j["aliases"]) p.aliases.push_back(a.get<std::string>());
    }
    if (j.contains("channels") && j["channels"].is_array()) {
        for (const auto& c : j["channels"]) {
            InmarsatChannel ch;
            ch.label = c.value("label", "");
            ch.freqHz = c.value("freqHz", 0.0);
            ch.mode = c.value("mode", "aero_oqpsk");
            ch.baud = c.value("baud", 10500);
            p.channels.push_back(ch);
        }
    }
    return p;
}

} // namespace

nlohmann::json InmarsatBandPlan::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["aliases"] = aliases;
    j["orbitalSlotDegE"] = orbitalSlotDegE;
    j["region"] = region;
    j["source"] = source;
    nlohmann::json chs = nlohmann::json::array();
    for (const auto& c : channels) {
        chs.push_back({{"label", c.label},
                       {"freqHz", c.freqHz},
                       {"freqMHz", c.freqHz / 1e6},
                       {"mode", c.mode},
                       {"baud", c.baud}});
    }
    j["channels"] = chs;
    return j;
}

InmarsatBandPlanStore& InmarsatBandPlanStore::instance() {
    static InmarsatBandPlanStore s;
    return s;
}

InmarsatBandPlanStore::InmarsatBandPlanStore() { reload(nullptr); }

void InmarsatBandPlanStore::reload(std::string* error) {
    plans_.clear();
    std::string used;
    for (const auto& dir : candidateDirs()) {
        QDir d(QString::fromStdString(dir));
        if (!d.exists()) continue;
        const auto files = d.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
        if (files.isEmpty()) continue;
        for (const auto& f : files) {
            try {
                std::ifstream in((d.absoluteFilePath(f)).toStdString());
                if (!in) continue;
                nlohmann::json j;
                in >> j;
                auto plan = parsePlan(j);
                if (!plan.id.empty()) plans_.push_back(std::move(plan));
            } catch (const std::exception& ex) {
                spdlog::warn("Inmarsat band plan {}: {}", f.toStdString(), ex.what());
            }
        }
        if (!plans_.empty()) {
            used = dir;
            break;
        }
    }
    if (plans_.empty()) {
        // DEC-0132: missing survey data must never create invented RF channels.
        if (error) *error = "No band plan JSON found; enter a known channel manually";
        spdlog::warn("InmarsatBandPlanStore: no plans found; manual tuning remains available");
    } else {
        spdlog::info("InmarsatBandPlanStore: loaded {} plans from {}", plans_.size(), used);
        if (error) error->clear();
    }
}

const InmarsatBandPlan* InmarsatBandPlanStore::findById(const std::string& id) const {
    for (const auto& p : plans_) {
        if (p.id == id) return &p;
        for (const auto& a : p.aliases) {
            if (a == id) return &p;
        }
    }
    return nullptr;
}

nlohmann::json InmarsatBandPlanStore::catalogueJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : plans_) arr.push_back(p.toJson());
    return {{"ok", true}, {"plans", arr}};
}
