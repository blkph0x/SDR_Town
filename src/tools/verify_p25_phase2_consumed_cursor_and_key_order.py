#!/usr/bin/env python3
"""Guard Phase 2 rolling-cursor and speech-frame ordering regressions."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="replace")
session = (root / "include" / "P25ReceiverSession.h").read_text(encoding="utf-8", errors="replace")

required = {
    "pending AMBE queue reports new frames": (
        "static bool p25QueuePhase2PendingAmbeFrame" in main
        and "++out.phase2PendingAmbeFramesQueued" in main
    ),
    "rolling cursor commits only consumed p2 windows": (
        "p25Phase2RollingDecodeWindowConsumed" in main
        and "selected voice not consumed" in main
        and "rolling.commitDecodeAbsolute(iqDecodeEndAbsolute)" in main
    ),
    "GUI worker uses consumed cursor gate": (
        "const bool consumedRollingWindow" in main
        and "p25Phase2RollingDecodeWindowConsumed(result.audio)" in main
        and "rollingIt->second.holdDecodeAbsolute(result.iqDecodeEndAbsolute)" in main
    ),
    "held rolling range blocks later queued results": (
        "heldDecodeRangeKnown" in main
        and "resultCoversHeldDecodeRange" in main
        and "P25 rolling held-result drop" in main
        and "purgeP25VoiceDecodeWorkForSession" in main
    ),
    "sequencer accepts protocol identity helper": (
        "p25Phase2VoiceFrameKeyHasProtocolIdentity(key)" in main
        and "!key.streamDibitKnown || !key.streamBurstStartDibitKnown" not in main
    ),
    "frame-key comparison prefers session codeword id": (
        "if (a.sessionCodewordIdKnown && b.sessionCodewordIdKnown)" in session
        and "a.sessionCodewordId < b.sessionCodewordId" in session
    ),
    "parser keeps pendingQueued diagnostics": "pendingQueued|pendingRel" in (
        root / "src" / "tools" / "p25_capture_audit.py"
    ).read_text(encoding="utf-8", errors="replace"),
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 consumed cursor / key-order regression FAILED: "
        + ", ".join(missing)
    )

print("P25 Phase 2 consumed cursor / key-order regression: PASS")
