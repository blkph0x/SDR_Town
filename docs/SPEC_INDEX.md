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
| SDRTrunk P25P2DecoderHDQPSK | `_codex_refs/sdrtrunk` | 6000 baud, LPF pass 6500 / stop 7200, no RRC | `kP25Phase2HdqpskPassHz` |
| OP25 gr-op25_repeater | `_codex_refs/op25` | Cross-check burst/ISCH; do not treat OP25 dibit starts 11/48/96/133 as a second default (same absolute bits, different pointer) | P25LiveDecoder comments |
| mbelib AMBE 3600×2450 | `external/mbelib` | Vocoder for clear frames only | feed/emit |
| Voicetest continuousOk | `src/main.cpp` | duty ≥ 0.65 + cadence/sequencer/AMBE/concealment | SoT L3, DEC-0002, DEC-0004 |
| Late-entry strong target VCW | `kP25Phase2LateEntryStrongTargetVoiceCodewords` = 8 | Extract floor scaled per second in DEC-0002 | REQ-P2.0 |
| Baseline 20260810 | `docs/P25_BASELINE_CLEAR_CONTINUOUS_20260810.md` | Isolation PASS / continuity PARTIAL | SoT historical marker |
| Capture 20260808_034136 | regression tracker | Sticky ESS + dual-slot MAC-dead garble | DEC-0003 forbidden path |
| Capture 20260908_041716 | AppData IQ + p25_log | Post-emit mixed MAC-dead `ess=clear` still `gate=emit` (seq=134) | DEC-0012 |
| Capture 20260903_040719 | README / planner comments | 180 ms speaker catch-up raised dups + bridge fill | ISS-0003 |

Analog demod, Qt, SoapySDR, miniaudio, and updater crypto are out of the
active Phase 2 index until a DEC makes them the REQ.
