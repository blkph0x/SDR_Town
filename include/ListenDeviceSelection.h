#pragma once
#include <cstddef>
#include <string>

// Resolve a physical identity on every tune. A missing or duplicate identity
// cannot become the first RTL dongle merely because USB enumeration changed.
template<class Devices>
size_t resolveListenDevice(const Devices& devices, const std::string& key, size_t fallback) {
    if (key.empty()) return fallback;
    size_t match = size_t(-1);
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].stableKey != key) continue;
        if (match != size_t(-1)) return size_t(-1);
        match = i;
    }
    return match;
}
