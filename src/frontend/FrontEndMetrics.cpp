#include "frontend/FrontEndMetrics.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string utcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::string jsonEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n' || c == '\r') { out += " "; continue; }
        out.push_back(c);
    }
    return out;
}
}

void FrontEndMetrics::add(std::string name, double value, std::string text) {
    FrontEndMetric row;
    row.sampleIndex = next_++;
    row.utc = utcNow();
    row.name = std::move(name);
    row.value = value;
    row.hasValue = true;
    row.text = std::move(text);
    records_.push_back(std::move(row));
}

void FrontEndMetrics::addText(std::string name, std::string text) {
    FrontEndMetric row;
    row.sampleIndex = next_++;
    row.utc = utcNow();
    row.name = std::move(name);
    row.text = std::move(text);
    records_.push_back(std::move(row));
}

std::string FrontEndMetrics::toJsonl() const {
    std::ostringstream out;
    for (const auto& row : records_) {
        out << "{\"sampleIndex\":" << row.sampleIndex
            << ",\"utc\":\"" << jsonEscape(row.utc) << "\""
            << ",\"name\":\"" << jsonEscape(row.name) << "\"";
        if (row.hasValue) out << ",\"value\":" << row.value;
        if (!row.text.empty()) out << ",\"text\":\"" << jsonEscape(row.text) << "\"";
        out << "}\n";
    }
    return out.str();
}

bool FrontEndMetrics::writeJsonl(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << toJsonl();
    return static_cast<bool>(file);
}
