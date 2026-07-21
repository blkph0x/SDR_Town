#!/usr/bin/env python3
"""Regression guard for forensic-audit P1 sequencer and PCM lifecycle fixes."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="ignore")
session_h = (root / ".." / "include" / "P25ReceiverSession.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)
receiver_h = (root / ".." / "include" / "Receiver.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)
receiver_cpp = (root / "Receiver.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "voice2 voice4 burst tracking": "activeBurstVoiceCount" in session_h and "expectedVoiceIndex" in session_h,
    "sequencer gap silence ordinals": "gapSilenceOrdinals" in session_h and "p25Phase2InsertSequencerGapSilence" in main,
    "burst voice count on frame key": "burstVoiceCount" in session_h and "key.burstVoiceCount" in main,
    "ptt generation on receiver": "p25PttGeneration" in receiver_h,
    "begin new ptt helper": "p25Phase2BeginNewPtt" in receiver_cpp,
    "grant refresh without session reset": "p25Phase2RefreshGrantEpoch" in main,
    "tagged speaker pending queue": "P25Phase2SpeakerPendingQueue" in session_h,
    "central pending clear helper": "p25Phase2ClearSpeakerPendingQueue" in main,
    "bind pending to call session": "p25Phase2BindSpeakerPendingToCall" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P1 regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P1 regression: PASS")
