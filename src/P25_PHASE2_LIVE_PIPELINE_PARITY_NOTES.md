# P25 Phase 2 live pipeline parity patch

This patch targets the remaining field symptom where the waterfall shows the
traffic burst ending, but the decoder continues to process stale buffered windows
and/or starts audio late.

Changes:

1. Decode through retune/settle

The DSP worker no longer skips Phase 2 voice decode windows just because
`p25VoiceSettleUntilMs` is active.  The speaker remains muted during settle, but
PTT/MAC/ESS/superframe/mask evidence is still fed to `P25LiveDecoder` from the
start of the traffic burst.  This mirrors sdrtrunk more closely: traffic-channel
state is decoded immediately, while audio release is gated separately.

2. Mutex-owned decoder handoff

`armP25VoiceFollowState()` now holds `rx.dspMutex` before replacing
`rx.p25VoiceLiveDecoder`.  The previous try-lock soft-start path could replace
that object while the DSP worker was using it, which could drop mask/ESS state
or leave stale decoder tails running.

3. Per-timeslot Phase 2 session state inside decode windows

A Phase 2 RF channel carries both logical TDMA traffic slots.  The decoder now
keeps separate session state for the two logical slots during a detailed Phase 2
decode pass, selected by `phase2TrafficSlotFromSuperframeBurstIndex()`.  This
prevents TS1 MAC/ESS from polluting TS2 and vice versa inside a 12-burst
superframe.

4. Prefix/tail offset normalization

`processPhase2HardDibitsDetailedInternal()` may prepend a decoder tail prefix so
bursts crossing a window boundary can still be found.  Bursts/codeword offsets
are now normalized back to the caller's fresh input before they leave the
function.  `annotatePhase2SessionCodewords()` also no longer double-counts
`burst.dibitOffset`, because `P25Phase2VoiceCodeword::dibitOffset` is already an
absolute offset within the input window.

Regression:

- `src/tools/verify_p25_phase2_decode_through_settle_and_offset.py`
- existing sdrtrunk/security/timeslot/slot-probe/audio-gate verifiers
- `g++ -std=c++20 -Iinclude -fsyntax-only src/P25LiveDecoder.cpp`
