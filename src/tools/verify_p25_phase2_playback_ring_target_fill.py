#!/usr/bin/env python3
"""Verify P25 speaker playback keeps a target jitter fill instead of starving the ring."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')
engine_h = (root / '../include/AudioEngine.h').resolve().read_text(encoding='utf-8', errors='replace')
engine_cpp = (root / 'AudioEngine.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'ring queued samples API': 'getRingQueuedSamples' in engine_h and 'getRingQueuedSamples' in engine_cpp,
    'jitter cap helper': 'getJitterQueueCapFrames' in engine_h and 'getJitterQueueCapFrames' in engine_cpp,
    'deeper digital-voice jitter cap': 'kDigitalVoiceJitterSeconds = 0.85' in engine_cpp,
    '120ms playback prime': '0.120' in main.split('pushP25LiveStreamingAudio', 1)[1][:2200],
    '500ms target fill': '0.500' in main.split('pushP25LiveStreamingAudio', 1)[1][:3200],
    '720ms fresh-audio ceiling': '0.720' in main.split('pushP25LiveStreamingAudio', 1)[1][:3400],
    'low-water 20ms fresh push': 'ringLowWaterSamples' in main and '0.100' in main.split('pushP25LiveStreamingAudio', 1)[1][:3600],
    '1200ms pending stash': '1.200' in main.split('pushP25LiveStreamingAudio', 1)[1][:3600],
    '20ms p25 frame push': 'phase2FrameSamples' in main.split('pushP25SpeakerAudio', 1)[1][:900] and '0.020' in main.split('pushP25SpeakerAudio', 1)[1][:900],
    'batch top-up loop': 'targetQueuedSamples' in main and 'while (pending.size() - totalPushed >= frameSize)' in main,
    'speaker top-up helper': 'p25TopUpSpeakerPlaybackRing' in main,
    'phase2 playout bridge helper': 'pushP25Phase2PlayoutBridge' in main,
    'phase2 clock-only bridge': 'Clock-only bridge' in main and 'std::vector<float> out(bridgeSamples, 0.0f);' in main,
    'phase2 bridge direct speaker push': 'engine->pushAudioToActiveOutputs(bridge.data() + pushed, effectiveFrameSize, activeOutputIndices);' in main,
    'phase2 bridge deeper target': 'outRate * 0.380' in main.split('pushP25Phase2PlayoutBridge', 1)[1][:1600],
    'phase2 bridge faster refill': 'outRate * 0.120' in main.split('pushP25Phase2PlayoutBridge', 1)[1][:1800],
    'phase2 bridge soft-join island tails': 'Soft-join island tails' in main,
    'phase2 bridge survives empty worker holes': 'consecutiveEmptyFeedWindows >= 32' in main,
    'phase2 mid-call hot prime': 'midCallRingRestart' in main and '0.040' in main.split('pushP25LiveStreamingAudio', 1)[1][:2800],
    'phase2 bridge bounded to active clear tail': 'p25Phase2PlayoutBridgeAllowed' in main and 'sinceLastEmitMs > 1600' in main,
    'phase2 bridge does not refresh follow activity': 'p25Phase2ResetPlayoutBridge(rx);' in main,
    'dsp worker top-up call': 'p25TopUpSpeakerPlaybackRing(speakerEngine' in main,
    'ring already primed bypass': 'ringAlreadyPrimed' in main,
    'speaker queue depth allows prestage': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 2' in main,
    'speaker sustain 40ms hop': 'kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.040' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 playback ring target-fill regression failed: ' + ', '.join(failed))
print('P25 Phase 2 playback ring target-fill regression: PASS')
