#!/usr/bin/env python3
from pathlib import Path
main = Path(__file__).resolve().parents[1] / "main.cpp"
s = main.read_text()
assert "uint32_t talkgroupId = 0;" in s, "AMBE emit de-dupe state must track talkgroup/call boundary"
assert "state.slotKnown != currentSlotKnown" in s and "state.slot != currentSlot" in s, "AMBE emit de-dupe must reset on slot handoff"
assert "std::abs(state.voiceFreqHz - currentVoiceFreqHz) > 25.0" in s, "AMBE emit de-dupe must reset on frequency handoff"
assert "rx.freqHz" in s, "AMBE emit cursor should use receiver tuned voice frequency"
assert "if (codeword.duplicateInSession) {\n                skippedDuplicateVoice = true;\n            }" in s, "session duplicate marker should be diagnostic only, not an audio hard-skip"
assert "duplicate marker diagnostic-only" in s, "patch comment should document why duplicateInSession is diagnostic-only"
print("P25 Phase 2 sdrtrunk-like audio emit cursor regression: PASS")
