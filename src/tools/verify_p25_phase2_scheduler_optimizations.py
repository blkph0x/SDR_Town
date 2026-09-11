#!/usr/bin/env python3
"""Verify P25 Phase 2 scheduler/decode-path optimizations from field capture analysis."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

checks = {
    'effective minFresh helper': 'p25Phase2EffectiveMinFreshSamples' in main,
    'no-new-iq does not stall decode': (
        'if (rolling.samples.empty())' in main and
        'if (!appended)' in main and
        'No new IQ this tick, but keep decoding from the rolling buffer.' in main
    ),
    'depleted rolling pull boost': 'p25Phase2PrepareRollingIqPull' in main and 'recoveryPull' in main,
    'speaker decode gap window': 'kP25Phase2SpeakerDecodeGapBlockMs' in main,
    'narrow decode-cadence-gap block': 'veryRecentSpeakerGap' in main,
    'lower minFresh floor': (
        'plan.minFreshFloorSamples = 8192.0' in main
        and 'plan.minFreshFloorSamples = 16384.0' in main
    ),
    'waiting-fresh logs effective minFresh': 'effMinFresh' in main,
    '180ms cold playback prime': '0.180' in main.split('size_t pushP25LiveStreamingAudio', 1)[1][:2200],
    '120ms hot restart prime': '0.120' in main.split('size_t pushP25LiveStreamingAudio', 1)[1][:2600],
    '180ms p25 live target': '0.180' in main.split('size_t pushP25LiveStreamingAudio', 1)[1][:3400],
    'ring already primed bypass': 'ringAlreadyPrimed' in main,
    'trust emitted pcm for carrier gate': (
        'p25Audio.phase2EmittedPcmFrames > 0' in main
        and 'p25VoiceBlockHasSpeakerTimelineAudio(p25Audio)' in main
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 scheduler optimization regression failed: ' + ', '.join(failed))
print('P25 Phase 2 scheduler optimization regression: PASS')
