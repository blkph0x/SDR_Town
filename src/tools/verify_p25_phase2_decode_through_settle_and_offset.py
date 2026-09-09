from pathlib import Path
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
dec = Path('src/P25LiveDecoder.cpp').read_text(errors='ignore')
assert 'Decode through settle' in main or 'Do not skip Phase-2 decode during retune/settle' in main
assert 'p25VoiceOutputMutedForSettle' in main
assert 'p25VoiceSettleUntilMs > nowMs' in main
assert 'p25VoiceBlockMayBypassPostArmSettle' in main
assert 'p25TalkgroupGrantProvesSpeakerClear(tg)\n                        ? nowMs' not in main
assert main.count('p25VoiceBlockMayBypassPostArmSettle(') >= 4, 'GUI, worker, and CLI settle bypass must share proof-aware gate'
assert 'mgr.setReceiverCursorToLiveEdge(i, rx);\n                            }\n                        }' not in main
assert 'mgr.setReceiverCursorToLiveEdge(di, rx);\n                        }\n                    }' not in main
assert 'Never assign rx.p25VoiceLiveDecoder unless rx.dspMutex is held' not in main or 'if (dspLock.owns_lock())' in main
assert 'std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);' in main
assert 'if (!clearAudio)' in main and 'return false;' in main
assert 'Convert that window coordinate to the monotonic stream coordinate\n            // exactly once' in dec
assert 'Adding streamBurstStart here collapses same-burst' in dec
assert 'const uint64_t streamDibit = codeword.streamDibitKnown' in dec
assert '? codeword.streamDibit' in dec
assert ': (streamStart + static_cast<uint64_t>(codeword.dibitOffset));' in dec
assert 'Normalize offsets back to the caller\'s fresh input' in dec
assert 'annotatePhase2SessionCodewords(out, dibits, softDibitMinAbsLlr);' in dec
assert 'const uint8_t trafficSlot = phase2TrafficSlotForSuperframeBurst' in dec
assert 'auto& burstSession = slotSessions[trafficSlot & 0x01u];' in dec
assert 'trafficRx->p25AfcFrozen = source.retunesPrimary && inheritedControlAfcKnown && !phase2Traffic' in main
assert 'p25TrustedControlOffsetForPhase2Traffic' in main
assert 'kP25Phase2ControlCarryFreshMs = 30000' in main
assert main.count('p25TrustedControlOffsetForPhase2Traffic(') >= 4, 'GUI and CLI follow paths must share trusted control-offset carry'
assert main.count('p25SeedPhase2TrafficOffsetFromControl(') >= 3, 'GUI traffic source/reuse/hop paths must seed through the shared helper'
assert 'rx.p25Phase2TrafficTargetOffsetTrust = carryPhase2TrafficOffset ? 1 : 0;' in main, 'CLI waitgrant must carry offset as unverified traffic hint'
assert 'rx.p25Phase2TrafficTargetOffsetTrust = seedPhase2TrafficOffset ? 1 : 0;' in main, 'legacy GUI follow must carry offset as unverified traffic hint'
assert 'P25 one-RTL Phase 2 traffic decode seeded from trusted control target offset' in main
assert 'traffic TDMA/MAC evidence must promote the hint' in main
assert 'A control-channel target probe is not a traffic-channel AFC' not in main
print('P25 Phase 2 decode-through-settle/offset/session regression: PASS')
