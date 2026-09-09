#!/usr/bin/env python3
from pathlib import Path
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
s = main
assert 'p25Phase2ShouldFlushStaleVoicePipeline' in s, 'missing stale Phase-2 pipeline flush helper'
assert 'out.diag == P25VoiceDiagCode::NoSync' in s, 'flush helper must key on hard no-sync'
assert 'out.phase2VoiceCodewords == 0' in s, 'flush helper must not flush active VCW windows'
assert 'out.phase2SuperframeBursts == 0' in s, 'flush helper must not flush active superframe windows'
assert 'p25ReceiverSessionKey(rx)' in s, 'rolling IQ buffers must use generation-stamped receiver session keys'
assert 'phase2IqByRx.erase(p25ReceiverSessionKey(rx));' in s or 'phase2IqByRx.erase(sessionKey);' in s, 'GUI/CLI must clear rolling IQ buffer on stale no-sync'
assert 'lastPhase2DecodeByRx.erase(p25ReceiverSessionKey(rx));' in s or 'lastPhase2DecodeByRx.erase(sessionKey);' in s, 'GUI/CLI must reset decode cadence after stale no-sync'
assert 'p25Phase2ClearSpeakerPendingQueue(rx,' in s and 'p25SpeakerPendingFor(pendingAudioByRx, rx)' in s, 'GUI/CLI must clear pending PCM through the central queue helper on stale no-sync'
assert 'DeviceManager::instance().setReceiverCursorToLiveEdge(devIndex, rx);' in s, 'GUI path must advance cursor to live edge after stale tail'
assert 'mgr.setReceiverCursorToLiveEdge(di, rx);' in s, 'CLI path must advance cursor to live edge'
assert 'setReceiverCursorBeforeLiveEdge(\n                    source.deviceIndex, *trafficRx,' in s, 'Phase 2 traffic source should arm with bounded pre-roll'
assert 'uint64_t flushSeq = 0;' in s, 'P25 voice worker job/result must carry audio flush generation'
assert 'job.flushSeq = p25PendingAudioFlushSeq.load(std::memory_order_acquire);' in s, 'GUI worker must snapshot audio flush generation on submit'
assert 'result.flushSeq = job.flushSeq;' in s, 'worker result must preserve submit-time audio flush generation'
assert 'audio-flush-sequence-stale' in s, 'decode and publish paths must reject results from a prior audio flush'
print('P25 Phase 2 live-edge stale flush regression: PASS')

