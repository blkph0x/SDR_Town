#!/usr/bin/env python3
"""Guard against retrying Phase 2 windows whose selected VCWs are all old."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import definition_body, orchestration_source_text
main = orchestration_source_text()

fn = definition_body(
    main,
    "bool p25Phase2RollingDecodeWindowConsumed",
    ["// Dual-slot carriers routinely carry two TGs"],
)

required = {
    "expected-only/no-target windows are consumed": (
        "Expected VCWs are a cadence diagnostic" in fn
        and "out.phase2TargetVoiceCodewords == 0" in fn
        and "out.phase2FedToMbelib == 0" in fn
        and "out.phase2PendingAmbeFramesQueued == 0" in fn
    ),
    "duplicate/context accounted counter present": "selectedVoiceAlreadyAccounted" in fn,
    "accounted includes absolute duplicates": "phase2AbsoluteDuplicateSuppressedVoiceCodewords" in fn,
    "accounted includes sequencer drops": "phase2SequencerSuppressedVoiceCodewords" in fn,
    "accounted includes context drops": "phase2ContextSuppressedVoiceCodewords" in fn,
    "accounted subtracts companion rejects": "nonSelectedRejectedVoice" in fn,
    "companion reject labels are overlapping": (
        "std::max(out.phase2OppositeVoiceCodewords, out.phase2WrongSlotVoiceCodewords)" in fn
        and "out.phase2OppositeVoiceCodewords + out.phase2WrongSlotVoiceCodewords" not in fn
    ),
    "accounted includes selected rejects": "selectedRejectedVoice" in fn,
    "accounted includes quality rejects": "phase2InputQualityRejectedVoiceCodewords" in fn,
    "does not consume queued pending audio": "out.phase2PendingAmbeFramesQueued == 0" in fn,
    "consumes windows that advanced persistent mbelib timeline": (
        "The persistent vocoder/timeline has already advanced" in fn
        and "out.phase2FedToMbelib > 0" in fn
        and "out.phase2EmittedPcmFrames > 0" in fn
        and "only pins the rolling cursor and purges newer live speech jobs" in fn
    ),
    "consumes only fully accounted selected voice": (
        "selectedVoiceAlreadyAccounted >= selectedVoiceNeedingDisposition" in fn
    ),
    "hold log records absolute duplicate evidence": (
        "dup=%7 absDup=%8 seqDrop=%9 reject=%10 wrongSlot=%11" in main
    ),
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 duplicate-only cursor consumption regression FAILED: "
        + ", ".join(missing)
    )

print("P25 Phase 2 duplicate-only cursor consumption regression: PASS")
