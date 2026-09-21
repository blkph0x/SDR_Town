from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: {label}: expected one match, found {count}")
    p.write_text(text.replace(old, new), encoding="utf-8", newline="\n")
    print(f"patched {label}")


replace_once(
    "src/DeviceManager.cpp",
    '''bool DeviceManager::retuneWithLease(size_t index, double freqHz, DeviceLeaseOwner owner, bool force,
                                    std::string* error) {
    if (!acquireDeviceLease(index, owner, force, error)) return false;
    setCenterFreq(index, freqHz);
    return true;
}''',
    '''bool DeviceManager::retuneWithLease(size_t index, double freqHz, DeviceLeaseOwner owner, bool force,
                                    std::string* error) {
    if (!acquireDeviceLease(index, owner, force, error)) return false;
    const uint64_t requestSeq = setCenterFreq(index, freqHz);
    if (requestSeq == 0) {
        if (error) {
            *error = "device index " + std::to_string(index) +
                     " is unavailable; rescan devices and select a valid receiver";
        }
        return false;
    }
    return true;
}''',
    "retune invalid-device result",
)

replace_once(
    "src/SatcomScannerEngine.cpp",
    '''    demodResetRequested_ = true;

    // Enter the locked pass path before the worker starts. This avoids a race
    // where the old UI started a band scan first and only armed the pass later.
    if (!run_.load(std::memory_order_acquire) && !start(force)) {''',
    '''    demodResetRequested_ = true;

    // Always issue the base-frequency tune, even when Doppler auto-track is
    // disabled. Previously tickPassTrack() returned early in that mode, leaving
    // an apparently armed pass on the receiver's old frequency.
    if (!deviceManager.retuneWithLease(dev, snap.armed.freqHz,
                                       DeviceManager::DeviceLeaseOwner::Satcom,
                                       true, &err)) {
        const std::string tuneError = err.empty() ? "Could not tune pass receiver" : err;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            passTrackActive_ = false;
            armedRole_.clear();
            state_ = run_.load(std::memory_order_acquire)
                ? SatcomScannerState::Scanning
                : SatcomScannerState::Idle;
            lastStatus_ = tuneError;
        }
        SatPassPlanner::instance().disarm();
        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        if (error) *error = tuneError;
        return false;
    }

    // Enter the locked pass path before the worker starts. This avoids a race
    // where the old UI started a band scan first and only armed the pass later.
    if (!run_.load(std::memory_order_acquire) && !start(force)) {''',
    "unconditional initial pass retune",
)

replace_once(
    "tests/test_devicemanager.cpp",
    '''    REQUIRE(mgr.acquireDeviceLease(0, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));
    mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
    mgr.stopStreaming(0);''',
    '''    REQUIRE(mgr.acquireDeviceLease(0, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));
    mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);

    err.clear();
    const size_t invalidIndex = mgr.getDevices().size() + 100;
    REQUIRE_FALSE(mgr.retuneWithLease(invalidIndex, 145.8e6,
                                      DeviceManager::DeviceLeaseOwner::Satcom,
                                      true, &err));
    REQUIRE_FALSE(err.empty());
    mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
    mgr.stopStreaming(0);''',
    "invalid leased retune regression test",
)

for temporary in (
    ".github/workflows/apply-arm-initial-tune-fix.yml",
    "scripts/apply_arm_initial_tune_fix.py",
):
    p = Path(temporary)
    if p.exists():
        p.unlink()
