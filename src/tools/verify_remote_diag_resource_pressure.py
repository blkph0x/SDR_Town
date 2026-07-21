"""Verify resource-pressure diagnostics and IQ capture low-disk hardening."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8", errors="replace")
SERVER = (ROOT / "src" / "tools" / "remote_diag_server.py").read_text(encoding="utf-8", errors="replace")

checks = {
    "manual captures use free-space preflight": (
        "captureStorageReadyForStart(root, session.sampleRateHz, plannedDurationMs" in MAIN
        and "kManualCapturePreflightSeconds" in MAIN
        and "captureStartRequiredBytes" in MAIN
    ),
    "capture has runtime low-storage auto-stop": (
        "capture_auto_stop_low_storage" in MAIN
        and "storageStopRequested" in MAIN
        and "IQ capture auto-stop requested" in MAIN
        and "low_capture_storage_free_space" in MAIN
    ),
    "capture artifacts retain storage-stop evidence": (
        '"storage_stop_requested"' in MAIN
        and '"storage_stop_available_bytes"' in MAIN
        and "# storage_stop_requested=" in MAIN
    ),
    "resource monitor sends pressure reasons": (
        "diagnosticsSystemHealthPressureReasons" in MAIN
        and '"pressureReasons"' in MAIN
        and '"pressureSummary"' in MAIN
        and "low_appdata_free_space" in MAIN
    ),
    "server fingerprints resource pressure by reason": (
        'event_type == "app.performance.resource_pressure"' in SERVER
        and "pressureSummary" in SERVER
        and "pressureReasons" in SERVER
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("Remote diagnostics resource-pressure regression failed: " + ", ".join(failed))

print("Remote diagnostics resource-pressure regression: PASS")
