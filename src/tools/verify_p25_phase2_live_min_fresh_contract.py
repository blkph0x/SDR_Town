#!/usr/bin/env python3
from pathlib import Path


root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
audit = (root / "src" / "tools" / "p25_capture_audit.py").read_text(encoding="utf-8")

checks = {
    "RollingIqWindow exposes explicit partial-fresh control":
        "bool allowPartialFresh = true" in main,
    "partial-fresh softening is gated by allowPartialFresh":
        "allowPartialFresh && effectiveMinFresh > 0" in main,
    "GUI live Phase 2 decode rejects partial fresh chunks":
        "&iqDecodeEndAbsolute, &iqDecodeEndAbsoluteKnown, false" in main,
    "CLI live Phase 2 decode rejects partial fresh chunks":
        main.count("&iqDecodeEndAbsolute, &iqDecodeEndAbsoluteKnown, false") >= 2,
    "capture audit flags sub-frame scheduler submits":
        "SCHEDULER_SUBMITTED_RE" in audit and
        "scheduler_subframe_fresh_submit" in audit and
        "phase2_scheduler_subframe_fresh" in audit,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "verify_p25_phase2_live_min_fresh_contract failed:\n- " +
        "\n- ".join(failed)
    )

print("verify_p25_phase2_live_min_fresh_contract: PASS")
