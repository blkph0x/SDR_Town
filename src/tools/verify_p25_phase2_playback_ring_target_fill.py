#!/usr/bin/env python3
"""Verify P25 speaker playback keeps a target jitter fill instead of starving the ring."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
engine_h = (root / 'include' / 'AudioEngine.h').read_text(encoding='utf-8', errors='replace')
engine_cpp = (root / 'src' / 'AudioEngine.cpp').read_text(encoding='utf-8', errors='replace')
live_push = main.split('size_t pushP25LiveStreamingAudio', 1)[1].split(
    'size_t pushP25SpeakerAudio', 1
)[0]
speaker_push = main.split('size_t pushP25SpeakerAudio', 1)[1][:1200]
topup_helper = main.split('size_t p25TopUpSpeakerPlaybackRing', 1)[1].split(
    'std::vector<float> resampleDecodedP25PcmWithState', 1
)[0]

checks = {
    'ring queued samples API': 'getRingQueuedSamples' in engine_h and 'getRingQueuedSamples' in engine_cpp,
    'jitter cap helper': 'getJitterQueueCapFrames' in engine_h and 'getJitterQueueCapFrames' in engine_cpp,
    'bounded digital-voice jitter cap': 'kDigitalVoiceJitterSeconds = 0.65' in engine_cpp,
    '320ms cold playback prime': '0.320' in live_push[:2600],
    '240ms hot restart prime': '0.240' in live_push[:3000],
    '420ms target fill': '0.420' in live_push[:3600],
    'fresh real PCM leaves live headroom below jitter cap': (
        'measuredCadenceCeiling' in live_push[:3800]
        and 'outRate * 0.480' in live_push[:3800]
        and 'std::min(jitterCap, measuredCadenceCeiling)' in live_push[:3800]
    ),
    '240ms low-water restart cushion': 'ringLowWaterSamples' in main and '0.240' in live_push[:3800],
    '650ms pending stash': '0.650' in live_push[:5200]
        and '1.200' not in live_push[:5200],
    '20ms p25 frame push': 'phase2FrameSamples' in speaker_push[:900] and '0.020' in speaker_push[:900],
    'batch top-up loop': 'targetQueuedSamples' in main and 'while (pending.size() - totalPushed >= frameSize)' in main,
    'speaker top-up helper': 'p25TopUpSpeakerPlaybackRing' in main,
    'phase2 playout bridge helper': 'pushP25Phase2PlayoutBridge' in main,
    'phase2 clock-only bridge': 'Clock-only zeros' in main and 'return std::vector<float>(bridgeSamples, 0.0f);' in main,
    'phase2 bridge tagged speaker push': 'engine->pushBridgeAudioToActiveOutputs(bridge.data() + pushed, effectiveFrameSize, activeOutputIndices);' in main,
    'phase2 real pcm preempts bridge headroom': (
        'dropQueuedBridgeAudio(bridgeHeadroomDeficit, activeOutputIndices)' in main
        and 'bridgeHeadroomDeficit' in live_push[:5600]
    ),
    'phase2 bridge measured-cadence underrun target': 'outRate * 0.320' in main.split('size_t pushP25Phase2PlayoutBridge', 1)[1][:1600],
    'phase2 bridge bounded refill': 'outRate * 0.080' in main.split('size_t pushP25Phase2PlayoutBridge', 1)[1][:1800],
    'phase2 bridge longer active-clear cap': 'consecutivePlayoutBridgeFrames >= 225' in main,
    'active clear path early backlog catch-up': 'activeSpeakerClearPath' in main and '0.050' in main,
    'gui and cli active clear scheduler parity': main.count('activeSpeakerClearPath') >= 2,
    'phase2 bridge does not fade last speech sample': (
        'Fading the last speech sample into the hole' in main
        and 'Soft-join island tails' not in main
    ),
    'phase2 bridge survives empty worker holes': 'consecutiveEmptyFeedWindows >= 48' in main,
    'phase2 mid-call hot prime': 'midCallRingRestart' in main and '0.240' in live_push[:3200],
    'startup prime is not reapplied to an active stream':
        'p25SpeakerNeedsStartupPrime(queuedNow, pending.size(), minPrimeSamples)' in live_push,
    'phase2 bridge bounded to active clear tail': 'p25Phase2PlayoutBridgeAllowed' in main and 'sinceLastEmitMs > 4500' in main,
    'top-up never inserts synthetic silence behind speech': (
        'pushP25Phase2PlayoutBridge(' not in topup_helper
        and 'pushBridgeAudioToActiveOutputs(' not in topup_helper
        and 'pushP25LiveStreamingAudio(' in topup_helper
        and 'if (bridgeAudioPushed) *bridgeAudioPushed = 0;' in topup_helper
    ),
    'no other producer calls the legacy bridge helper':
        main.count('pushP25Phase2PlayoutBridge(') == 1,
    'callback still zero-fills genuine underruns':
        'std::memset(out + read, 0, (toRead - read) * sizeof(float));' in engine_cpp,
    'phase2 bridge does not refresh follow activity': 'p25Phase2ResetPlayoutBridge(rx);' in main,
    'dsp worker top-up call': 'speakerEngine = peekAudioEngineIfReady()' in main
        and 'const size_t topUpPushed = p25TopUpSpeakerPlaybackRing(' in main
        and 'pendingAudioByRx,\n                        receiverSessionStillActive' in main,
    'async worker preserve top-up call': 'currentWorkerSessionActive' in main and 'const size_t topUpPushed =\n                    p25TopUpSpeakerPlaybackRing(audioOutputEngine,' in main,
    'top-up helper reports pushed samples': 'return totalPushed;' in topup_helper,
    'ring already playing bypass': 'p25SpeakerNeedsStartupPrime' in main,
    'speaker queue depth single-flight': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 1' in main
        and 'const size_t runningJobs = p25VoiceWorkerBusy.load' in main,
    'speaker sustain near-live hop': 'kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080' in main,
    'speaker sustain frame-pair fresh': 'kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.040' in main,
    'speaker sustain bounded lattice overlap': 'kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.280' in main,
    'speaker backlog catch-up uses backlog constants (ISS-0003)': (
        'kP25Phase2VoiceDecodeBacklogCatchUpChunkSeconds' in main
        and 'kP25Phase2VoiceDecodeBacklogCatchUpMinFreshSeconds' in main
        and 'kP25Phase2VoiceDecodeSpeakerCatchUpChunkSeconds' not in main
    ),
    'backlog helper uses effective decode cursor': (
        'p25Phase2UndecodedBacklogSamples(const RollingIqWindow& rolling)' in main
        and 'effectiveDecodeAbsolute()' in main.split(
            'p25Phase2UndecodedBacklogSamples(const RollingIqWindow& rolling)', 1
        )[1][:900]
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 playback ring target-fill regression failed: ' + ', '.join(failed))
print('P25 Phase 2 playback ring target-fill regression: PASS')
