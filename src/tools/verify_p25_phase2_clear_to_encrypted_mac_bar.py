#!/usr/bin/env python3
"""Guard: DEC-0025 Clear→Encrypted needs MAC/PTT bar like trustedEncryptedEss."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
marker = "DEC-0025 / capture 20260908_103955"
if marker not in main:
    raise SystemExit("DEC-0025 regression failed: missing security-gate comment")
region = main.split(marker, 1)[1][:900]
checks = {
    "thisWindowEssEncryptedClaim": "thisWindowEssEncryptedClaim" in region,
    "requires MAC or PTT": (
        "phase2TargetMacCrcValid" in region
        and "phase2MacCrcValid > 0" in region
        and "phase2TargetSecurityStateFromPtt" in region
    ),
    "ReturnEncrypted logs reason": "reason={trustedEss=" in main,
    "follow diag ESS enc needs MAC when latched clear": (
        "do not promote grantEncrypted from" in main
        and "phase2MacCrcValid > 0" in main
    ),
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0025 regression failed: " + ", ".join(failed))
print("DEC-0025 Clear-to-Encrypted MAC bar regression: PASS")
