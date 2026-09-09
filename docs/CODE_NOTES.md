# Code notes (tree map)

If a P25 file's purpose is not in this table, the map is wrong — fix the map
in the same commit. Analog/GUI modules are listed at coarse grain until they
become the active REQ.

| Path | REQ | Spec / cite | Notes |
|---|---|---|---|
| `SOURCE_OF_TRUTH.md` | — | law | Product law L1–L8 |
| `CAUSE_EFFECT_MAP.md` | — | gates | A–E buckets; REQ-P2.* |
| `DEVELOPMENT_RULES.md` | — | method | Athanor process, not sovereignty law |
| `src/main.cpp` | P2.* | DEC-0002/0003/0012/0014/0015/0018/0019/0023/0024/0025/0026/0027/0028/0032/0033/0034/0035/0036/0037/0038 | Orchestration mega-file (ISS-0004): chunk plan, feed/emit gates, CADENCE, voicetest, GUI. Independent Phase 2 traffic stays block-channelize by default; streaming DDC is env opt-in after DEC-0014 measured 105622 duty 0.685→0.095. DEC-0038: sticky Gardner under stream still duty 0.23 on 060036 — default-on still off. DEC-0018: env=1 must not map IQ-sample time onto the dibit lattice. DEC-0017/0020/0022 hop-shrink trials rejected. DEC-0012 companion-louder mixed MAC-dead. DEC-0019 hard CQPSK hint stop; hot cand stays 8. DEC-0023: rolling soft-trim protects 280 ms (not 80 ms); active rolling 4 s; hard-cap must not jump decode cursor over undecoded RF (095936). DEC-0024: backlog catch-up overlap 280 ms not 40 ms (101644 start-middle). DEC-0025: Clear→Encrypted / grantEncrypted needs MAC/PTT bar (103955 false ReturnEncrypted). DEC-0026/0027: narrowed post-emit cold escalate; **DEC-0028 removes it** (115603 perfect-then-silence). **DEC-0029:** coldAcquire exits on sf+mask≥4; same-call unknown grant preserves clearKnown (053448). **DEC-0031:** once-clear continuation without requireFedAudio chicken-egg (062006); planner catch-up-before-sustain **superseded by DEC-0032**. **DEC-0032:** keep DEC-0009 80+280 after speak; post-emit empty-eye soft ForceMask rehunt (081701). **DEC-0034:** do **not** clearBlockCqpskHint on empty eyes (092250). **DEC-0035:** live eye-lost after speak uses replay cand=16/240 (094846 live vs file split); healthy eye keeps cand=8. **DEC-0033:** GUI replay context=0 when streaming DDC; voicetest prints cqpsk/framer/demodState. Soft mask rehunt remains. Do not add more gates without a named bucket. |
| `include/P25SdrtrunkTune.h` | follow | DEC-0015/0016 / SDRTrunk CenterFrequencyCalculator | Follow LO is single-channel voice park (voice−11249). Two-channel set calculator is citation only — 115315 997 kHz edge. |
| `include/P25AudioDropClass.h` `src/P25AudioDropClass.cpp` | REQ-P2.0 | DEC-0002 | Pure A–E classifier from CADENCE/voicetest counters |
| `include/P25LiveDecoder.h` `src/P25LiveDecoder.cpp` | P2.1 | SDRTrunk HDQPSK / Voice2/4 / DEC-0033/0034/0038 | IQ → dibits → superframe/ISCH/XOR/MAC/ESS/Voice2/4. Streaming: persistent framer commit when anchor known; sticky Gardner during unlocked search (DEC-0038); companion-only sticky fallthrough **streaming-gated only**. |
| `include/dsp/P25Phase2Framer.h` `src/dsp/P25Phase2Framer.cpp` | P2.1 | OP25/SDRTrunk streaming framer | 180-dibit burst / 720-dibit superframe |
| `src/dsp/P25StreamingChannelDdc.cpp` | P2.1 | DEC-0014/0018 / SDRTrunk HDQPSK | Stateful DDC; overlap must be 0 when enabled. Opt-in via `SDR_TOWN_P25_STREAMING_DDC=1`. DEC-0018: do not jump the dibit lattice to RF-sample time (105622 env=1 duty 0.095). Locked hops 80 ms. |
| `src/dsp/P25CqpskStagedScorer.cpp` | P2.1 | — | Cheap CQPSK pre-filter |
| `src/P25Control.cpp` `include/P25Control.h` | follow | TSBK / Phase 2 MAC grants | TG, Hz, slot, enc. Does not open speaker alone |
| `src/P25FollowStateMachine.cpp` `include/P25FollowStateMachine.h` | P2.5 | DEC-0029 | Stay vs return-to-CC; slot probe. Clear-trusted: 40s speaker grace without live VCW + 15s activity silence (053448 quiet-return thrash). Pure policy |
| `src/P25TrafficChannelProcessor.cpp` `include/P25TrafficChannelProcessor.h` | P2.5 | — | Observational call-active / audioOpen |
| `include/P25ReceiverSession.h` | P2.2 | — | Per-RX key, pending AMBE, abs-dedupe, latch |
| `src/AudioEngine.cpp` `include/AudioEngine.h` | P2.4 | miniaudio | Ring, underrun, digital-voice jitter cap |
| `src/DeviceManager.cpp` `include/DeviceManager.h` | follow | Soapy/RTL | IQ stream + one-RTL retune |
| `src/Demod.cpp` `include/Demod.h` | analog | — | WFM/AM/NFM/SSB; keep stable |
| `src/Receiver.cpp` `include/Receiver.h` | — | — | Logical channel; owns P25 session state |
| `src/SpectrumWidget.cpp` | GUI | — | FFT/waterfall |
| `external/mbelib/` | P2.2 | AMBE 3600×2450 | Vocoder when `SDR_TOWN_ENABLE_MBELIB` |
| `_codex_refs/sdrtrunk/` | cite | DEC-0003 | Reference implementation, not linked |
| `_codex_refs/op25/` | cite | SPEC_INDEX | Reference implementation, not linked |
| `src/P25TxSession.cpp` `src/P25AmbeEncoder.cpp` `src/P25Phase2TxFramer.cpp` | TX later | — | Lab TX shells; not the RX continuity REQ |
| `src/tools/p25_capture_audit.py` | P2.0 | field logs | Forensic counts; not a SoT checkbox |
| `src/tools/verify_p25_phase2_*.py` | ISS-0002 | string locks | Invariant guards only |
| `tests/test_p25follow.cpp` | P2.5 | — | Return/hold policy units |
| `tests/test_p25_audio_drop_class.cpp` | P2.0 | DEC-0002 | Classifier vectors |
| `tests/test_p25_sdrtrunk_tune.cpp` | follow | DEC-0015 | CenterFrequencyCalculator numbers (voice−11249; 095450 pair) |
