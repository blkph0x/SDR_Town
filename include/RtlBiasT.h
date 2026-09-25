#pragma once
#include <optional>
#include <string>

namespace SoapySDR { class Device; }
namespace RtlBiasT {
struct State {
    bool probed = false;
    bool supported = false; // Driver API only; not a physical-circuit claim.
    bool enabled = false;   // Saved, explicit user intent.
    std::optional<bool> reported;
    std::string status;
};
#ifdef HAVE_SOAPYSDR
void probe(SoapySDR::Device& device, State& state);
void apply(SoapySDR::Device& device, State& state, bool enabled);
void powerOff(SoapySDR::Device& device) noexcept;
#endif
}
