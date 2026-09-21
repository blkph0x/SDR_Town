from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    s = p.read_text(encoding="utf-8-sig")
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old!r}")
    p.write_text(s.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "include/DeviceManager.h",
    "    void releaseDeviceLease(DeviceLeaseOwner owner);\n"
    "    DeviceLeaseOwner deviceLeaseOwner() const;\n"
    "    bool retuneWithLease(size_t index, double freqHz, DeviceLeaseOwner owner, bool force, std::string* error);",
    "    void releaseDeviceLease(DeviceLeaseOwner owner);\n"
    "    DeviceLeaseOwner deviceLeaseOwner() const;\n"
    "    bool deviceLeaseMatches(size_t index, DeviceLeaseOwner owner) const;\n"
    "    bool retuneWithLease(size_t index, double freqHz, DeviceLeaseOwner owner, bool force, std::string* error);",
)

replace_once(
    "src/DeviceManager.cpp",
    "DeviceManager::DeviceLeaseOwner DeviceManager::deviceLeaseOwner() const {\n"
    "    std::lock_guard<std::mutex> lk(leaseMutex_);\n"
    "    return deviceLeaseOwner_;\n"
    "}\n\n"
    "bool DeviceManager::acquireDeviceLease",
    "DeviceManager::DeviceLeaseOwner DeviceManager::deviceLeaseOwner() const {\n"
    "    std::lock_guard<std::mutex> lk(leaseMutex_);\n"
    "    return deviceLeaseOwner_;\n"
    "}\n\n"
    "bool DeviceManager::deviceLeaseMatches(size_t index, DeviceLeaseOwner owner) const {\n"
    "    std::lock_guard<std::mutex> lk(leaseMutex_);\n"
    "    return owner != DeviceLeaseOwner::None &&\n"
    "           deviceLeaseOwner_ == owner && deviceLeaseIndex_ == index;\n"
    "}\n\n"
    "bool DeviceManager::acquireDeviceLease",
)

replace_once(
    "src/MainWindowP25Orchestration.cpp",
    "                    if (skipPausedOneRtlControlReceiver) {\n"
    "                        mgr.setReceiverCursorToLiveEdge(i, rx);\n"
    "                        didWork = true;\n"
    "                        continue;\n"
    "                    }\n"
    "                    if (i >= mgr.getDevices().size() || !mgr.isStreaming(i)) {",
    "                    if (skipPausedOneRtlControlReceiver) {\n"
    "                        mgr.setReceiverCursorToLiveEdge(i, rx);\n"
    "                        didWork = true;\n"
    "                        continue;\n"
    "                    }\n"
    "                    // A Satcom lease means the same physical receiver has been\n"
    "                    // intentionally retuned away from ordinary listening. Pause\n"
    "                    // this logical receiver at the live edge so it cannot mix\n"
    "                    // stale/aliased PCM into the shared output. It resumes\n"
    "                    // automatically after Satcom releases the lease.\n"
    "                    if (mgr.deviceLeaseMatches(i, DeviceManager::DeviceLeaseOwner::Satcom)) {\n"
    "                        mgr.setReceiverCursorToLiveEdge(i, rx);\n"
    "                        didWork = true;\n"
    "                        continue;\n"
    "                    }\n"
    "                    if (i >= mgr.getDevices().size() || !mgr.isStreaming(i)) {",
)

replace_once(
    "tests/test_devicemanager.cpp",
    "    REQUIRE(mgr.acquireDeviceLease(0, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));\n"
    "    REQUIRE(mgr.retuneWithLease(0, 145.8e6, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));",
    "    REQUIRE(mgr.acquireDeviceLease(0, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));\n"
    "    REQUIRE(mgr.deviceLeaseMatches(0, DeviceManager::DeviceLeaseOwner::Satcom));\n"
    "    REQUIRE_FALSE(mgr.deviceLeaseMatches(0, DeviceManager::DeviceLeaseOwner::P25));\n"
    "    REQUIRE(mgr.retuneWithLease(0, 145.8e6, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));",
)

replace_once(
    "tests/test_devicemanager.cpp",
    "    mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n\n"
    "    err.clear();",
    "    mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n"
    "    REQUIRE_FALSE(mgr.deviceLeaseMatches(0, DeviceManager::DeviceLeaseOwner::Satcom));\n\n"
    "    err.clear();",
)

print("Applied Satcom active-listen DSP pause repair")
