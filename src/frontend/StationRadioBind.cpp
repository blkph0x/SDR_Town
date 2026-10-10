#include "frontend/StationRadio.h"

#include "DeviceManager.h"
#include "SdrplayProfile.h"

namespace {
bool biasCommand(DeviceManager& manager, const StationRadioRequest& request, bool enable, std::string* error) {
    std::string rtlError;
    if (manager.setRtlBiasT(request.deviceIndex, enable, &rtlError)) return true;
    std::string playError;
    if (manager.setLiveSdrplaySetting(request.deviceIndex, SdrplaySettings::kBiasT,
                                      enable ? "true" : "false", &playError))
        return true;
    if (error) *error = playError.empty() ? rtlError : playError;
    return false;
}

bool applyStationRadio(const StationRadioRequest& request, std::string* error) {
    auto& manager = DeviceManager::instance();
    if (request.biasBackend == BiasBackend::SdrInternal && request.biasEnable) {
        if (!biasCommand(manager, request, true, error)) return false;
    } else if (request.biasBackend != BiasBackend::None) {
        std::string ignored;
        biasCommand(manager, request, false, &ignored);
    }
    if (request.tune) {
        if (!manager.retuneWithLease(request.deviceIndex, request.ifHz,
                                     DeviceManager::DeviceLeaseOwner::Satcom, false, error)) {
            if (request.biasBackend == BiasBackend::SdrInternal && request.biasEnable) {
                std::string ignored;
                biasCommand(manager, request, false, &ignored);
            }
            return false;
        }
    }
    if (request.releaseLease)
        manager.releaseDeviceLease(request.deviceIndex, DeviceManager::DeviceLeaseOwner::Satcom);
    return true;
}

struct InstallStationRadio {
    InstallStationRadio() { setStationRadio(applyStationRadio); }
};

InstallStationRadio installStationRadio;
}
