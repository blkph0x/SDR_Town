#!/usr/bin/env python3
from pathlib import Path
text = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(encoding='utf-8', errors='replace')
start = text.index('auto armP25VoiceFollowState = [this]')
end = text.index('auto scheduleP25VoiceFollowArm', start)
arm = text[start:end]
checks = [
    ('voice-arm blocks on receiversMutex', 'std::unique_lock<std::mutex> lk(receiversMutex);' in arm),
    ('voice-arm blocks on stateMutex', 'std::unique_lock<std::mutex> rxLock(rx.stateMutex);' in arm),
    ('voice-arm owns dspMutex before decoder replacement', 'std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex);' in arm),
    ('voice-arm no receivers try-lock', 'receiversMutex, std::try_to_lock' not in arm),
    ('voice-arm no state try-lock', 'rx.stateMutex, std::try_to_lock' not in arm),
    ('commit comment present', 'create/commit the' in arm and 'traffic channel synchronously' in arm and 'own dspMutex' in arm),
    ('retune receiver blocks on receiversMutex', 'std::unique_lock<std::mutex> listLock(receiversMutex);' in text),
    ('prepare target blocks on receiversMutex', 'std::unique_lock<std::mutex> lk(receiversMutex);' in text),
]
missing = [name for name, ok in checks if not ok]
if missing:
    raise SystemExit('P25 Phase 2 voice-arm blocking commit regression FAILED: ' + ', '.join(missing))
print('P25 Phase 2 voice-arm blocking commit regression: PASS')
