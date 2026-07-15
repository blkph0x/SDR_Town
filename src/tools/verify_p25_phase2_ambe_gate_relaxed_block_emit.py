from pathlib import Path
main = Path(__file__).resolve().parents[1] / 'main.cpp'
text = main.read_text(errors='ignore')
fn = text[text.index('static bool p25AmbeDecodeFrameLooksUsable'):text.index('static QString p25Phase2ValidationPath')]
assert 'decoded.totalErrors <=' not in fn, 'AMBE gate must not use a hard total-error threshold after mbelib/JMBE-style concealment'
assert 'rms < 1.0e-6' not in fn, 'AMBE gate must keep low-energy/silence frames for continuous 20 ms cadence'
assert 'peak > kP25DecodedAudioSafeMaxPeak' in fn and 'rms > kP25DecodedAudioSafeMaxRms' in fn, 'AMBE gate should reject only runaway PCM'
may = text[text.index('static bool p25VoiceBlockMayEmitAudio'):text.index('static P25VoiceAudioBlock applyP25Phase2SecurityAudioGate')]
assert 'out.phase2AmbeRejected ||' not in may, 'mixed window rejected candidates must not mute accepted audio'
accepted = text[text.index('if (acceptedReleaseVoice &&'):text.index('} else if ((sdrtrunkLateEntryVoiceRelease', text.index('if (acceptedReleaseVoice &&'))]
assert 'out.phase2AmbeRejected = false;' in accepted, 'accepted audio should clear AMBE-rejected diagnostic for the emitted block'
print('P25 Phase 2 relaxed AMBE gate/block emit regression: PASS')
