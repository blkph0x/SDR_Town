from pathlib import Path
main = Path(__file__).resolve().parents[1] / 'main.cpp'
text = main.read_text(errors='ignore')
fn = text[text.index('static bool p25AmbeDecodeFrameLooksUsable'):text.index('static QString p25Phase2ValidationPath')]
assert 'decoded.totalErrors > 3' in fn, 'fresh AMBE speech must reject mbelib repeat/erasure-grade frames'
assert "decoded.message.find('R')" in fn and "decoded.message.find('E')" in fn, 'fresh AMBE speech must reject mbelib repeat/erasure markers'
assert 'static bool p25DecodedAmbePcmLooksSafeForSpeaker' in fn, 'speaker gate must be separate from strict fresh-speech proof'
assert 'rms < 1.0e-6' not in fn, 'AMBE gate must keep low-energy/silence frames once they are otherwise valid'
assert 'peak > kP25DecodedAudioSafeMaxPeak' in fn and 'rms > kP25DecodedAudioSafeMaxRms' in fn, 'AMBE gate should still reject runaway PCM'
decode = text[text.index('static bool p25DecodePhase2AmbeFrameToAudio'):text.index('static bool p25ProbePhase2AmbeFrameForDiagnostics')]
assert 'const bool strictFresh = p25AmbeDecodeFrameLooksUsable(decoded);' in decode, 'hard path must still classify fresh speech quality'
assert 'const bool speakerSafe = p25DecodedAmbePcmLooksSafeForSpeaker(decoded);' in decode, 'hard path must keep a separate speaker safety gate'
assert 'mildSoftConceal' in decode, 'mild mbelib soft-concealment may still emit for cadence'
assert 'erasureOrMute' in decode, 'erasure/mute codec garbage must not emit as speaker PCM'
assert 'nearSilentCodecPcm' in decode and 'emitAsSpeaker' in decode, 'near-silent erasure/repeat PCM must not count as accepted speaker audio'
assert 'frame.accepted = emitAsSpeaker;' in decode, 'validation accepted flag should reflect speaker emission, not fresh proof'
assert 'if (!strictFresh)' in decode and '++out.phase2ConcealmentFrames;' in decode, 'non-fresh codec PCM must be counted as concealment quality debt'
may = text[text.index('static bool p25VoiceBlockMayEmitAudio'):text.index('static P25VoiceAudioBlock applyP25Phase2SecurityAudioGate')]
assert 'out.phase2AmbeRejected ||' not in may, 'mixed window rejected candidates must not mute accepted audio'
accepted = text[text.index('if (acceptedReleaseVoice &&'):text.index('} else if ((sdrtrunkLateEntryVoiceRelease', text.index('if (acceptedReleaseVoice &&'))]
assert 'out.phase2AmbeRejected = false;' in accepted, 'accepted audio should clear AMBE-rejected diagnostic for the emitted block'
print('P25 Phase 2 strict fresh-AMBE gate/block emit regression: PASS')
