#!/usr/bin/env python3
"""Regression guard for the P25 Phase 2 voice-arm path.

The GUI voice arm must publish the new grant metadata quickly, but it must not
claim the follow is armed until the live decoder has actually been reset under
dspMutex.  If the DSP worker is busy, the arm path leaves resetPending set and
returns false so the retry loop commits a fresh decoder instead of producing
short, stale-decoder audio bursts.
"""
from pathlib import Path
src = Path(__file__).resolve().parents[1] / "main.cpp"
text = src.read_text(encoding="utf-8")
start = text.index('auto armP25VoiceFollowState = [this]')
end = text.index('auto scheduleP25VoiceFollowArm', start)
arm = text[start:end]
assert 'std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);' in arm
assert 'if (dspLock.owns_lock())' in arm
assert 'rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));' in arm
assert 'rx.p25VoiceResetPending = true;' in arm
assert 'if (!clearAudio)' in arm and 'return false;' in arm
assert '/* rough check */ true' not in arm
assert 'own dspMutex before decoder replacement' in arm
assert 'if (attempt >= 600)' in text
assert 'P25 voice arm retry:' in text
assert 'enum class P25VoiceDecodeProfile' in text
voice_cfg = text[text.index('static P25LiveDecoderConfig p25VoiceDecoderConfig'):text.index('static int p25CliDecodeScore')]
assert 'P25VoiceDecodeProfile::Realtime' in voice_cfg
assert 'cfg.realtimeVoiceSearch = profile == P25VoiceDecodeProfile::Realtime;' in voice_cfg
assert 'cfg.enableC4fmFixedPhaseSearch = false;' in voice_cfg
assert 'cfg.maxC4fmFixedPhaseCandidates = 0;' in voice_cfg
deep_trace = text[text.index('static bool p25Phase2DeepTraceEnabled'):text.index('static void rotateP25Phase2ValidationLogIfNeeded')]
assert 'return value == "1" || value == "true" || value == "yes" || value == "on";' in deep_trace
assert 'qgetenv("SDR_TOWN_P25_DEEP_TRACE")' in deep_trace
assert 'return enabled;' in deep_trace
assert 'static const bool enabled' in deep_trace
print('P25 Phase 2 voice-arm DSP starvation regression: PASS')
