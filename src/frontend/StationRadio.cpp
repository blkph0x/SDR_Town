#include "frontend/StationRadio.h"

namespace {
StationRadioFn& radioSink() {
    static StationRadioFn sink;
    return sink;
}
}

void setStationRadio(StationRadioFn sink) {
    radioSink() = std::move(sink);
}

bool commandStationRadio(const StationRadioRequest& request, std::string* error) {
    if (!radioSink()) {
        const bool needsRadio = request.tune ||
            (request.biasBackend == BiasBackend::SdrInternal && request.biasEnable);
        if (needsRadio) {
            if (error) *error = "No radio is bound to the station";
            return false;
        }
        return true;
    }
    return radioSink()(request, error);
}
