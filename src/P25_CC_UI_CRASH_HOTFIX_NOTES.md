# P25 Control-Channel UI Crash/Freeze Hotfix

Field symptom: selecting a P25 control channel produced one decode/log update, often with `bestErr=5`, then the UI stopped responding. Clicking Return/Monitor/other buttons could hard-crash or show Windows "not responding".

Root cause addressed here:

1. The Qt/UI thread synchronously locked `p25ControlWorkerDecoderMutex` to reset the control-channel decoder while the background P25 control worker could still be inside a long `processIq()` call. If the worker was slow or stuck on false Phase-2 telemetry, any button that reset monitoring could block the UI.
2. The realtime control-channel worker was still allowed to run Phase-2 traffic-burst analysis. For a P25 Phase-1 FDMA control channel this is not required for normal TSBK grants, and false Phase-2 telemetry can be expensive and misleading.
3. The previous audio-first target-offset probe was still present in the hot path. It has now been hard-disabled so one voice decode window cannot run multiple full P25 decodes.

Changes:

- Added `P25LiveDecoderConfig::enablePhase2Decode`.
- Added `p25RealtimeControlDecoderConfig()` with Phase-2 traffic-burst decoding disabled.
- The GUI P25 control worker now uses the realtime control config.
- UI actions no longer block on `p25ControlWorkerDecoderMutex`; they set `p25ControlWorkerResetPending` and clear pending results. The worker applies the reset when it next owns the decoder.
- Kept Phase-2 decoding enabled for voice/follow/offline diagnostics.
- Hard-disabled the live target-offset probe block in `decodeP25VoiceAudioBlock()`.

Expected result: selecting a CC and clicking buttons should remain responsive. Control-channel TSBK grant decode should continue. Phase-2 traffic MAC/ESS work remains in the voice decoder path, not the control-channel UI worker.
