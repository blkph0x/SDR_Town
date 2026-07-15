from pathlib import Path
main = Path('src/main.cpp').read_text(errors='ignore')
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
assert 'Do not add burst.dibitOffset again' in dec
assert 'const uint64_t streamDibit = streamStart +\n                static_cast<uint64_t>(codeword.dibitOffset);' in dec
assert 'Normalize offsets back to the caller\'s fresh input' in dec
assert 'annotatePhase2SessionCodewords(out, dibits);' in dec
assert 'slotSessions[phase2TrafficSlotFromSuperframeBurstIndex(slot) & 0x01u]' in dec
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
