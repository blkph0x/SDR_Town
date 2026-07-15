#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p25 = (root / 'P25LiveDecoder.cpp').read_text(errors='ignore')
main = (root / 'main.cpp').read_text(errors='ignore')
main_l = main.lower()
follow_h_path = root.parent / 'include' / 'P25FollowStateMachine.h'
follow_h = follow_h_path.read_text(errors='ignore') if follow_h_path.exists() else main
follow = (root / 'P25FollowStateMachine.cpp').read_text(errors='ignore')
checks = {
    'I-ISCH anchor must use decoded I-ISCH, not S-ISCH sync word': '!burst.isch.sync && burst.isch.location <= 2 && burst.superframeBurstIndexKnown' in p25 and 'channelMatchesGrantSlot' in p25,
    'Persistent mask phase promotion must use low-error decoded I-ISCH': 'b.isch.valid && !b.isch.sync' in p25 and 'static_cast<uint8_t>(b.isch.channel & 0x01u)' in p25,
    'sdrtrunk 40-bit ISCH + 320-bit timeslot payload start': 'const size_t payload = pos + P25LiveDecoder::Phase2FrameSyncDibits;' in p25,
    'DUID positions must match 320-bit timeslot bits 0,1,74,75,244,245,318,319': 'payloadDibits[0]' in p25 and 'payloadDibits[37]' in p25 and 'payloadDibits[122]' in p25 and 'payloadDibits[159]' in p25,
    'Phase 2 timeslot payload must not status-strip after ISCH': 'posInPeriod' not in p25 and 'status-stripped continuous payload' not in p25,
    'XOR mask phase must not relabel TDMA traffic slot': 'trafficSuperframeBurstIndex' in p25 and 'maskSuperframeBurstIndex' in p25 and 'burst.grantSlot = phase2TrafficSlotFromSuperframeBurstIndex(trafficSuperframeBurstIndex);' in p25 and 'phase2TrafficSlotFromSuperframeBurstIndex(maskSuperframeBurstIndex)' not in p25 and 'window.sessions[phase2TrafficSlotFromSuperframeBurstIndex(slot) & 0x01u]' in p25 and 'slotSessions[phase2TrafficSlotFromSuperframeBurstIndex(slot) & 0x01u]' in p25,
    'Voice frame starts must match sdrtrunk 2/76/172/246 bit offsets': 'const std::array<size_t, 4> starts{1, 38, 86, 123};' in p25,
    'AMBE path must not hard-gate on totalErrors': 'totalErrors <= 24' not in main and 'p25AmbeDecodeFrameLooksUsable' in main,
    'Slot probe uses recent decoded PCM to suppress flips': 'currentSlotHasUsefulAudio' in main and 'decodedFrames > 0' in main,
    'Phase 2 release is target-slot keyed, not grant-clear keyed': 'P25P2CallAudioKey' in main and 'phase2TargetSessionAudioRelease' in main,
    'Unknown audio queues by target call key before release': 'if (unknownSecurity)' in main and 'p25QueuePhase2PendingAmbeFrame(rx, audioKey, pending);' in main and ('p25TakePhase2PendingAudio(rx, key)' in main or 'p25TakePhase2PendingAudio(rx, audioKey)' in main),
    'Explicit clear grant waits for target traffic clear evidence before release': 'cleargranttargetreleaseallowed' not in main_l and 'p25phase2explicitcleargrantvoicereleaseevidence' in main_l and 'releasePendingRawVoiceFromValidatedExplicitClearGrant' in main and 'explicit-clear-grant-target-release' not in main and 'explicit-clear-grant-validated-release' in main and 'phase2DiagnosticAmbeProbeAccepted >= kP25Phase2ExplicitClearGrantProbeMinFrames' in main and 'const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;' in main and 'establishedClearCall' in main and 'const bool targetTrafficClearEvidence =' in main and 'out.phase2TargetSessionAudioRelease ||' in main and '(out.phase2TargetEssKnown && !out.phase2TargetEssEncrypted)' in main,
    'Known TDMA slot must not decode opposite-slot VCWs as target audio': 'armed follow + masked vcw always counts toward the followed target slot' not in main and 'armed follow + descrambled voice' not in main and 'count for target audio' not in main and 'std::swap(out.phase2TargetVoiceCodewords' not in main and 'phase2AutoInvertGrantSlot' not in main and 'don\'t skip decode even on slot mismatch' not in main,
    'Call/session reset clears keyed pending queue': 'p25ClearPhase2PendingAudio(rx);' in main,
}
missing=[name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 sdrtrunk parity math/flow check failed: ' + '; '.join(missing))
print('P25 sdrtrunk parity math/flow regression: PASS')
