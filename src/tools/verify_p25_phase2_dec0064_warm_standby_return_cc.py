#!/usr/bin/env python3
"""DEC-0064: warm-standby must not feed CC validation; return resets validation on real CC RF."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "MainWindow.cpp").read_text(encoding="utf-8", errors="ignore")

gate = ""
if "warmStandbyTunerAwayFromCc" in main:
    gate = main.split("warmStandbyTunerAwayFromCc", 1)[1][:900]

note = ""
if "void MainWindow::noteP25ControlMonitorDecodeWindow" in main:
    note = main.split("void MainWindow::noteP25ControlMonitorDecodeWindow", 1)[1][:1200]

expire = ""
if "auto expireP25WarmStandbyIfNeeded" in main:
    expire = main.split("auto expireP25WarmStandbyIfNeeded", 1)[1][:2200]

arm = ""
if "bool MainWindow::armGuiRuntimeP25Control" in main:
    arm = main.split("bool MainWindow::armGuiRuntimeP25Control", 1)[1][:4200]

ret = ""
if "onSuccessfulReturnToControlChannel" in main:
    ret = main.split("onSuccessfulReturnToControlChannel", 1)[1][:3500]

required = {
    "CC decode paused during warm-standby": (
        "warmStandbyTunerAwayFromCc" in main
        and "!warmStandbyTunerAwayFromCc" in gate
        and "decodeControlFromThisDevice" in gate
    ),
    "validation note ignores warm-standby windows": (
        "p25AutoFollowWarmStandbyUntilMs > nowMs) return;" in note.replace("\n", " ").replace(" ", "")
        or "p25AutoFollowWarmStandbyUntilMs > nowMs) return;" in note
        or ("p25AutoFollowWarmStandbyUntilMs > nowMs" in note and "return;" in note[:400])
    ),
    "warm-standby expire retunes + resets validation": (
        "warm-standby expired" in expire
        and "resetP25ControlMonitorValidation(ccHz, \"warm-standby expired\")" in expire
        and "p25AutoFollowReturnControlFreqHz" in expire
    ),
    "return-to-CC resets validation when RF on CC": (
        "return-to-control immediate" in main
        and "return-to-control follow" in main
        and "resetP25ControlMonitorValidation" in ret
    ),
    "failed retune does not claim monitored CC": (
        "not claiming CC monitor is active" in main
        and "p25MonitoredControlFreqHz = 0.0;"
        in main.split("Do not claim CC monitor while RF is still on voice.", 1)[1][:400]
    ),
    "idle arm requires RF on CC (not warm-standby lie)": (
        "rfOnControlChannel" in arm
        and "warmStandbyHold" in arm
        and "sameControlChannel && !followLive && rfOnControlChannel" in arm
    ),
    "status exposes warmStandbyActive": (
        'p25.insert("warmStandbyActive", warmStandbyActive)' in main
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 DEC-0064 warm-standby return-to-CC regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 DEC-0064 warm-standby return-to-CC regression: PASS")
