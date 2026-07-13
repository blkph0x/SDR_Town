#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text()
assert 'P25 DSP VOICE LOOP:' in main, 'missing DSP voice loop diagnostic'
assert 'getNewIQWindowForReceiver(i, rx, pullWindow)' in main, 'GUI Phase2 path must use continuous cursor IQ'
assert 'getNewIQWindowForReceiver(di, rx, pullWindow)' in main, 'CLI Phase2 path must use continuous cursor IQ'
assert 'setReceiverCursorToLiveEdge(i, rx)' not in main[main.index('phase2BufferedDecode = monP25VoiceDecode'):main.index('const size_t got = iq.size();')], 'GUI Phase2 path still skips to live edge'
assert 'setReceiverCursorToLiveEdge(di, rx)' not in main[main.index('const bool phase2BufferedDecode = rxP25VoiceDecode'):main.index('const size_t got = iq.size();', main.index('const bool phase2BufferedDecode = rxP25VoiceDecode'))], 'CLI Phase2 path still skips to live edge'
print('P25 Phase 2 continuous rolling-IQ voice decode regression: PASS')
