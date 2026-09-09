from pathlib import Path
from p25_orchestration_sources import orchestration_source_text
root = Path(__file__).resolve().parents[2]
main = orchestration_source_text()
text = main
assert 'takeUndecoded(size_t maxSamples,' in text and 'size_t overlapSamples,' in text, 'Rolling IQ decode must accept overlapSamples'
assert 'absolute dibit de-duplication' in text, 'Overlap rationale/de-duplication comment missing'
assert 'kP25Phase2VoiceDecodeWindowSeconds = 0.720' in text, 'Phase 2 rolling window should cover two full superframes for MAC/ESS recovery'
assert 'kP25Phase2VoiceDecodeFirstColdEyeSeconds = 0.720' in text, 'First traffic eye should cover two full Phase 2 superframes'
assert 'kP25Phase2VoiceDecodeAcquireChunkSeconds = 0.050' in text, 'Post-eye acquire should use short bounded fresh chunks'
assert 'kP25Phase2VoiceDecodeAcquireOverlapSeconds = 0.160' in text, 'Post-cold acquire must keep bounded pre-roll instead of replaying a full two-superframe window'
assert 'kP25Phase2VoiceDecodeMinFreshSeconds = 0.020' in text, 'Post-eye acquire should not stall waiting for a full superframe'
assert 'kP25Phase2VoiceDecodeFirstColdEyeSeconds = 0.720' in text, 'First cold eye must wait for a full two-superframe acquire window'
assert 'kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds = 0.160' in text, 'Unacquired Phase 2 must advance in full-window acquisition slices'
assert 'kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds' in text, 'Unacquired Phase 2 must keep enough overlap to preserve full acquisition context'
assert 'if (chunkPlan.treatAsContextFreeFresh)' in text, 'Only explicit context-free chunk plans should erase context accounting'
assert 'plan.overlapSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds;' in text, 'Unacquired acquisition should preserve overlap context after the first cold eye'
assert 'kP25Phase2VoiceDecodeSustainChunkSeconds = 0.080' in text, 'Sustain should advance on bounded near-live IQ slices'
assert 'kP25Phase2VoiceDecodeSustainMinFreshSeconds = 0.040' in text, 'Sustain should not wait for a full superframe once acquired'
assert 'kP25Phase2VoiceDecodeSustainOverlapSeconds = 0.280' in text, 'Locked sustain should keep one 360 ms superframe of pre-roll (DEC-0009)'
assert 'kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080' in text, 'Speaker sustain must not replay cold-eye sized fresh IQ'
assert 'kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.040' in text, 'Speaker sustain should advance on frame-pair fresh IQ'
assert 'kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.280' in text, 'Speaker sustain should preserve one 360 ms superframe of context (DEC-0009)'
assert 'phase2SustainDecodeWindow' in text and 'p25Phase2UseSustainDecodeWindowLocked' in text, 'Phase 2 decode path must switch between acquire and sustained windows'
assert 'hasTrustedCallState && (hasStableSuperframeMask' in text, 'Sustain decode must wait for trusted target call state before dropping below acquisition context'
assert 'p25Phase2EstablishedClearVoiceStreamingLocked' in text, 'Established clear voice must stay on the low-latency streaming path'
assert 'currentStreamingEye' in text and 'return false;' in text[text.find('currentStreamingEye'):text.find('static bool p25Phase2UseSustainDecodeWindowLocked')], 'Wide reacquire must not override streaming while Phase 2 bursts/CQPSK lock are present'
decoder = (root / 'src' / 'P25LiveDecoder.cpp').read_text(encoding='utf-8', errors='replace')
assert 'phase2SyncTailDibits = m_config.realtimeVoiceSearch' in decoder, 'Realtime Phase 2 must trim the internal dibit tail after acquisition'
assert 'Phase2BurstDibits * 4' in decoder and 'Phase2BurstDibits * 12' in decoder, 'Realtime keeps a 4-burst tail while forensic/cold decode keeps full-superframe context'
assert 'armedTrafficSource && hasRecentTrafficEvidence' not in text, 'Soft recent traffic evidence must not trigger premature sustain decode'
assert text.count('takeUndecoded(maxDecodeChunk, decodeOverlap, iqStartAbsolute, iqStartAbsoluteKnown') >= 2, 'Both GUI and CLI Phase 2 paths must use overlapped decode windows'
assert 'takeUndecoded(maxDecodeChunk, iqStartAbsolute' not in text, 'Strict non-overlapped Phase 2 decode call remains'
assert 'maxOverlapSamples = (rollingWindow > maxDecodeChunk)' in text, 'Overlap must be capped by rolling-window budget, not by half the fresh chunk'
assert 'maxDecodeChunk / 2' not in text[text.find('const double maxDecodeChunkSeconds'):text.rfind('const size_t minDecodeFreshNominal')], 'Phase 2 overlap is still clamped to half the fresh chunk'
print('P25 Phase 2 overlapped rolling decode regression: PASS')

