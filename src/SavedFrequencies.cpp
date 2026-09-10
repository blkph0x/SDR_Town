#include "SavedFrequencies.h"

#include "DemodModeUtils.h"

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using json = nlohmann::json;

static QString savedFrequenciesPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/saved_frequencies.json";
}

std::vector<SavedFrequency> loadSavedFrequencies()
{
    std::vector<SavedFrequency> out;
    std::ifstream f(savedFrequenciesPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            SavedFrequency sf;
            sf.name = item.value("name", std::string("Saved Frequency"));
            sf.freqHz = item.value("freqHz", 100e6);
            sf.mode = modeFromString(item.value("mode", std::string("AUTO")));
            sf.bandwidthHz = item.value("bandwidthHz", 180000.0);
            sf.lpfHz = item.value("lpfHz", 15000.0);
            sf.lpfEnabled = item.value("lpfEnabled", true);
            sf.squelchDb = item.value("squelchDb", -105.0);
            sf.tags = item.value("tags", std::string());
            if (std::isfinite(sf.freqHz) && sf.freqHz > 0.0) out.push_back(sf);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load saved_frequencies.json: {}", ex.what());
    }
    return out;
}

void saveSavedFrequencies(const std::vector<SavedFrequency>& freqs)
{
    json arr = json::array();
    for (const auto& sf : freqs) {
        arr.push_back({
            {"name", sf.name},
            {"freqHz", sf.freqHz},
            {"mode", modeToString(sf.mode)},
            {"bandwidthHz", sf.bandwidthHz},
            {"lpfHz", sf.lpfHz},
            {"lpfEnabled", sf.lpfEnabled},
            {"squelchDb", sf.squelchDb},
            {"tags", sf.tags},
        });
    }
    try {
        std::ofstream f(savedFrequenciesPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save saved_frequencies.json: {}", ex.what());
    }
}

void populateSavedFrequencyTable(QTableWidget* table, const std::vector<SavedFrequency>& freqs)
{
    if (!table) return;
    table->setRowCount(static_cast<int>(freqs.size()));
    for (int row = 0; row < static_cast<int>(freqs.size()); ++row) {
        const auto& sf = freqs[static_cast<size_t>(row)];
        table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(sf.name)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(sf.freqHz / 1e6, 'f', 5)));
        table->setItem(row, 2, new QTableWidgetItem(modeToQString(sf.mode)));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(sf.bandwidthHz / 1000.0, 'f', 1)));
        table->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(sf.tags)));
    }
}
