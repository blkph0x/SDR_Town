#pragma once
#include <QJsonObject>
#include <chrono>

// Owner-thread sampler. No identifiers, process paths, RF content or allocation trace.
class ProcessPerformance {
public:
    QJsonObject sample();
    static double cpuPercent(double cpuSeconds,double wallSeconds,int logicalCpus);
private:
    std::chrono::steady_clock::time_point previous_{};
    double previousCpu_=0;
};
