# P25 / sdrtrunk parity review

This review targets the current observed failure: a clear Phase 2 traffic call is followed and the receiver reports full superframe/mask/VCW evidence, but AMBE output stays near zero.

## Fixed in this patch

1. **I-ISCH mask-phase anchoring clarified and guarded.**
   In this codebase `P25Phase2IschState::sync` means a raw S-ISCH/sync word, not decoded I-ISCH.  Decoded I-ISCH is `valid && !sync` and carries `location/channel`.  The mask phase scorer now explicitly anchors to decoded I-ISCH only.  This matches sdrtrunk's `SuperFrameFragment`/`ISCHSequence` model where I-ISCH gives timeslot offset 0/4/8 for fragments 1/3, 2/3, 3/3.

2. **Trusted clear grant slot no longer auto-flips on full epoch lock.**
   sdrtrunk binds a traffic channel/timeslot from the control-channel allocation and processes both TDMA timeslots on the RF channel.  Opposite-slot VCWs are expected.  The slot probe now refuses to override a trusted clear grant when the traffic channel has a stable superframe/mask epoch.  If audio is still bad in that state, the problem is mask/AMBE/MAC/ESS extraction, not the grant slot.

3. **AMBE codec remains owner of correction/erasure handling.**
   The hard `totalErrors <= 24` post-vocoder gate stays removed.  We only reject non-finite, runaway, or pathological-silence PCM.

4. **Validation logging remains available.**
   Run with `SDR_TOWN_P25_VALIDATION_LOG=1` to write `p25_phase2_validation.jsonl`, including per-burst mask phase, I-ISCH, DUID, AMBE bits, mbelib errors, PCM peak/RMS, and accepted/drop reason.

## Highest-priority remaining parity gaps

- **P0: AMBE frame bit-order validation.**  If `p2sf=12 p2mask=12 p2vcw=20+ decoded=0` remains after this patch, the next change should add a controlled AMBE bit-order hypothesis test or compare `ambeBits` against a known-good capture.  The RF/follow side is already good in that state.
- **P0: MAC/ESS extraction reliability.**  Current logs often show `p2mac=0/0` with full mask lock.  sdrtrunk's audio module queues voice until PTT MAC or ESS establishes encryption state; our equivalent is spread across the live decoder and main follow path.
- **P1: Typed Phase 2 pipeline.**  Split local code into sdrtrunk-like `SuperFrameFragment`, `Timeslot`, `Voice2Timeslot`, `Voice4Timeslot`, and `P25P2AudioModule` units.  This will make future offset/slot bugs harder to introduce.
- **P1: IQ replay fixtures.**  Add golden replay snippets for known-clear P25P2 calls, encrypted calls, and late-entry calls.
- **P2: Motorola regroup/patch metadata.**  Parsing exists, but event handling/metadata should be aligned with sdrtrunk's current P25 patch-group changes.
- **P2: User-visible no-audio reasons.**  Surface exact reasons: encrypted, no grant, no superframe, mask phase unknown, MAC/ESS unknown, vocoder no frames, wrong slot suppressed by trusted grant, etc.
