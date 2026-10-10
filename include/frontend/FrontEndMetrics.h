#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FrontEndMetric {
    std::int64_t sampleIndex = 0;
    std::string utc;
    std::string name;
    double value = 0.0;
    bool hasValue = false;
    std::string text;
};

class FrontEndMetrics {
public:
    void add(std::string name, double value, std::string text = {});
    void addText(std::string name, std::string text);
    const std::vector<FrontEndMetric>& records() const { return records_; }
    std::string toJsonl() const;
    bool writeJsonl(const std::string& path) const;

private:
    std::int64_t next_ = 1;
    std::vector<FrontEndMetric> records_;
};
