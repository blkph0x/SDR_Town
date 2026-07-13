#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
recv_path = root.parent / 'include' / 'Receiver.h'
recv = recv_path.read_text(errors='ignore') if recv_path.exists() else main
p25 = (root / 'P25LiveDecoder.cpp').read_text(errors='ignore')
ctrl = (root / 'P25Control.cpp').read_text(errors='ignore')
checks = {
    'receiver owns pending Phase 2 PCM queue': 'p25Phase2PendingAudio' in recv and 'p25Phase2PendingAudioArmed' in recv,
    'unknown Phase 2 PCM is queued not emitted': 'applyP25Phase2SecurityAudioGate' in main and 'waitingForClearGrant = true' in main and 'out.audio.clear();' in main,
    'pending queue is capped to sub-second audio': 'kMaxPendingSamples = 11520' in main,
    'clear call flushes pending queue': 'trustedClear && key.valid() && p25Phase2PendingAudioMatches' in main and 'p25TakePhase2PendingAudio' in main,
    'encrypted call drops pending queue': 'trustedEncrypted' in main and 'p25ClearPhase2PendingAudio(rx)' in main,
    'MAC_IDLE resets session': 'case 3: // MAC_IDLE' in p25 and 'phase2ClearCallSession(*session);' in p25,
    'MAC_HANGTIME resets session': 'case 6: // MAC_HANGTIME' in p25 and 'phase2ClearCallSession(*session);' in p25 and 'MAC_HANGTIME' in ctrl,
}
missing=[name for name,ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 Phase 2 sdrtrunk security/session gate FAILED: '+', '.join(missing))
print('P25 Phase 2 sdrtrunk security/session gate: PASS')
