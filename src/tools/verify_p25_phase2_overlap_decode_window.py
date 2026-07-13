from pathlib import Path
main = Path(__file__).resolve().parents[1] / 'main.cpp'
text = main.read_text()
assert 'static constexpr int kP25Phase2VoiceDecodeCadenceMs = 120;' in text, 'Phase 2 voice decode cadence should be bounded without starving IQ'
assert 'takeUndecoded(size_t maxSamples,' in text and 'size_t overlapSamples,' in text, 'Rolling IQ decode must accept overlapSamples'
assert 'absolute dibit de-duplication' in text, 'Overlap rationale/de-duplication comment missing'
assert 'kP25Phase2VoiceDecodeAcquireChunkSeconds = 0.420' in text, 'Acquire window should cover a full Phase 2 superframe plus context'
assert 'kP25Phase2VoiceDecodeAcquireOverlapSeconds = 0.160' in text, 'Acquire window should include controlled pre-roll overlap'
assert 'kP25Phase2VoiceDecodeMinFreshSeconds = 0.300' in text, 'Acquire decode must wait for superframe-sized fresh IQ before advancing'
assert 'kP25Phase2VoiceDecodeSustainChunkSeconds = 0.260' in text, 'Sustained clear/trusted traffic should decode smaller low-latency chunks'
assert 'kP25Phase2VoiceDecodeSustainMinFreshSeconds = 0.100' in text, 'Sustained clear/trusted traffic should not wait for a full superframe every hop'
assert 'phase2SustainDecodeWindow' in text and 'p25Phase2UseSustainDecodeWindowLocked' in text, 'Phase 2 decode path must switch between acquire and sustained windows'
assert 'hasTrustedCallState && hasStableSuperframeMask' in text, 'Sustain decode must wait for stable TDMA framing before dropping below superframe-sized acquire chunks'
assert 'armedTrafficSource && hasRecentTrafficEvidence' not in text, 'Soft recent traffic evidence must not trigger premature sustain decode'
assert text.count('takeUndecoded(maxDecodeChunk, decodeOverlap, iqStartAbsolute, iqStartAbsoluteKnown') >= 2, 'Both GUI and CLI Phase 2 paths must use overlapped decode windows'
assert 'takeUndecoded(maxDecodeChunk, iqStartAbsolute' not in text, 'Strict non-overlapped Phase 2 decode call remains'
print('P25 Phase 2 overlapped rolling decode regression: PASS')
