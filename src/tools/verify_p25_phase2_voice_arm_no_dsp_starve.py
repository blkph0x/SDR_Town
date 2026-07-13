#!/usr/bin/env python3
"""Regression guard for the P25 Phase 2 voice-arm path.

The voice arm path used to either hard-fail on dspMutex try-lock or publish a
fresh decoder while the DSP worker could still be inside the old object.  The
current parity rule is stricter: block on dspMutex for the handoff and never use
try-lock for receiver/state/decoder replacement.
"""
from pathlib import Path
src = Path(__file__).resolve().parents[1] / "main.cpp"
text = src.read_text(encoding="utf-8")
start = text.index('auto armP25VoiceFollowState = [this]')
end = text.index('auto scheduleP25VoiceFollowArm', start)
arm = text[start:end]
assert 'std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex);' in arm
assert 'rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfig(rx.p25VoicePhase2));' in arm
assert 'rx.dspMutex, std::try_to_lock' not in arm
assert 'own dspMutex before decoder replacement' in arm
assert 'if (attempt >= 600)' in text
assert 'enum class P25VoiceDecodeProfile' in text
voice_cfg = text[text.index('static P25LiveDecoderConfig p25VoiceDecoderConfig'):text.index('static int p25CliDecodeScore')]
assert 'P25VoiceDecodeProfile::Realtime' in voice_cfg
assert 'cfg.realtimeVoiceSearch = profile == P25VoiceDecodeProfile::Realtime;' in voice_cfg
assert 'cfg.enableC4fmFixedPhaseSearch = false;' in voice_cfg
assert 'cfg.maxC4fmFixedPhaseCandidates = 0;' in voice_cfg
deep_trace = text[text.index('static bool p25Phase2DeepTraceEnabled'):text.index('static void rotateP25Phase2ValidationLogIfNeeded')]
assert 'return value == "1" || value == "true" || value == "yes" || value == "on";' in deep_trace
assert 'return true;' not in deep_trace
print('P25 Phase 2 voice-arm DSP starvation regression: PASS')
