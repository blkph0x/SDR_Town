#!/usr/bin/env python3
from pathlib import Path
text = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(encoding='utf-8', errors='replace')
start = text.index('auto armP25VoiceFollowState = [this]')
end = text.index('auto scheduleP25VoiceFollowArm', start)
arm = text[start:end]
checks = [
    ('voice-arm uses bounded receiver try-lock retry', 'std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);' in arm),
    ('voice-arm uses bounded state try-lock retry', 'std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);' in arm),
    ('voice-arm owns dspMutex before decoder replacement', 'if (dspLock.owns_lock())' in arm and 'rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));' in arm),
    ('voice-arm marks reset pending when dsp busy', 'rx.p25VoiceResetPending = true;' in arm),
    ('voice-arm false until decoder committed', 'if (!clearAudio)' in arm and 'return false;' in arm),
    ('voice-arm removed stale rough-check branch', '/* rough check */ true' not in arm),
    ('commit comment present', 'create/commit the' in arm and 'traffic channel synchronously' in arm and 'own dspMutex' in arm),
    ('retune receiver blocks on receiversMutex', 'std::unique_lock<std::mutex> listLock(receiversMutex);' in text),
    ('prepare target blocks on receiversMutex', 'std::unique_lock<std::mutex> lk(receiversMutex);' in text),
]
missing = [name for name, ok in checks if not ok]
if missing:
    raise SystemExit('P25 Phase 2 voice-arm committed-before-success regression FAILED: ' + ', '.join(missing))
print('P25 Phase 2 voice-arm committed-before-success regression: PASS')
