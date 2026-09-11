#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
receiver = (root / "src" / "Receiver.cpp").read_text(encoding="utf-8", errors="replace")
from p25_orchestration_sources import definition_body, orchestration_source_text
main = orchestration_source_text()

begin = receiver.split("void p25Phase2BeginNewPtt", 1)[1].split("void p25Phase2RefreshGrantEpoch", 1)[0]
commit = definition_body(
    main,
    "void p25CommitPhase2TrafficMetadataFollow",
    ["bool p25Phase2ShouldFreezeCqpskDiscrete"],
)

checks = {
    "new ptt clears pending raw ambe": "rx.p25SessionState.pendingAudio = {};" in begin,
    "new ptt clears ambe dedupe": "rx.p25SessionState.ambeDedupe = {};" in begin,
    "new ptt clears frame sequencer": "rx.p25SessionState.frameSequencer = {};" in begin,
    "new ptt clears audio tail": "rx.p25SessionState.audioTail = {};" in begin,
    "new ptt clears sustain": "rx.p25SessionState.sustain = {};" in begin,
    "new ptt clears security latch": "rx.p25SessionState.callSecurityLatch = P25CallSecurityLatch::Unknown;" in begin,
    "same-call metadata treats source as soft control-plane state": (
        "sameAllocationControlSourceChange" in commit and
        "rx.p25VoiceSourceId != followTg.lastSourceId" in commit and
        "(!sameAllocationControlSourceChange || rx.p25VoiceSourceId == 0)" in commit
    ),
    "same-call metadata checks slot": "slotCompatible" in commit and "rx.p25VoiceTdmaSlot & 0x01u" in commit,
    "same-call does not require source compatible": "sourceCompatible &&" not in commit,
    "same-call requires slot compatible": "slotCompatible &&" in commit,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("P25 Phase 2 call-boundary latch reset regression failed: " + ", ".join(failed))
print("P25 Phase 2 call-boundary latch reset regression: PASS")
