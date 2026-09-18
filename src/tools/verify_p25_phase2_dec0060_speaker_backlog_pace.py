#!/usr/bin/env python3
"""DEC-0060 sizes superseded by DEC-0061 — keep script as a redirect guard."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
timing_h = (root / "include" / "P25VoiceTiming.h").read_text(encoding="utf-8", errors="ignore")

# Thin 80 ms speaker catch-up overlap was the 0060 experiment; 0061 restored 280.
if "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds = 0.080" in timing_h:
    raise SystemExit(
        "DEC-0060 thin overlap still present — superseded by DEC-0061 (240+280)"
    )
if "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds = 0.280" not in timing_h:
    raise SystemExit("DEC-0060→0061 guard failed: speaker catch-up overlap not 280 ms")
if "DEC-0061" not in timing_h:
    raise SystemExit("DEC-0060→0061 guard failed: missing DEC-0061 citation")
print("P25 Phase 2 DEC-0060 superseded by DEC-0061 (overlap restored): PASS")
