#!/usr/bin/env python3
"""Guard: DEC-0023 rolling soft-trim protects 280 ms DEC-0009 overlap."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
marker = "DEC-0023 / capture 20260908_095936"
if marker not in main:
    raise SystemExit("DEC-0023 regression failed: missing soft-trim comment")
region = main.split(marker, 1)[1][:1200]
checks = {
    "protect 573440 samples": "kProtectedOverlapSamples = 573440" in region,
    "cites 160 ms collapse": "160 ms" in region,
    "active rolling 4s": "kP25Phase2VoiceDecodeActiveRollingSeconds = 4.000" in main,
    "hard-cap avoids cursor jump": "jump lastDecodeAbsolute" in main
    and "emergencyCap" in main,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0023 regression failed: " + ", ".join(failed))
print("DEC-0023 rolling protect-overlap regression: PASS")
