#pragma once

#include <string>

struct DeviceInfo;
namespace SoapySDR { class Device; }

// DEC-0128: no sample-path state here. Caller owns device lifetime and I/O lock.
namespace SdrplayControl {
enum class Kind { Agc, Gain, Bandwidth, Setting, Antenna };
struct Change {
    Kind kind;
    std::string key;
    std::string value;
    double number = 0.0;
};
void probe(SoapySDR::Device& device, DeviceInfo& info);
void mergeCapabilities(const DeviceInfo& probed, DeviceInfo& desired);
void prepare(DeviceInfo& desired, const Change& change);
void apply(SoapySDR::Device& device, DeviceInfo& desired);
void applyChange(SoapySDR::Device& device, DeviceInfo& desired, const Change& change);
}
