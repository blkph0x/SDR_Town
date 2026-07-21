#!/usr/bin/env python3
"""Regression guard for forensic-audit P0 timeline-safety fixes."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="ignore")
decoder_h = (root / ".." / "include" / "P25LiveDecoder.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)
decoder_cpp = (root / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
session_h = (root / ".." / "include" / "P25ReceiverSession.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)

completed_section = main.split("p25VoiceCompletedResults", 1)[1]
required = {
    "lossless publish queue": "pendingVoicePublishResults" in main,
    "publish returns bool": "bool publishP25VoiceDecodeResult(" in main,
    "publication lock miss counter": "p25VoicePublicationLockMisses" in main,
    "no completed-result eviction": "p25VoiceDroppedResults.fetch_add" not in completed_section,
    "worker blocks on full completed queue": "p25VoiceCompletedResults.size() < kP25VoiceDecodeMaxCompletedResults" in main,
    "stream dibit on codeword": "streamDibitKnown" in decoder_h and "codeword.streamDibit = streamDibit" in decoder_cpp,
    "frame key uses stream dibit": "key.streamDibitKnown = cw.streamDibitKnown" in main,
    "first-frame erasure timeline": "Every accepted AMBE feed position must occupy one 20 ms slot" in main,
    "metadata excluded from audio key": "NAC/WACN/system are late-arriving metadata" in session_h,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P0 timeline regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P0 timeline regression: PASS")
