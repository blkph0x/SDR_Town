#!/usr/bin/env python3
"""DEC-0065: return-to-CC must retune when primary RF is off the control channel."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "MainWindow.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "return forces retune when RF away from CC": (
        "mustRetunePrimaryHome" in main
        and "rfAwayFromControl" in main
        and "forcing retune home despite retunesPrimary=false" in main
        and "trafficRetunedPrimary || rfAwayFromControl" in main
    ),
    "without-RF-retune only after mustRetune gate": (
        "continuing muted control-channel monitor" in main
        and main.find("mustRetunePrimaryHome")
        < main.find("continuing muted control-channel monitor")
    ),
    "start latches RetunedPrimary when LO leaves CC": (
        "primaryLoMovedAwayFromCc" in main
        and "source.retunesPrimary || primaryLoMovedAwayFromCc" in main
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 DEC-0065 RF-home return regression failed: " + ", ".join(failed)
    )
print("P25 Phase 2 DEC-0065 RF-home return regression: PASS")
