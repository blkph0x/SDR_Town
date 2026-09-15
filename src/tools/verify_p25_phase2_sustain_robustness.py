#!/usr/bin/env python3
"""Verify P25 Phase 2 sustain/playback robustness regressions."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
p25 = (root / 'src' / 'P25LiveDecoder.cpp').read_text(encoding='utf-8', errors='replace')
bootstrap_block = main.split('bool p25Phase2BootstrappedMaskTargetVoiceEvidence', 1)[1][:500] if 'bool p25Phase2BootstrappedMaskTargetVoiceEvidence' in main else ''

checks = {
    'fixed speaker sustain cadence': (
        'During active speaker sustain, keep a fixed near-live cadence' in main and
        'gLastDspMicros' not in main.split('p25Phase2AdaptiveVoiceDecodeCadenceMs', 1)[1].split('bool p25Phase2SpeakerSustainDecodeActive', 1)[0]
    ),
    'speaker gate requires target vcw': 'phase2-no-target-slot-vcw' in main,
    'bootstrap requires target vcw': (
        'out.phase2TargetVoiceCodewords >= 2' in bootstrap_block and
        'out.phase2OppositeVoiceCodewords' not in bootstrap_block
    ),
    'skip offset probe after session lock': (
        'skipOffsetProbeAfterMaskLock' in main and
        'p25Phase2SessionHadBurstEye(rx) ||' in main.split('skipOffsetProbeAfterMaskLock', 1)[1][:500]
    ),
    'bounded p25 speaker push helper': (
        'pushP25SpeakerAudio' in main and
        'phase2FrameSamples' in main.split('size_t pushP25SpeakerAudio', 1)[1][:900] and
        'pushP25LiveStreamingAudio(engine, pending, audio, activeOutputIndices,' in
            main.split('size_t pushP25SpeakerAudio', 1)[1][:1200] and
        'phase2FrameSamples, ringFillPercent,' in
            main.split('size_t pushP25SpeakerAudio', 1)[1][:1200] and
        'warmPendingRealAudio, pushedRealAudio);' in
            main.split('size_t pushP25SpeakerAudio', 1)[1][:1200] and
        'pushAudioFrames(engine, pending, audio' not in main.split('size_t pushP25SpeakerAudio', 1)[1][:900]
    ),
    'sustain decode on successful emit': 'sustain.hadSuccessfulEmit' in main.split('p25Phase2UseSustainDecodeWindowLocked', 1)[1][:3500],
    'selected clear streaming before first pcm': (
        'selectedClearTrafficStreaming' in main and
        'targetClearEvidence' in main.split('p25Phase2EstablishedClearVoiceStreamingLocked', 1)[1][:1800] and
        'selectedClearStreamingEye' in main
    ),
    'same-call clear sustain feeds target voice': (
        'const bool sameCallClearSustainFeed' in main and
        'targetTrafficClearEvidence ||' in main.split('const bool sameCallClearSustainFeed', 1)[1][:1200] and
        'p25Phase2AudioTailGraceActive(rx)' in main.split('const bool sameCallClearSustainFeed', 1)[1][:1200] and
        'sameCallClearSustainFeed ||' in main.split('securityProvedClearForFeed', 1)[1][:500]
    ),
    'broad clear latch cannot feed mbelib': (
        'recentClearSecurityForCall &&' in main.split('const bool establishedClearCall', 1)[1][:220] and
        '(latchedClearForCall || recentClearSecurityForCall)' not in main and
        'broad Clear latch alone must not' in main
    ),
    'security gate sustain requires recent target proof': (
        'sameCallRecentClearSustain' in main and
        'recentTargetClearForCall' in main and
        'recent-clear-sustain' in main and
        'latched-clear-sustain' not in main
    ),
    'explicit clear grant queues until target traffic proof': (
        'explicitClearGrantHardVoiceRelease' in main and
        'explicitClearGrantSelectedVoiceRelease' not in main and
        '(targetTrafficClearEvidence || explicitClearGrantSelectedVoiceRelease)' not in main and
        'freshTargetTrafficClearEvidence &&' in main.split('const bool explicitClearGrantHardVoiceRelease', 1)[1].split(';', 1)[0] and
        'explicit clear control-channel' in main
    ),
    'hot cqpsk search bounded for live voice': (
        'kP25VoiceWorkerHotRealtimeBudgetMs = 120' in main and
        'kP25VoiceWorkerHotMaxCqpskCandidates = 8' in main
    ),
    'deep acch when mask applied': 'mask != nullptr' in p25 or '&& mask' in p25,
    'soft cqpsk hold before mac ess': (
        'allowRealtimePhase2SoftDemodHold' in p25 and
        'allowSoftCqpskStop' in p25 and
        'const bool standardsStateMayHoldDemod =\n        phase2StandardsStateSeen && m_cqpskLock.valid;' in p25 and
        '(standardsStateMayHoldDemod ||\n         m_config.allowPhase2SoftAmbeMaskPhaseLock ||\n         allowRealtimeLockOnlyCandidate)' in p25 and
        'standardsStateMayHoldDemod ||\n        allowRealtimePhase2SoftDemodHold' in p25 and
        'phase2StandardsStateSeen ||\n        allowRealtimePhase2SoftDemodHold' not in p25
    ),
    'locked soft cqpsk candidate selected': (
        'lockedSoftPhase2Evidence' in p25 and
        '(trust > 0 || lockedSoftPhase2Evidence) && betterLiveResult(candidate, best' in p25
    ),
    'lock-only cqpsk can hold before fresh mac ess': (
        'allowRealtimeLockOnlyCandidate' in p25 and
        'm_config.maxCqpskSearchCandidates <= 1' in p25 and
        'allowRealtimeLockOnlyCandidate);' in p25
    ),
    'bounded realtime mask rescue': (
        'maxRescueCandidates' in p25 and
        'm_config.realtimeVoiceSearch ? std::min<size_t>(phaseWindows.size(), 2u)' in p25 and
        'rescueScoreSlots = 12u' in p25 and
        'rescueDeepBudget = m_config.realtimeVoiceSearch ? 1u : 8u' in p25 and
        '? size_t{2}' in p25
    ),
    'throttled unknown-security realtime mask hunt': (
        'm_phase2LastFullMaskPhaseHuntGeneration' in p25 and
        'kRealtimeFullMaskPhaseHuntSpacingGenerations = 24' in p25 and
        '!m_config.allowPhase2SoftAmbeMaskPhaseLock' in p25 and
        'realtimeMaskHuntThrottledForLock' in p25 and
        'const bool cheapProbeMask = !annotateSessionCodewords || realtimeMaskHuntThrottledForLock;' in p25 and
        'throttleRealtimeUnknownSecurityAcchRescue' in p25 and
        '!throttleRealtimeUnknownSecurityAcchRescue' in p25
    ),
    'realtime acch uses exact duid before rescue fanout': (
        'allowAlternateKindFanout' in p25 and
        'deepAcchSearch ||' in p25 and
        '(!superframeLocked || xorMask == nullptr)' in p25 and
        'if (!allowAlternateKindFanout && k != burst.kind) break;' in p25 and
        'Deep rescue always fans out ACCH kinds' in p25
    ),
    'realtime acch de-dupes overlapping bursts': (
        'm_phase2RecentAcchDecodeBurstDibits' in p25 and
        'decodeAcch = true' in p25 and
        'recentlyDecodedAcchBurst' in p25 and
        'rememberAcchBurst' in p25 and
        '!duplicateRealtimeAcch' in p25 and
        p25.count('recentlyDecodedAcchBurst(') >= 3 and
        p25.count('rememberAcchBurst(') >= 3
    ),
    'contiguous phase2 timeslot payload': (
        'Phase 2 TDMA does not' in p25 and
        'payloadDibits[i] = dibits[payload + i] & 0x03;' in p25 and
        'stripPhase2TimeslotPayload' not in p25
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 sustain robustness regression failed: ' + ', '.join(failed))
print('P25 Phase 2 sustain robustness regression: PASS')
