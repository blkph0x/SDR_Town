#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded block, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def replace_all(path: str, old: str, new: str, expected: int) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{path}: expected {expected} occurrences of {old!r}, found {count}")
    p.write_text(text.replace(old, new), encoding="utf-8")


placeholder_block = '''bool isPlaceholderDevice(const DeviceInfo& device) {
    std::string label = device.label;
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return label.find("placeholder") != std::string::npos ||
           label.find("(stub)") != std::string::npos;
}

'''

# Satcom engine: permit only DeviceManager's deferred real-open proxy, not demo stubs.
replace_once(
    "src/SatcomScannerEngine.cpp",
    '#include "SstvReceiverFeed.h"\n',
    '#include "SstvReceiverFeed.h"\n#include "SdrDeviceCandidate.h"\n',
)
replace_once("src/SatcomScannerEngine.cpp", placeholder_block, "")
replace_all(
    "src/SatcomScannerEngine.cpp",
    "!isPlaceholderDevice(devices[i])",
    "SdrDeviceCandidate::canAttemptRealHardware(devices[i].label)",
    2,
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "!isPlaceholderDevice(devices[selected.deviceIndex])",
    "SdrDeviceCandidate::canAttemptRealHardware(devices[selected.deviceIndex].label)",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    'if (error) *error = "No real SDR device is available; placeholder/stub devices cannot run Satcom";',
    'if (error) *error = "No hardware-capable SDR entry is available. Rescan devices; explicit (stub) demo entries cannot run Satcom.";',
)

# Inmarsat engine uses the same DeviceManager inventory and had the same false rejection.
replace_once(
    "src/InmarsatEngine.cpp",
    '#include "Receiver.h"\n',
    '#include "Receiver.h"\n#include "SdrDeviceCandidate.h"\n',
)
replace_once("src/InmarsatEngine.cpp", placeholder_block, "")
replace_all(
    "src/InmarsatEngine.cpp",
    "!isPlaceholderDevice(devices[i])",
    "SdrDeviceCandidate::canAttemptRealHardware(devices[i].label)",
    2,
)
replace_once(
    "src/InmarsatEngine.cpp",
    "!isPlaceholderDevice(devices[selected.deviceIndex])",
    "SdrDeviceCandidate::canAttemptRealHardware(devices[selected.deviceIndex].label)",
)
replace_once(
    "src/InmarsatEngine.cpp",
    'if (error) *error = "No real SDR device is available; placeholder/stub devices cannot run Inmarsat";',
    'if (error) *error = "No hardware-capable SDR entry is available. Rescan devices; explicit (stub) demo entries cannot run Inmarsat.";',
)

# Inmarsat UI previously hid the deferred real-open entry completely.
widget_placeholder_block = '''bool isPlaceholder(const DeviceInfo& device) {
    const QString label = QString::fromStdString(device.label).toLower();
    return label.contains("placeholder") || label.contains("(stub)");
}

'''
replace_once(
    "src/InmarsatWidget.cpp",
    '#include "DeviceManager.h"\n',
    '#include "DeviceManager.h"\n#include "SdrDeviceCandidate.h"\n',
)
replace_once("src/InmarsatWidget.cpp", widget_placeholder_block, "")
replace_once(
    "src/InmarsatWidget.cpp",
    '    const QString state = streaming ? "LIVE" : (device.enabled ? "READY" : "AVAILABLE");\n',
    '    const QString state = streaming ? "LIVE"\n        : (device.enabled ? "READY"\n           : (SdrDeviceCandidate::isDeferredHardwareProxyLabel(device.label)\n                  ? "PROBE ON START" : "AVAILABLE"));\n',
)
replace_once(
    "src/InmarsatWidget.cpp",
    "        if (!ok || index >= devices.size() || isPlaceholder(devices[index])) return;\n",
    "        if (!ok || index >= devices.size() ||\n            !SdrDeviceCandidate::canAttemptRealHardware(devices[index].label)) return;\n",
)
replace_once(
    "src/InmarsatWidget.cpp",
    "        if (isPlaceholder(devices[index])) continue;\n",
    "        if (!SdrDeviceCandidate::canAttemptRealHardware(devices[index].label)) continue;\n",
)

# Make the same deferred-open state explicit in the Satcom receiver selector.
replace_once(
    "src/SatcomScannerWidget.cpp",
    '#include "DeviceManager.h"\n',
    '#include "DeviceManager.h"\n#include "SdrDeviceCandidate.h"\n',
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    '    const QString state = streaming ? "LIVE" : (device.enabled ? "READY" : "AVAILABLE");\n',
    '    const QString state = streaming ? "LIVE"\n        : (device.enabled ? "READY"\n           : (SdrDeviceCandidate::isDeferredHardwareProxyLabel(device.label)\n                  ? "PROBE ON START" : "AVAILABLE"));\n',
)

# Regression coverage for the exact label that triggered the field failure.
replace_once(
    "tests/test_satcom.cpp",
    '#include "SatcomHostServices.h"\n',
    '#include "SatcomHostServices.h"\n#include "SdrDeviceCandidate.h"\n',
)
first_test = 'TEST_CASE("AX.25 FCS matches known payload", "[satcom][ax25]")\n'
policy_test = '''TEST_CASE("Deferred hardware proxy is eligible but demo stubs are rejected", "[satcom][device]")
{
    CHECK(SdrDeviceCandidate::isDeferredHardwareProxyLabel(
        "RTL-SDR placeholder (enable will try hardware)"));
    CHECK(SdrDeviceCandidate::canAttemptRealHardware(
        "RTL-SDR placeholder (enable will try hardware)"));
    CHECK(SdrDeviceCandidate::canAttemptRealHardware("SDRplay RSPdx"));
    CHECK_FALSE(SdrDeviceCandidate::canAttemptRealHardware("RTL-SDR (stub)"));
    CHECK_FALSE(SdrDeviceCandidate::canAttemptRealHardware("SDRplay RSPdx (stub)"));
    CHECK_FALSE(SdrDeviceCandidate::canAttemptRealHardware("generic placeholder"));
}

'''
replace_once("tests/test_satcom.cpp", first_test, policy_test + first_test)

print("Applied Satcom/Inmarsat deferred-hardware proxy selection fix")
