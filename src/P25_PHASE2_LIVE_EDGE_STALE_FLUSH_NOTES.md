# P25 Phase 2 live-edge stale decode flush

Field symptom: waterfall shows the voice carrier has stopped, but the app continues
logging `no voice sync` and then emits delayed random bleeps.  The Phase-2 rolling
IQ worker was keeping overlap/context IQ alive after a hard no-sync/no-VCW window,
so old buffered chunks could still be decoded well after the signal ended.

Fix:
- Add `p25Phase2ShouldFlushStaleVoicePipeline()`.
- When an armed Phase-2 voice receiver reports a hard `NoSync` window with no
  VCWs, no bursts, no superframe/mask and no decoded audio, clear the rolling IQ
  window, clear pending PCM, erase decode cadence state and move the receiver IQ
  cursor to the live ring edge.
- Apply the same flush to GUI and CLI decode loops.

This is intentionally narrower than the earlier retune/discard logic: active
superframe/mask/VCW windows remain available to the decoder.  Only empty no-sync
stale-tail windows are flushed.
