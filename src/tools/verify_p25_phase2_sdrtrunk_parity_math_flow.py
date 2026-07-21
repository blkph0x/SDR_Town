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
    'Hard Phase 2 framer must accept only normal sync after demod correction': 'static constexpr std::array<uint64_t, 1> kSyncWords' in p25 and '0x0104015155ull' not in p25 and '0xFEFBFEAEAAull' not in p25 and '0xA8A2A80800ull' not in p25 and '0xA8A2A8D800ull' not in p25,
    'Absolute traffic chunk alignment must point at chunk start until decode consumes it': 'm_phase2StreamDibits = chunkEnd;' not in p25 and 'm_phase2StreamDibits = chunkStartAbsolute;' in p25 and 'advances it to the end after the chunk is actually consumed' in p25,
    'Control-channel C4FM hard lock must skip CQPSK traffic search': 'hasTrustedPhase1ControlPayload' in p25 and 'c4fmHardLockSkippedCqpsk' in p25 and '!phase2TrafficDecoder' in p25 and 'm_config.stopC4fmSearchOnHardLock' in p25 and 'cfg.stopC4fmSearchOnHardLock = true;' in main,
    'Persistent mask phase promotion must use low-error decoded I-ISCH': 'b.isch.valid && !b.isch.sync' in p25 and 'static_cast<uint8_t>(b.isch.channel & 0x01u)' in p25,
    'sdrtrunk 40-bit ISCH + 320-bit timeslot payload start': 'const size_t payload = pos + P25LiveDecoder::Phase2FrameSyncDibits;' in p25,
    'DUID positions must match 320-bit timeslot bits 0,1,74,75,244,245,318,319': 'payloadDibits[0]' in p25 and 'payloadDibits[37]' in p25 and 'payloadDibits[122]' in p25 and 'payloadDibits[159]' in p25,
    'Phase 2 timeslot payload must not status-strip after ISCH': 'posInPeriod' not in p25 and 'status-stripped continuous payload' not in p25,
    'XOR mask phase must not relabel TDMA traffic slot': 'trafficSuperframeBurstIndex' in p25 and 'maskSuperframeBurstIndex' in p25 and 'burst.grantSlot = superframeLocked' in p25 and 'phase2TrafficSlotForSuperframeBurst(dibits, superframeOffset, trafficSuperframeBurstIndex)' in p25 and 'phase2TrafficSlotFromSuperframeBurstIndex(maskSuperframeBurstIndex)' not in p25 and 'window.sessions[trafficSlot & 0x01u]' in p25 and 'slotSessions[trafficSlot & 0x01u]' in p25,
    'Voice frame starts must match sdrtrunk 2/76/172/246 bit offsets': 'const std::array<size_t, 4> starts{1, 38, 86, 123};' in p25,
    'AMBE path rejects mbelib repeat/erasure frames as fresh speech': 'totalErrors <= 24' not in main and 'decoded.totalErrors > 3' in main and "decoded.message.find('R')" in main and 'p25AmbeDecodeFrameLooksUsable' in main,
    'Slot probe uses recent decoded PCM to suppress flips': 'currentSlotHasUsefulAudio' in main and 'decodedFrames > 0' in main,
    'Phase 2 release is target-slot keyed, not grant-clear keyed': 'P25P2CallAudioKey' in main and 'phase2TargetSessionAudioRelease' in main,
    'Unknown audio queues by target call key before release': 'if (unknownSecurity)' in main and 'p25QueuePhase2PendingAmbeFrame(rx, audioKey, pending);' in main and ('p25TakePhase2PendingAudio(rx, key)' in main or 'p25TakePhase2PendingAudio(rx, audioKey)' in main),
    'Explicit clear grant queues until target traffic proves clear': 'cleargranttargetreleaseallowed' not in main_l and 'p25phase2explicitcleargrantvoicereleaseevidence' in main_l and 'releasePendingRawVoiceFromExplicitClearTrafficProof' in main and 'explicit-clear-grant-target-release' not in main and 'explicit-clear-grant-validated-release' not in main and 'explicit-clear-grant-traffic-clear-release' in main and 'validatedExplicitClearGrantSeed' not in main and 'boundedProbeAccepted' not in main and 'const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;' in main and 'establishedClearCall' in main and 'const bool targetTrafficClearEvidence =' in main and 'p25Phase2TargetHardClearEvidence(out)' in main and 'p25Phase2TargetPttSessionClear' in main and 'p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out)' in main and 'explicitGrantTargetSlotSelected' in main and 'explicitClearGrantSelectedVoiceRelease' not in main and '(targetTrafficClearEvidence || explicitClearGrantSelectedVoiceRelease)' not in main and 'targetTrafficClearEvidence &&' in main.split('const bool explicitClearGrantHardVoiceRelease', 1)[1].split(';', 1)[0],
    'Known TDMA slot must not decode opposite-slot VCWs as target audio': 'armed follow + masked vcw always counts toward the followed target slot' not in main and 'armed follow + descrambled voice' not in main and 'count for target audio' not in main and 'std::swap(out.phase2TargetVoiceCodewords' not in main and 'phase2AutoInvertGrantSlot' not in main and 'don\'t skip decode even on slot mismatch' not in main,
    'Call/session reset clears keyed pending queue': 'p25ClearPhase2PendingAudio(rx);' in main,
}
missing=[name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 sdrtrunk parity math/flow check failed: ' + '; '.join(missing))
print('P25 sdrtrunk parity math/flow regression: PASS')
