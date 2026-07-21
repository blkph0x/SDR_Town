#!/usr/bin/env python3
from pathlib import Path
main = Path(__file__).resolve().parents[1] / 'main.cpp'
s = main.read_text()
assert 'p25Phase2ShouldFlushStaleVoicePipeline' in s, 'missing stale Phase-2 pipeline flush helper'
assert 'out.diag == P25VoiceDiagCode::NoSync' in s, 'flush helper must key on hard no-sync'
assert 'out.phase2VoiceCodewords == 0' in s, 'flush helper must not flush active VCW windows'
assert 'out.phase2SuperframeBursts == 0' in s, 'flush helper must not flush active superframe windows'
assert 'p25ReceiverSessionKey(rx)' in s, 'rolling IQ buffers must use generation-stamped receiver session keys'
assert 'phase2IqByRx.erase(p25ReceiverSessionKey(rx));' in s or 'phase2IqByRx.erase(sessionKey);' in s, 'GUI/CLI must clear rolling IQ buffer on stale no-sync'
assert 'lastPhase2DecodeByRx.erase(p25ReceiverSessionKey(rx));' in s or 'lastPhase2DecodeByRx.erase(sessionKey);' in s, 'GUI/CLI must reset decode cadence after stale no-sync'
assert 'pendingAudioByRx[p25ReceiverSessionKey(rx)].clear();' in s or 'pendingAudioByRx[sessionKey].clear();' in s, 'GUI/CLI must clear pending PCM on stale no-sync'
assert 'DeviceManager::instance().setReceiverCursorToLiveEdge(devIndex, rx);' in s, 'GUI path must advance cursor to live edge after stale tail'
assert 'mgr.setReceiverCursorToLiveEdge(di, rx);' in s, 'CLI path must advance cursor to live edge'
assert 'setReceiverCursorBeforeLiveEdge(\n                    source.deviceIndex, *trafficRx,' in s, 'Phase 2 traffic source should arm with bounded pre-roll'
print('P25 Phase 2 live-edge stale flush regression: PASS')
