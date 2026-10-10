#include "frontend/FrontEndPower.h"

bool FrontEndPower::selectBackend(BiasBackend backend, std::string* error) {
    if (enabled_ && backend != backend_ && backend != BiasBackend::None) {
        if (error) *error = "Disable power before changing the Bias-T source";
        return false;
    }
    backend_ = backend;
    if (error) error->clear();
    return true;
}

bool FrontEndPower::enable(bool confirm, int voltageV, bool tone22kHz, std::string* error) {
    if (!confirm) {
        if (error) *error = "Bias-T enable requires explicit confirmation";
        return false;
    }
    if (backend_ == BiasBackend::None) {
        if (error) *error = "Select a Bias-T source before enabling power";
        return false;
    }
    if (voltageV != 0 && voltageV != 13 && voltageV != 18) {
        if (error) *error = "LNB voltage must be 13 V or 18 V";
        return false;
    }
    enabled_ = true;
    voltageV_ = voltageV;
    tone_ = tone22kHz;
    record("enable");
    if (error) error->clear();
    return true;
}

void FrontEndPower::disable(const std::string& reason) {
    enabled_ = false;
    voltageV_ = 0;
    tone_ = false;
    record(reason.empty() ? "disable" : reason);
}

void FrontEndPower::noteMeasurement(std::optional<double> volts, std::optional<double> milliamps, double maxCurrentMa) {
    if (milliamps && *milliamps > maxCurrentMa) disable("over-current");
    (void)volts;
}

void FrontEndPower::record(const std::string& reason) {
    audit_.push_back(PowerTransition{reason, enabled_, backend_, voltageV_, tone_});
}
