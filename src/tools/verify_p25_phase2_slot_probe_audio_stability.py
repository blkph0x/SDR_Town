#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
sm = (root / 'P25FollowStateMachine.cpp').read_text(errors='ignore')
checks = {
    'decoded audio outranks wrong-slot diagnostic': 'if (out.decodedFrames > 0 && !out.audio.empty()) return P25VoiceDiagCode::Decoding;' in main,
    'accepted AMBE clears wrong-slot latch': 'out.phase2WrongSlot = false;' in main and 'out.phase2WrongSlotVoiceCodewords = 0;' in main,
    'audio emitter does not hard-mute mixed wrong-slot windows': 'out.phase2EssMissing || out.phase2WrongSlot ||' not in main and 'out.phase2AmbeRejected ||' not in main,
    'GUI slot probe suppressed when any PCM exists': 'decoded > 0 && voiceDiag.audioSamples > 0' in main,
    'CLI slot probe suppressed when any PCM exists': 'diag.decodedFrames > 0 && diag.audioSamples > 0' in main,
    'state machine documents pure wrong-slot only': 'Only\n        // a pure wrong-slot window with no decoded audio reaches this point.' in sm,
}
missing=[name for name,ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 Phase 2 slot-probe/audio stability regression FAILED: '+', '.join(missing))
print('P25 Phase 2 slot-probe/audio stability regression: PASS')
