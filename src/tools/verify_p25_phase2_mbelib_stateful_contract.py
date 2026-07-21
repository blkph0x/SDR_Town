#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(errors="ignore")

assert "for (int v = 0; v < 8" not in main, (
    "Phase 2 AMBE must not brute-force bit variants through the live stateful mbelib decoder"
)
assert "kP25Phase2AmbeProbeVariants" in main
assert "p25ResolvePhase2AmbeFrame" in main
assert "p25ProbeSinglePhase2AmbeVariant" in main
call_changed = main[main.index("if (callChanged) {"):main.index("state.talkgroupId = currentTg;", main.index("if (callChanged) {"))]
assert "rx.p25AmbeVoiceDecoder = P25AmbeVoiceDecoder();" in call_changed, (
    "new Phase 2 calls must reset the persistent mbelib decoder"
)
assert "rx.p25SessionState.resampler = {};" in call_changed, (
    "new Phase 2 calls must reset P25 resampler history"
)
assert "rx.p25Phase2LastGoodPcm.clear();" in call_changed, (
    "new Phase 2 calls must clear PLC tail audio"
)
assert "rx.p25Phase2PreferredAmbeVariantByVoiceIndex.fill(-1);" in call_changed, (
    "new Phase 2 calls must clear AMBE variant history"
)

loop_start = main.index("for (const auto& codeword : burst.voiceCodewords)")
loop_end = main.index("if (!drainedPendingRawVoice)", loop_start)
loop = main[loop_start:loop_end]
dedupe_pos = loop.index("p25Phase2ShouldEmitAmbeFrame")
decode_pos = loop.index("p25DecodePhase2AmbeFrameToAudio")
assert dedupe_pos < decode_pos, (
    "overlapped AMBE duplicate suppression must happen before mbelib decode"
)
assert "attemptedNewVoice = true;" in loop[:decode_pos], (
    "fresh AMBE frames should mark attemptedNewVoice before decode"
)
assert "p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant" in loop or "p25ResolvePhase2AmbeFrame" in loop, (
    "live decode path must resolve AMBE frames through variant-aware mapping"
)

print("P25 Phase 2 mbelib stateful decoder contract: PASS")
