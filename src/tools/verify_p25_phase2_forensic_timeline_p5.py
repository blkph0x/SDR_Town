#!/usr/bin/env python3
"""Regression guard for v0.2.43 overlap-boundary and burst-closure fixes."""

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

normalize = decoder_cpp.split("normalizePhase2BurstOffsets", 1)[1][:1200]
same_burst_main = main.split("p25Phase2SameVoiceBurst", 1)[1][:900]
close_burst = main.split("p25Phase2CloseActiveVoiceBurst", 1)[1][:2600]
session_same = session_h.split("p25Phase2VoiceFrameKeysSameBurst", 1)[1][:700]

required = {
    "no prefix-position duplicate erase": "cw.dibitOffset < phase2PrefixDibits" not in normalize,
    "recent burst reuse table": "RecentPhase2Burst" in decoder_h and "m_phase2RecentBursts" in decoder_h,
    "burst id matched by stream start": "streamBurstStart > seen.streamBurstStartDibit" in decoder_cpp,
    "same burst prefers stream start dibit": (
        same_burst_main.find("streamBurstStartDibitKnown") < same_burst_main.find("sessionBurstIdKnown")
    ),
    "frame keys prefer stream start dibit": (
        session_same.find("streamBurstStartDibitKnown") < session_same.find("sessionBurstIdKnown")
    ),
    "close consumes held before erasure": (
        "heldFutureFrames[seq.expectedVoiceIndex]" in close_burst and
        "p25Phase2SequencerExpireHeldFrames" not in main
    ),
    "close passes decode queue on burst switch": (
        "p25Phase2BeginVoiceBurst(rx, seq, key, &decodeQueue" in main
    ),
    "unknown burst length not default four": "key.burstVoiceCount > 0 ? key.burstVoiceCount : 4" not in main,
    "fallback-only frames rejected from sequencer": (
        "!key.streamDibitKnown || !key.streamBurstStartDibitKnown" in main.split(
            "p25Phase2SequencerProcessSpeechFrame", 1
        )[1][:1200]
    ),
    "unorderable does not fake hold count": (
        "FrameOrderResult::Unorderable" in main and
        main.split("FrameOrderResult::Unorderable", 1)[1].split("return decodeQueue", 1)[0].count("reorderHeld") == 0
    ),
    "reduced live streaming target fill": "outRate * 0.120" in main.split("pushP25LiveStreamingAudio", 1)[1][:3200],
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P5 regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P5 regression: PASS")
