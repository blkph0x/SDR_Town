#!/usr/bin/env python3
"""Regression guard for v0.2.42 sequencer burst identity and reorder buffer."""

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

required = {
    "decoder sessionBurstId on codeword": "sessionBurstIdKnown" in decoder_h and "m_phase2NextSessionBurstId" in decoder_cpp,
    "frame key carries sessionBurstId": "key.sessionBurstId = cw.sessionBurstId" in main,
    "same burst uses sessionBurstId": "activeSessionBurstId" in session_h and "key.sessionBurstId == seq.activeSessionBurstId" in main,
    "removed streamDibit minus voiceIndex burst id": "p25Phase2StableBurstId" not in main,
    "full-frame reorder buffer": "heldFutureFrames" in session_h and "P25Phase2SequencerSpeechInput" in session_h,
    "sequencer process returns decode queue": "p25Phase2SequencerProcessSpeechFrame" in main,
    "hold when voiceIndex exceeds expected": "key.voiceIndex > seq.expectedVoiceIndex" in main.split(
        "p25Phase2SequencerProcessSpeechFrame", 1
    )[1],
    "flush held releases frames": "p25Phase2SequencerFlushHeldIntoQueue" in main,
    "pending path uses same sequencer pcm sink": "p25Phase2SequencerProcessSpeechFrame(rx, seqInput, &out, outputRateHz)" in main,
    "pending sort partitions coordinate classes": "pendingFrameRank" in main,
    "receiver gone rolls back cursor": "P25VoicePublishOutcome::ReceiverGone" in main.split("rollbackSubmittedDecode", 1)[1],
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P4 regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P4 regression: PASS")
