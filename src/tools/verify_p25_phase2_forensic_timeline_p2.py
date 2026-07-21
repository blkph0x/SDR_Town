#!/usr/bin/env python3
"""Regression guard for forensic-audit P2 continuous FIFO, CQPSK freeze, slot probe."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="ignore")
decoder_h = (root / ".." / "include" / "P25LiveDecoder.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)
decoder_cpp = (root / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
receiver_h = (root / ".." / "include" / "Receiver.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)

slot_probe_fn = main.split("static bool applyP25Phase2SlotProbeLocked", 1)[1].split("static void pushAudioFrames", 1)[0]

required = {
    "durable cursor commit on result ingest": "commitDecodeAbsolute(result.iqDecodeEndAbsolute)" in main.split(
        "for (auto& result : takeP25VoiceDecodeResults())", 1
    )[1].split("pendingVoicePublishResults.push_back", 1)[0],
    "speaker pipeline depth 3": "kP25VoiceDecodeMaxPendingJobsSpeaker = 3" in main,
    "cqpsk discrete freeze api": "setCqpskDiscreteFrozen" in decoder_h and "m_cqpskDiscreteFrozen" in decoder_cpp,
    "cqpsk hypothesis blocked counter": "m_cqpskDiscreteChangesBlocked" in decoder_cpp and "p25DiagCqpskHypothesisChanges" in receiver_h,
    "worker applies cqpsk freeze": "p25Phase2ShouldFreezeCqpskDiscrete" in main,
    "non-destructive slot probe keeps decoder": "P25LiveDecoder(p25VoiceDecoderConfigForReceiver" not in slot_probe_fn,
    "slot probe retargets preferred slot": "setPhase2PreferredTdmaSlot(true, requested)" in slot_probe_fn,
    "slot probe continues decode": 'return false;\n                };' in main.split("slot-probe-applied-before-decode", 1)[1][:400],
    "playout bridge frame cap": "consecutivePlayoutBridgeFrames >= 20" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P2 regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P2 regression: PASS")
