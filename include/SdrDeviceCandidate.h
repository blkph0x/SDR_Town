#pragma once

#include <algorithm>
#include <cctype>
#include <string>

// DeviceManager deliberately publishes an RTL-SDR launch proxy when Soapy did
// not enumerate a stick during the safe startup pass.  The proxy is not a fake
// receiver: startStreaming(index, true) uses it to attempt the real hardware
// open and then reports "live hardware" only after valid IQ arrives.
//
// Builds without Soapy also publish explicit demo entries whose labels contain
// "(stub)".  Those entries must never be accepted by Satcom or Inmarsat.
namespace SdrDeviceCandidate {

inline std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline bool isExplicitSimulationLabel(const std::string& label)
{
    return lowerAscii(label).find("(stub)") != std::string::npos;
}

inline bool isDeferredHardwareProxyLabel(const std::string& label)
{
    const std::string lower = lowerAscii(label);
    return lower.find("placeholder") != std::string::npos &&
           lower.find("enable will try hardware") != std::string::npos;
}

inline bool canAttemptRealHardware(const std::string& label)
{
    if (isExplicitSimulationLabel(label)) return false;

    const std::string lower = lowerAscii(label);
    if (lower.find("placeholder") == std::string::npos) return true;

    // Unknown/generic placeholders stay rejected.  Only DeviceManager's
    // deliberate real-open proxy is eligible.
    return isDeferredHardwareProxyLabel(label);
}

} // namespace SdrDeviceCandidate
