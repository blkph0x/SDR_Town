#!/usr/bin/env python3
"""DEC-0063: same-CC P25 arm stays idempotent while follow is live; analog tune refuses."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "MainWindow.cpp").read_text(encoding="utf-8", errors="ignore")

arm = ""
if "bool MainWindow::armGuiRuntimeP25Control" in main:
    arm = main.split("bool MainWindow::armGuiRuntimeP25Control", 1)[1][:4500]

tune = ""
if "QJsonObject MainWindow::applySdrTownControlTune" in main:
    tune = main.split("QJsonObject MainWindow::applySdrTownControlTune", 1)[1][:5000]

status = ""
if "QJsonObject MainWindow::sdrTownControlStatusSnapshot" in main:
    status = main.split("QJsonObject MainWindow::sdrTownControlStatusSnapshot", 1)[1][:4000]

required = {
    "arm early-outs when same CC + follow live": (
        "p25-control-arm-idempotent" in arm
        and "keeping active follow" in arm
        and "sameControlChannel && followLive" in arm
        and "startGuiRuntimeDeviceAt(ccHz, true)" in arm
        and arm.find("p25-control-arm-idempotent") < arm.find("startGuiRuntimeDeviceAt(ccHz, true)")
    ),
    "arm does not clear voice on follow idempotent path": (
        "p25AutoFollowVoiceFreqHz = 0.0" in arm
        and arm.find("p25-control-arm-idempotent") < arm.find("p25AutoFollowVoiceFreqHz = 0.0")
        and "p25FollowEnabled = false" in arm
        and arm.find("return true;") < arm.find("p25FollowEnabled = false")
    ),
    "idle arm requires RF physically on CC": (
        "rfOnControlChannel" in arm
        and "sameControlChannel && !followLive && rfOnControlChannel" in arm
    ),
    "analog tune refuses while follow/warm-standby live unless force": (
        "sdr-town-control-tune-refused-follow" in tune
        and "p25AutoFollowWarmStandbyUntilMs" in tune
        and 'body.value("force").toBool(false)' in tune
        and '"status", 409' in tune
    ),
    "status exposes voiceFrequencyHz for bridge prove": (
        'p25.insert("voiceFrequencyHz", p25AutoFollowVoiceFreqHz)' in status
        and 'p25.insert("followEnabled", p25FollowEnabled)' in status
        and 'p25.insert("warmStandbyActive", warmStandbyActive)' in status
    ),
    "control tune path logs requests": (
        "sdr-town-control-tune" in tune
        and "followLive" in tune
    ),
    "bridge P25 control persists autoFollow": (
        "guiRuntimeConfig.autoFollow = autoFollow;" in tune
        and "oldAutoFollow" not in tune.split("if (p25Control)", 1)[1][:800]
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 DEC-0063 idempotent control-arm regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 DEC-0063 idempotent control-arm regression: PASS")
