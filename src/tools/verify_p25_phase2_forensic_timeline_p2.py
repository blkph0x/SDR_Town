#!/usr/bin/env python3
"""Regression guard for forensic-audit P2 continuous FIFO, CQPSK freeze, slot probe."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
from p25_orchestration_sources import definition_body, orchestration_source_text
main = orchestration_source_text()
decoder_h = (root / ".." / "include" / "P25LiveDecoder.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)
decoder_cpp = (root / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
receiver_h = (root / ".." / "include" / "Receiver.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)

slot_probe_fn = definition_body(
    main,
    "bool applyP25Phase2SlotProbeLocked",
    ["void pushAudioFrames"],
)

required = {
    "single cursor settlement after publish": "outcome == P25VoicePublishOutcome::Published" in main,
    "speaker pipeline is single-flight": "kP25VoiceDecodeMaxPendingJobsSpeaker = 1" in main
    and "inFlightJobs < p25VoiceDecodeMaxPendingJobsNow" in main,
    "cqpsk discrete freeze api": "setCqpskDiscreteFrozen" in decoder_h and "m_cqpskDiscreteFrozen" in decoder_cpp,
    "cqpsk hypothesis blocked counter": "m_cqpskDiscreteChangesBlocked" in decoder_cpp and "p25DiagCqpskHypothesisChanges" in receiver_h,
    "worker applies cqpsk freeze": "p25Phase2ShouldFreezeCqpskDiscrete" in main,
    "non-destructive slot probe keeps decoder": "P25LiveDecoder(p25VoiceDecoderConfigForReceiver" not in slot_probe_fn,
    "slot probe retargets preferred slot": "setPhase2PreferredTdmaSlot(true, requested)" in slot_probe_fn,
    "slot probe continues decode": 'return false;\n                };' in main.split("slot-probe-applied-before-decode", 1)[1][:400],
    "playout bridge frame cap": "consecutivePlayoutBridgeFrames >= 225" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P2 regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P2 regression: PASS")
