#!/usr/bin/env python3
"""DEC-0059: companion ESS must not force clear-follow ReturnEncrypted."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
voice = (root / "src" / "P25VoiceDecode.cpp").read_text(encoding="utf-8", errors="ignore")
traffic = (root / "src" / "P25TrafficChannelProcessor.cpp").read_text(encoding="utf-8", errors="ignore")
main = (root / "src" / "MainWindow.cpp").read_text(encoding="utf-8", errors="ignore")
test_traffic = (root / "tests" / "test_p25traffic_processor.cpp").read_text(
    encoding="utf-8", errors="ignore"
)

required = {
    "traffic this-burst ESS only": (
        "essObservedThisBurst" in traffic
        and "trafficSecurityObservedThisBurst" in traffic
        and "DEC-0059" in traffic
    ),
    "recent ESS clear supersedes sticky encrypted": (
        "p25Phase2RecentTargetEssEncrypted = false" in voice
        and "targetEssKnown && !targetEssEncrypted" in voice
        and "DEC-0059" in voice
    ),
    "target ESS encrypted only from observed paint": (
        "burst.essObservedThisBurst && burst.essKnown" in voice
        and "never promote encrypted from non-observed paint" in voice
    ),
    "pending refuse opposite-only drain": (
        "never drain pending into an opposite-only window" in voice
        and "phase2OppositeVoiceCodewords > 0" in voice
    ),
    "MainWindow follow ESS not OR traffic encrypted": (
        "phase2TargetEssEncrypted" in main
        and "Do not OR trafficStatus.diag.encrypted" in main
        and "trafficStatus.present && trafficStatus.diag.encrypted" not in main
    ),
    "unit: sticky non-observed encrypted ignored": (
        "ignores sticky non-observed encrypted ESS paint" in test_traffic
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit("DEC-0059 regression failed: " + ", ".join(failed))
print("P25 Phase 2 DEC-0059 companion ESS / ReturnEncrypted: PASS")
