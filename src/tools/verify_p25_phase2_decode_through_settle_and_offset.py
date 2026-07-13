from pathlib import Path
main = Path('src/main.cpp').read_text(errors='ignore')
dec = Path('src/P25LiveDecoder.cpp').read_text(errors='ignore')
assert 'Decode through settle' in main or 'Do not skip Phase-2 decode during retune/settle' in main
assert 'p25VoiceOutputMutedForSettle' in main
assert 'p25VoiceSettleUntilMs > nowMs' in main
assert 'mgr.setReceiverCursorToLiveEdge(i, rx);\n                            }\n                        }' not in main
assert 'mgr.setReceiverCursorToLiveEdge(di, rx);\n                        }\n                    }' not in main
assert 'Never assign rx.p25VoiceLiveDecoder unless rx.dspMutex is held' not in main or 'std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex);' in main
assert 'std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex);' in main
assert 'Do not add burst.dibitOffset again' in dec
assert 'const uint64_t streamDibit = streamStart +\n                static_cast<uint64_t>(codeword.dibitOffset);' in dec
assert 'Normalize offsets back to the caller\'s fresh input' in dec
assert 'annotatePhase2SessionCodewords(out, dibits);' in dec
assert 'slotSessions[phase2TrafficSlotFromSuperframeBurstIndex(slot) & 0x01u]' in dec
assert 'trafficRx->p25AfcFrozen = source.retunesPrimary && inheritedControlAfcKnown && !phase2Traffic' in main
assert 'Phase-2 traffic must start at the granted voice RF' in main
print('P25 Phase 2 decode-through-settle/offset/session regression: PASS')
