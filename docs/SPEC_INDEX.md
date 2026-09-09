# Specification index

We do not invent P25 bit layouts or security policy. If a constant is not in
this list (or a DEC that cites it), it does not go in the tree.

TIA-102 PDFs are not in this clone (ISS-0006). Until they are, cited
implementations under `_codex_refs/` plus field captures are the anti-guess
list. If two references disagree, open a DEC — do not average.

| ID | Document | What we take from it | Used by |
|---|---|---|---|
| SDRTrunk CenterFrequencyCalculator | GitHub `DSheirer/sdrtrunk` `source/tuner/manager/CenterFrequencyCalculator.java` | Single channel: minFreq − dcHalf + 1. Two-channel span ≤ usableHalf: minFreq − dcHalf. | DEC-0015, `P25SdrtrunkTune.h` |
| SDRTrunk R8xEmbeddedTuner | GitHub `source/tuner/rtl/r8x/R8xEmbeddedTuner.java` | DC_SPIKE_AVOID_BUFFER=5000, USABLE_BANDWIDTH_PERCENT=0.98 | DEC-0015 |
| SDRTrunk DecodeConfigP25Phase2 | GitHub `module/decode/p25/phase2/DecodeConfigP25Phase2.java` | ChannelSpecification bandwidth 12500 Hz | DEC-0015 |
| SDRTrunk P25P2AudioModule | `_codex_refs/sdrtrunk` audio module | Per-timeslot module; queue until PTT/ESS once; play until squelch; no opposite mix | DEC-0003, REQ-P2.2 |
| SDRTrunk Voice2/Voice4Timeslot | `_codex_refs/sdrtrunk` phase2 | Voice bit offsets 2/76/172/246 → dibit 1/38/86/123 after 20-dibit ISCH | P25LiveDecoder |
| SDRTrunk ScramblingSequence | `_codex_refs/sdrtrunk` | XOR mask LFSR from NAC/WACN/SysID; 4320-bit superframe | mask generation |
| SDRTrunk P25P2DecoderHDQPSK | `_codex_refs/sdrtrunk` | 6000 baud, LPF pass 6500 / stop 7200, no RRC; sticky Costas+Gardner across chunks | `kP25Phase2HdqpskPassHz`, DEC-0033/0038 |
| SDRTrunk P25P2MessageFramer | `_codex_refs/sdrtrunk` | Continuous dibit framer for channel life | DEC-0033 persistent framer |
| OP25 multi_rx / costas | `_codex_refs/op25` | Hop resets frame sync, not Costas | DEC-0033 |
| OP25 gr-op25_repeater | `_codex_refs/op25` | Cross-check burst/ISCH; do not treat OP25 dibit starts 11/48/96/133 as a second default (same absolute bits, different pointer) | P25LiveDecoder comments |
| mbelib AMBE 3600×2450 | `external/mbelib` | Vocoder for clear frames only | feed/emit |
| Voicetest continuousOk | `src/main.cpp` | duty ≥ 0.65 + cadence/sequencer/AMBE/concealment | SoT L3, DEC-0002, DEC-0004 |
| Late-entry strong target VCW | `kP25Phase2LateEntryStrongTargetVoiceCodewords` = 8 | Extract floor scaled per second in DEC-0002 | REQ-P2.0 |
| Baseline 20260810 | `docs/P25_BASELINE_CLEAR_CONTINUOUS_20260810.md` | Isolation PASS / continuity PARTIAL | SoT historical marker |
| Capture 20260808_034136 | regression tracker | Sticky ESS + dual-slot MAC-dead garble | DEC-0003 forbidden path |
| Capture 20260908_041716 | AppData IQ + p25_log | Post-emit mixed MAC-dead `ess=clear` still `gate=emit` (seq=134) | DEC-0012 |
| Capture 20260908_053241 | AppData IQ + p25_log | Dual-TG same RF; short emit islands under hop-wide DEC-0012 + drop D | DEC-0012 / T-0010 |
| Capture 20260908_060221 | AppData IQ + p25_log | Same DEC-0012 + drop D; file peak duty 0.922; DEC-0019 hard hint stop | DEC-0019 / T-0010 |
| Capture 20260908_095936 | AppData IQ + p25_log | Soft-trim 80 ms → 160 ms eyes; DEC-0023 protect 280 ms | DEC-0023 / T-0010 |
| Capture 20260908_101644 | AppData IQ + p25_log | Soft-trim OK; start/middle 40 ms catch-up eyes; late 10330 280 ms | DEC-0024 / T-0010 |
| Capture 20260908_103955 | AppData IQ + p25_log | DEC-0024 eyes OK; RID RF split file 0.46 vs 0.83; false ReturnEncrypted | DEC-0025 / T-0010 |
| Capture 20260908_110146 | AppData IQ + p25_log | File continuous vs live islands; opp-slot cold CQPSK escalate | DEC-0026 / T-0010 |
| Capture 20260908_112922 | AppData IQ + p25_log | Peak duty 0.639 drop D; structureNoVcw cold 462 ms | DEC-0027 / T-0010 |
| Capture 20260909_100909 | AppData IQ + p25_log | DEC-0036 chirps; max duty 0.40 | DEC-0037 / T-0010 |
| Capture 20260909_095846 | AppData IQ + p25_log | Clear duty 0.947; start unknown hold silence | DEC-0036/0037 / T-0010 |
| Capture 20260909_094846 | AppData IQ + p25_log | Live A after emit; same IQ file duty 0.43 | DEC-0035 / T-0010 |
| Capture 20260909_092250 | AppData IQ + p25_log | One emit then all-A; clearBlock hint wipe | DEC-0034 / T-0010 |
| Capture 20260909_083254 | AppData IQ + p25_log | DEC-0032 80+280 held; emit then drop A; ForceMask 0 | DEC-0033 / T-0010 |
| Capture 20260909_081701 | AppData IQ + p25_log | Single emit then no-voice; post-emit fresh=120 ms | DEC-0032 / T-0010 |
| Capture 20260909_062006 | AppData IQ + p25_log | LO OK; rolling 4 s; live A after emit; file duty 0.46 dual-slot | DEC-0031 / T-0010 |
| Capture 20260909_060036 | AppData IQ + p25_log | Live rolling stuck 4194304 after emit; file duty 0.705 | DEC-0030 / T-0010 |
| Capture 20260909_053448 | AppData IQ + p25_log | Sparse islands; clearTrusted quiet-return +5s; cold re-arm | DEC-0029 / T-0010 |
| Capture 20260908_115603 | AppData IQ + p25_log | Perfect first emit then silence; post-emit emptyStreak cold | DEC-0028 / T-0010 |
| Capture 20260903_040719 | README / planner comments | 180 ms speaker catch-up raised dups + bridge fill | ISS-0003 |

Analog demod, Qt, SoapySDR, miniaudio, and updater crypto are out of the
active Phase 2 index until a DEC makes them the REQ.
