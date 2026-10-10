#pragma once

#include "frontend/FrontEndPower.h"

#include <cstddef>
#include <functional>
#include <string>

// A requested radio change. A queued tune is not proof the hardware moved,
// and a Bias-T request is not a measured voltage.
struct StationRadioRequest {
    bool tune = false;
    bool releaseLease = false;
    std::size_t deviceIndex = 0;
    double ifHz = 0.0;
    bool biasEnable = false;
    BiasBackend biasBackend = BiasBackend::None;
    int voltageV = 0;
    bool tone22kHz = false;
};

using StationRadioFn = std::function<bool(const StationRadioRequest&, std::string*)>;

// The application installs the device binding. Tests keep the default, which
// rejects a tune or an internal Bias-T request.
void setStationRadio(StationRadioFn sink);
bool commandStationRadio(const StationRadioRequest& request, std::string* error);
