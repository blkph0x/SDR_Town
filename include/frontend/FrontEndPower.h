#pragma once

#include <optional>
#include <string>
#include <vector>

// DEC-0210: commanded state only. A driver ACK is not a voltmeter.
enum class BiasBackend { None, SdrInternal, External };

struct PowerTransition {
    std::string reason;
    bool enabled = false;
    BiasBackend backend = BiasBackend::None;
    int voltageV = 0;
    bool tone22kHz = false;
};

class FrontEndPower {
public:
    bool enabled() const { return enabled_; }
    BiasBackend backend() const { return backend_; }
    const std::vector<PowerTransition>& audit() const { return audit_; }

    // Selecting a backend while the other is ON is refused.
    bool selectBackend(BiasBackend backend, std::string* error);

    // confirm must be true. voltage is 0, 13, or 18.
    bool enable(bool confirm, int voltageV, bool tone22kHz, std::string* error);
    void disable(const std::string& reason);

    void noteMeasurement(std::optional<double> volts, std::optional<double> milliamps, double maxCurrentMa);

private:
    void record(const std::string& reason);
    bool enabled_ = false;
    BiasBackend backend_ = BiasBackend::None;
    int voltageV_ = 0;
    bool tone_ = false;
    std::vector<PowerTransition> audit_;
};
