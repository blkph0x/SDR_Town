#include "RtlBiasT.h"
#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Device.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace RtlBiasT {
namespace {
bool read(SoapySDR::Device& device) {
    const auto value = device.readSetting("biastee");
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("RTL bias-T driver returned an invalid state");
}
}
void probe(SoapySDR::Device& device, State& state) {
    state.probed = false;
    state.supported = false;
    state.reported.reset();
    for (const auto& info : device.getSettingInfo())
        if (info.key == "biastee" && info.type == SoapySDR::ArgInfo::BOOL) state.supported = true;
    if (state.supported) state.reported = read(device);
    state.probed = true;
    state.status = state.supported ? "Driver supports bias-T; physical circuit not detected" : "Bias-T is not exposed by this driver";
}
void apply(SoapySDR::Device& device, State& state, bool enabled) {
    if (!state.probed || !state.supported)
        throw std::runtime_error("RTL bias-T is not advertised by the opened driver");
    // DEC-0129: always write, even OFF after open. Cached false alone cannot
    // establish that DC left on by another process has actually been removed.
    bool actual = false;
    try {
        device.writeSetting("biastee", enabled ? "true" : "false");
        actual = read(device);
        if (actual != enabled) throw std::runtime_error("RTL bias-T driver readback mismatch");
    } catch (...) {
        state.reported.reset();
        // A write can succeed before readback fails. Do not leave an unconfirmed
        // ON request energized while the GUI correctly rolls its checkbox back.
        if (enabled) powerOff(device);
        throw;
    }
    state.enabled = enabled;
    state.reported = actual;
    state.status = enabled ? "Driver reports ON (voltage not measured)" : "Driver reports OFF (voltage not measured)";
}
void powerOff(SoapySDR::Device& device) noexcept {
    try {
        State state;
        // Still attempt OFF if the readback is broken: do not make removal of
        // DC conditional on successfully reading the previous cached value.
        state.probed = true;
        for (const auto& info : device.getSettingInfo())
            if (info.key == "biastee" && info.type == SoapySDR::ArgInfo::BOOL) state.supported = true;
        if (state.supported) apply(device, state, false);
    } catch (const std::exception& ex) {
        spdlog::warn("RTL bias-T power-off not confirmed: {}; disconnect USB to guarantee power off", ex.what());
    } catch (...) {
        spdlog::warn("RTL bias-T power-off failed; disconnect USB to guarantee power off");
    }
}
}
#endif
