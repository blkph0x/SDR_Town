# Decisions

Format: ID, date, status, evidence, decision, consequences.
A decision is recorded **before** code that depends on it is written.

---

## DEC-0001 — Copy Athanor method, not Athanor product law

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Operator asked to apply SovereignFoundry / Athanor rules and
  methodology to `maulaudio_pro` and not to edit Athanor. SDR Town already
  depends on Qt 6, SoapySDR, mbelib, miniaudio, spdlog, nlohmann/json.
- **Decision:** Binding process is `DEVELOPMENT_RULES.md` (never guess,
  trackers, comments cite evidence, definition of done). Product law is
  `SOURCE_OF_TRUTH.md` for this receiver. Zero-external-library / Knox /
  ML-KEM rules stay in Athanor only.
- **Consequences:** New third-party deps still need a DEC. Existing deps stay.

---

## DEC-0002 — Classify every Phase 2 audio hole as A–E before changing gates

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** `docs/p25_phase2_regression_tracker.md` and README v0.2.50
  show months of contradictory hotfixes (open security → garble; close →
  silence; overlap decode → dup starve; bridge → fake continuity).
  CADENCE already counts `targetVcw`, `fed`, `emit`, `dups`, `dutySec`.
  Voicetest already defines continuous as duty ≥ 0.65 (`src/main.cpp`).
  Late-entry strong selected VCW bar already exists as 8 codewords
  (`kP25Phase2LateEntryStrongTargetVoiceCodewords`).
- **Decision:** Dominant drop bucket is computed by `classifyP25AudioDrop`
  (earliest pipeline stage wins):

  | Bucket | Label | Rule (scaled by `windowSeconds`) |
  |---|---|---|
  | E | follow | `followReturned` |
  | A | extract | unique selected VCW (`max(0, targetVcw − dups)`) below 8 per second, and emit below the continuous bar |
  | B | feed | unique ≥ 8/s and `fed == 0` |
  | C | emit | `fed > 0` and `emittedPcm == 0` |
  | D | playout | `emittedPcm > 0` and duty (`emittedPcm * 0.020 / windowSeconds`) < 0.65 |
  | ok | none | unique ≥ 8/s, fed>0, duty ≥ 0.65. Idle all-zeros is A, not ok. |

  CADENCE (1 s) and `p25 voicetest` print `drop=`. No timeout/gate/hop
  change until ISS-0001 cites a run.
- **Consequences:** T-0003…T-0007 stay blocked until a capture names a bucket.
  8/s cites the existing late-entry constant, not a new magic number.
  0.65 cites voicetest `continuousOk`.

---

## DEC-0003 — Selected-slot speaker policy follows SDRTrunk P25P2AudioModule

- **Date:** 2026-09-07
- **Status:** accepted (policy; code changes wait for measured bucket B or C)
- **Evidence:** SDRTrunk: one AudioModule per timeslot; queue Voice2/4 until
  PTT/ESS establishes clear/enc once; play that slot until squelch; never mix
  opposite slot. Our dual-slot MAC-dead mute (beeaea8 / 034136) stopped
  wrong-epoch garble and also punched holes in already-proven selected-slot
  audio (004206 duty drop). Sticky ESS alone is not epoch proof (034136).
- **Decision:** When we touch feed/emit (REQ-P2.2 / P2.3):
  1. Companion slot never feeds the speaker mbelib instance.
  2. Selected slot: after **this slot** has clear proof (this-window MAC CRC
     or this-window ESS clear on that slot, or MAC_PTT/ACTIVE clear), keep
     feeding descrambled Voice2/4 until END/HANG/squelch/encrypted.
  3. Dual-slot MAC-dead mutes **unproven** frames and pending drain. It does
     not mute selected-slot frames that already have this-slot proof for the
     current call key.
  4. Sticky grant-clear / latch without this-slot proof must not open dual-slot
     MAC-dead windows (034136 stays forbidden).
- **Consequences:** No feed-gate patch until REQ-P2.0. Isolation (SoT L1–L2, L7)
  outranks duty.

---

## DEC-0004 — PASS_PARTIAL_AUDIO is not done

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Voicetest distinguishes `PASS_PARTIAL_AUDIO` vs
  `PASS_CONTINUOUS_AUDIO`. Roadmap had marked continuous “done”. Field ~50%.
- **Decision:** SoT L3. Roadmap item 1 is open. String verifiers are not the
  gate.
- **Consequences:** ISS-0005 closed by retracting the roadmap claim.

---

## DEC-0005 — Do not invent PLC to hide missing Voice2/4

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** SoT L5. Prior silence-bridge work was an underrun guard
  (`p25Phase2PlayoutBridgeAllowed`, max ~4.5 s). Users heard blocky/partial
  speech, not a missing 20 ms click.
- **Decision:** No dummy speech PCM. Bridge may only fill true ring underrun
  on an already-open clear call. Concealment stays mbelib repeat/erasure
  already counted in voicetest (`concealmentOk` ≤ 25%).
- **Consequences:** REQ-P2.4 must raise unique emit rate, not lengthen bridge.

---

## DEC-0006 — Overlapping traffic eyes rewind the dibit stream cursor

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** `alignPhase2AbsoluteDibitCursor` comment already required the
  cursor to be the start of the current chunk. Contiguous and gap/rewind
  paths did that; overlapping lookback (chunkStart < previous end < chunkEnd)
  did not. Voicetest 20260905_105622 TG 30003 slot 0 skip=97334: hop 1
  0–720 ms then hop 2 160–880 ms then 80 ms sustain eyes with 80 ms context.
  Cursor stayed at the previous end; sticky lattice walk decoded 80–560 ms
  off the bursts. Hop lines: p2sf/p2mask high with p2vcw=0 or
  `wrong TDMA slot` islands; `PASS_PARTIAL_AUDIO drop=D duty=0.28`
  (ISS-0001, REQ-P2.1).
- **Decision:** If the new chunk overlaps the previous stream, set
  `m_phase2StreamDibits = chunkStartAbsolute` without clearing sticky
  epoch/tail. True ring-reset rewind (whole chunk behind the cursor by more
  than one burst) still clears. Sticky walk covers every complete timeslot
  in the working window (no 540 ms cap on a 720 ms cold eye).
- **Consequences:** Overlap Voice2/4 is de-duped by existing abs-dibit
  session IDs, not by leaving the cursor past the IQ. Not a hop/TTL tweak.

---

## DEC-0007 — Context IQ must not discard never-emitted selected Voice2/4

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** After DEC-0006, voicetest 105622 slot 0 duty fell 0.28→0.125.
  Hop startMs=98294: `targetVcw=2 ctxDrop=2 fed=0 dup=0` — unique selected
  frames a prior 80 ms hop missed (`p2vcw=0`) were hard-dropped because they
  sat before `freshStart − 240 dibits`. Abs-dibit `ShouldEmit` never ran.
- **Decision:** Pre-fresh Voice2/4 may be suppressed only when
  `p25Phase2ShouldEmitAmbeFrame` says they were already emitted (or 12-dibit
  start match). Do not hard-continue on `codewordIsContextOnly`.
- **Consequences:** Overlap still cannot replay (ShouldEmit / lastAbs). Missed
  RF in the lookback can reach mbelib after the call is proven clear.

---

## DEC-0008 — Sticky superframe lattice is contiguous-dibit only

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Voicetest 20260905_105622 TG 30003 slot 0 skip=97334 8 s
  (HEAD with DEC-0006/0007): hop 2 (720 ms block eye) `targetVcw=24 fed=30`;
  hop 3+ 160 ms sustain `p2sf/p2mask` with `p2vcw=0` or
  `Phase 2 wrong TDMA slot`; `PASS_PARTIAL_AUDIO drop=D duty=0.28`.
  Block-channelize clears CQPSK/Gardner/framer every hop
  (`20260729_114627`) then the sticky stream-space walk / anchor-aligned
  lock reused the previous eye's dibit lattice. Slot labels and Voice2/4
  DUIDs followed that stale grid. Streaming DDC keeps a real contiguous
  dibit stream; that path may keep the lattice.
- **Decision:** `findPhase2AnchorAlignedSuperframeLocks`, the hot sticky
  burst walk (early return), and the continuous sticky walk run only when
  `enableStreamingChannelDdc` is true. Block-channelize hops re-lock the
  superframe on this window's syncs. Sticky XOR mask phase stays.
- **Consequences:** Default CLI/GUI traffic (streaming DDC opt-in) extracts
  from each independent eye. Not a hop/TTL/minFresh change. Isolation
  unchanged (grant slot still authoritative).

---

## DEC-0009 — Locked block-channelize sustain eye is one superframe

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** After DEC-0008, voicetest 105622 slot 0 skip=97334 8 s duty
  0.40; 5 s talking slice duty 0.51 (`fed=128`). Sustain hops used 80 ms
  overlap → 160 ms eyes; many `p2bursts=1`. Hop 2 (720 ms) still recovered
  24 selected VCWs. A Phase 2 superframe is 12 × 30 ms = 360 ms (SDRTrunk
  SuperFrameFragment). Hop/minFresh stay 80/40 ms (not a TTL tweak).
- **Decision:** `kP25Phase2VoiceDecodeSustainOverlapSeconds` and
  `kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds` are 0.280 so
  overlap+fresh = 360 ms. Streaming DDC overlap remains 0.
- **Consequences:** More IQ per live tick than 160 ms eyes; still half the
  720 ms cold eye. Abs-dibit de-dupe still owns overlap.

---

## DEC-0010 — Deep overlap is lock-only after first emit

- **Date:** 2026-09-07
- **Status:** superseded by DEC-0013 (105622 duty 0.735→0.35; missed-eye catch-up needed)
- **Evidence:** Live capture `20260907_073304` (146 s gapless IQ). Operator:
  some audio better, still garbled + repeats.
  Repeats: TG 10330 slot 1 seq=386 emit=18 then seq=389 ctxVcw=10 ctxDrop=0
  (200 ms already-played overlap pushed again). CADENCE 17:34:54–58:
  dups=78–100/s, feedRatio=0.29, dutySec=0.72–0.92. Independent CQPSK eyes
  (DEC-0008) shift recovered abs by more than the 12-dibit ShouldEmit wobble,
  so DEC-0007's "context is playable if not an abs duplicate" replays speech.
  Tried also muting mixed MAC-dead continuation (TG 30302 seq=9 opp=16
  p2mac=0/2 gate=emit): 105622 voicetest duty 0.735→0.35. Reverted that
  half (T-0004 stays open).
- **Decision:** After `lastAbs != 0` or `hadSuccessfulEmit`, Voice2/4 that end
  more than one sustain hop (80 ms / 480 dibits) before `freshStart` are
  lock-only. The last hop of lookback still uses ShouldEmit (DEC-0007
  missed-eye). Dual-slot continuation escape is unchanged.
- **Consequences:** Overlap stays 280 ms for superframe lock (DEC-0009). Not a
  hop/TTL change. Mixed MAC-dead garble remains T-0004 / DEC-0012. Last-hop
  80 ms leak on independent eyes is DEC-0011.

---

## DEC-0011 — Block-channelize post-emit context is all lock-only

- **Date:** 2026-09-07
- **Status:** superseded by DEC-0013 (105622 duty 0.735→0.11 with DEC-0011)
- **Evidence:** Capture `20260907_073304` TG 10330 slot 1 after DEC-0010's
  480-dibit floor: seq=401 `ctxVcw=4 ctxDrop=0` (exactly one 80 ms hop of
  lookback still replayed). Independent CQPSK eyes shift recovered abs past
  the 12-dibit ShouldEmit wobble, so those four frames are treated as new.
  Same capture later TG 30302 CADENCE `dutySec=1.28–1.36` with `dups=0`.
- **Decision:** When streaming DDC is off (default live/voicetest block
  eyes), after `lastAbs != 0` or `hadSuccessfulEmit`, every Voice2/4 that
  ends before `freshStart` is lock-only. Streaming DDC keeps the last-hop
  ShouldEmit catch-up (contiguous abs, DEC-0007).
- **Consequences:** Overlap remains 280 ms for lock/MAC/ESS. Not a hop/TTL
  change. First-emit missed-eye catch-up unchanged (`lastAbs == 0`).

---

## DEC-0012 — Post-emit mixed MAC-dead mutes feed, not the hop PCM

- **Date:** 2026-09-07
- **Status:** accepted (feed-only; 2026-09-08)
- **Evidence:** Capture `20260907_073304` TG 30302 seq=9 `oppVcw=16 p2mac=0/2`
  `gate=emit` `action=explicit-clear-grant-traffic-clear-release` after a
  clean seq=1 `opp=0 p2mac=11/13`. Capture `20260908_041716` TG 10330 slot 1
  seq=131 `opp=0 p2mac=5/6` `trusted-clear-pending-release` then seq=134
  `targetVcw=6 oppVcw=12 fed=4 emit=4 p2mac=0/0 ess=clear`
  `explicit-clear-grant-traffic-clear-release`, then `Phase 2 wrong TDMA slot`.
  Operator: one good emit, then garbled wrong-slot audio. Call 1 on the same
  file already mixed-emitted (seq=27/36/47 `opp=8 p2mac=0/x ess=clear`).
  `DualSlotUntrustedGarbleWindow` stayed false because this-window ESS clear
  + companion accounted + strong selected (overlap Voice ESS / 004206 escape).
  Muting via that helper + `audio.clear()` dropped 105622 duty 0.735→0.38
  because it discarded already-proven PCM in the same hop.
- **Decision:** After the call has already spoken (`hadSuccessfulEmit` or
  sticky `p25Phase2CallHadSpeakerAudio`), if both slots have voice, this
  window has no selected MAC CRC, **and the companion is louder**
  (`oppVcw > targetVcw`), do not feed bursts that themselves lack MAC CRC.
  Same-call continuation and this-window ESS clear must not escape that
  skip. Do not brand the hop as `dual-slot-untrusted-garble-drop` and do
  not `audio.clear()` proven PCM. Selected-dominant mixed ESS
  (`targetVcw >= oppVcw`) still feeds (105622 startMs=99134 target=18
  opp=8; 004206). This-window selected MAC CRC still authorizes
  companion-louder hops (105622 startMs=100254 slot0Mac=2). Selected-only
  MAC-dead (`oppVcw=0`) still feeds.
- **Consequences:** Blanket mixed-MAC-dead mute after emit dropped 105622
  duty 0.685→0.27 (too many selected-dominant ESS hops). Companion-louder
  is the 041716 seq=134 / wrong-slot pattern. T-0004 first-hop
  `target=6 opp=28 fed=0` stays a separate cold-eye case. Equal mixed
  (041716 call 1 seq=27 target=8 opp=8) is still ESS-authorized.

---

## DEC-0013 — Overlap replay uses ISCH lattice identity, not lock-only

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** DEC-0010/0011 lock-only after emit dropped 105622 TG 30003
  slot 0 skip=97334 duty 0.735→0.35→0.11 (missed-eye catch-up, DEC-0007).
  Live 073304 TG 10330 seq=389/401 still replayed overlap because recovered
  abs moved more than the 12-dibit ShouldEmit wobble (DEC-0008 independent
  eyes). `Phase2VoiceFrameKey` equality includes that jittered abs / window
  local superframe anchor, so sequencer recentKeys also miss.
- **Decision:** After a selected-slot Voice2/4 is actually fed, remember
  `(slot, superframeBurstIndex, voiceIndex)` plus recovered abs. A later hop
  with the same lattice identity inside 1800 dibits (~300 ms, overlap 280 ms,
  superframe 2160 dibits) is a replay and is not fed. Next superframe keeps
  the index but is ≥2160 dibits later. Context Voice2/4 that were never
  emitted still use ShouldEmit only (DEC-0007). Mixed MAC-dead feed mute
  (DEC-0012) is feed-only after the call has spoken; do not hop-PCM clear.
- **Consequences:** Not a hop/TTL/overlap change. Requires ISCH superframe
  lock (`burstIndex < 12`); unlocked bursts still use 12-dibit abs.

---

## DEC-0014 — Independent Phase 2 traffic is SDRTrunk HDQPSK.receive

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - SDRTrunk `P25P2DecoderHDQPSK.receive(ComplexSamples)`: one Costas +
    Gardner + MessageFramer on a contiguous channelized stream; `main()`
    consumes 2048 samples at 25 kHz ≈ 81.92 ms. Two `P25P2AudioModule`s,
    `if (message.getTimeslot() == getTimeslot())`; queue Voice2/4 until
    PTT/ESS; PTT `clearPendingVoiceTimeslots()`; then 20 ms frames as they
    arrive. No overlapping independent CQPSK eyes.
  - Live capture `20260907_095450`: dual grant TG 30013 slot 1 + TG 30003
    slot 0 on 420.225 MHz. First eye `iq=1474560` (720 ms) `dsp=327 ms`
    `targetVcw=4 opp=0 p2mac=1/13`; seq=2 `decode-wall-timeout`. Later hops
    `fresh=80 ms context=160–280 ms` `dsp=159–573 ms` (wall 320 ms). seq=21
    `targetVcw=10 oppVcw=10 fed=10 emit=10 ctxDrop=0 p2mac=0/2` (200 ms PCM
    on an 80 ms hop = overlap replay + mixed-slot feed). First CADENCE
    `windows=593 dutySec=2.320` at 19:55:47 — 53 s of hops dumped as `1s`
    because `lastLogMs==0`.
  - Capture `20260830_064319` / `20260829`: auto streaming DDC into
    **20–40 ms** context-free slices on an unlocked eye → `p2bursts=0`.
    That was slice length, not the DDC. Cold eye stays 720 ms (DEC-0009).
- **Decision:**
  1. **Tried** independent realtime Phase 2 traffic enabling streaming DDC
     by default (SDRTrunk `receive`). Voicetest 105622 TG 30003 slot 0
     skip=97334 8 s fell `PASS_CONTINUOUS_AUDIO duty=0.685` →
     `PASS_PARTIAL_AUDIO drop=A duty=0.095` (`emptyWindows=80/89`,
     `targetVcw=56` vs 396). Same eye-loss as 20260830, not slice length
     alone — the first hop was already 720 ms. **Default stays
     block-channelize.** `SDR_TOWN_P25_STREAMING_DDC=1` still opts in.
     Do not hand off via `stickyReady`.
  2. When streaming DDC **is** on, locked hops are **80 ms** fresh-only
     (`overlap=0`), not 40 ms. Cite SDRTrunk 2048@25 kHz and 20260830.
  3. Hard reacquire `reset()`s Costas/DDC/framer. It must not flip an
     operator-opted-in stream back to block-channelize (`env=1` keeps DDC).
  4. CADENCE `1s` starts its bucket on the first diagnostic tick
     (`lastLogMs==0` discards, does not print) and uses elapsed time as
     `windowSeconds`. Capture 095450 `windows=593 dutySec=2.320` was a
     53 s backlog labeled as one second. Pending AMBE stays PTT-clear /
     ESS-drain (DEC-0003).
  5. DEC-0012 mixed MAC-dead is feed-only after emit (041716 seq=134).
     Slot bleed on 095450 (`targetVcw≈oppVcw`, `p2mac=0/x`, `ctxDrop=0`)
     remains independent-eye ISCH + overlap on the first hop (T-0004).
- **Consequences:** Live/voicetest default is still 80/40/280 block
  eyes (DEC-0009). CADENCE is honest. Streaming DDC remains the SDRTrunk
  stream experiment, not the proven continuous path on 105622.

---

## DEC-0015 — Tuner center is SDRTrunk CenterFrequencyCalculator

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - Operator: some follows look off-center.
  - Our Phase 2 follow parked `voice ± 250 kHz`
    (`kP25Phase2TrafficLowIfOffsetHz`) and reused a CC-centered tuner
    whenever `|voice−center| ≤ min(0.42*sr, max(250 kHz, 0.25*sr))`
    (~512 kHz at 2.048 Msps). Capture 095450 (CC 420.475, voice 420.225)
    therefore kept the tuner on the control channel: the grant sat 250 kHz
    off spectrum middle, and the CC channel overlapped the LO/DC spike.
    The 250 kHz offset was a local note (419.375 decoded from a 419.125
    ring, poorly when parked exactly on voice). That is “don’t sit on DC”,
    not SDRTrunk’s formula. Comment in `main.cpp` that claimed the 250 kHz
    park “matches SDRTrunk” was wrong.
  - SDRTrunk `CenterFrequencyCalculator.getCenterFrequency`
    (`source/tuner/manager/CenterFrequencyCalculator.java`), called from
    `HeterodyneChannelSourceManager.updateTunerFrequency` when
    `!tunerController.isTunedFor(channels)`:
    - One channel: `minFrequency − middleUnusableHalfBandwidth + 1`.
    - Two+ channels with span `≤ usableHalfBandwidth`:
      `minChannelFrequency − middleUnusableHalfBandwidth`.
    - `TunerChannel.getMinFrequency()` = `frequency − bandwidth/2`.
  - `DecodeConfigP25Phase2.getChannelSpecification`:
    `ChannelSpecification(50000.0, 12500, 6500.0, 7200.0)` → channel
    bandwidth **12500 Hz**.
  - R820T/R820T2 (typical RTL-SDR): `R8xEmbeddedTuner`
    `DC_SPIKE_AVOID_BUFFER = 5000`, `USABLE_BANDWIDTH_PERCENT = 0.98`.
    One traffic follow: `center = voice − 6250 − 5000 + 1` =
    **voice − 11249 Hz**. Wiki: do not park a decode channel on DC or on
    filter roll-off; recompute tuner center from the sourced set.
  - `TunerController.isTunedFor`: every channel inside
    `center ± usableBandwidth/2`, and none overlapping
    `[center − dcHalf, center + dcHalf]`.
- **Decision:**
  1. Replace the 250 kHz low-IF helper with SDRTrunk’s calculator
     (`include/P25SdrtrunkTune.h`). Keep the name
     `p25Phase2LowIfTrafficCenterHz` as a wrapper so existing string
     locks still see it.
  2. **Traffic-only** (CC paused, dedicated SDR, CLI waitgrant):
     single-channel center = `voiceMin − 5000 + 1`.
  3. **CC still live on the same tuner:** compute center for
     `{ccHz, voiceHz}` and **nudge** the tuner if `!isTunedFor`. Do not
     leave CC sitting on DC while DDC’ing traffic 250 kHz away. Do not
     pause CC for that nudge (`retunesPrimary` stays false).
  4. Reuse vs physical retune uses `canTune` (span ≤ usable bandwidth
     and a valid center exists), not the 250 kHz / 0.25*Nyquist quality
     clamp. Capture 005246 (750 kHz from a CC-at-DC tuner) was that
     geometry; after this calculator the lower channel sits just right
     of DC.
  5. No hop/TTL/feed-gate change. Do not re-enable default streaming DDC
     (DEC-0014).
- **Consequences:** Waterfall follows sit ~11 kHz right of center, not
  250 kHz off. File voicetest of 105622/073304 still uses capture
  geometry (`--center`), not this live LO. 095450 live should log
  `rfCenter≈420.21375` for CC 420.475 + voice 420.225 if both stay
  sourced. Isolation on 095450 dual-TG remains T-0004.
- **Superseded in part by DEC-0016:** two-channel `getCenterFrequency`
  on `{cc, voice}` is not the follow LO. It parks the *lower* channel
  off DC, which put 421.975 MHz at 997 kHz offset on capture 115315.

---

## DEC-0016 — Follow LO parks the granted voice, not the CC+voice set

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - Capture `20260907_115315` (162 s gapless, SNR ~17 dB). Operator:
    audio really bad. DEC-0015 two-channel calculator:
    - TG 30003 420.725 + CC 420.475 → `rfCenter=420.46375` **offset=261.3 kHz**
      (CC is the lower channel, so CC sits off DC and traffic is 261 kHz up).
    - TG 30003 421.975 + CC 420.475 → span 1.51 MHz ≤ usable 2.007 MHz so
      `canTune` was true. Set calculator then places highest at the high
      end: `rfCenter=420.97773` **offset=997.3 kHz**. Voice channel max
      equals `center + usableHalf`. SDRTrunk wiki: filter roll-off at
      spectral edges is unusable for decode. Capture 005246: 750 kHz
      offset left `p2vcw≈0`.
    - CADENCE after that hop: a few seconds `drop=D dutySec=0.22–0.45`,
      then `drop=A` `reject` high / `no-sf-mask` / `no-vcw-from-live-window`
      for the rest of TG 30003 and almost all of TG 30302 at 421.225
      (reused the 420.97773 LO, `kind=existing-wideband-source-low-if`).
  - SDRTrunk `CenterFrequencyCalculator` optimizes the *set* because
    every sourced channel is a first-class decode. This GUI has one
    selected speaker follow. The channel that must sit just right of
    the R820T DC hole is the **granted voice** (single-channel formula,
    DEC-0015). CC stays only if that same LO still `isTunedFor` CC.
- **Decision:**
  1. `p25SdrtrunkTunerCenterHz(voice)` is always the single-channel
     park (`voiceMin − 5000 + 1`). Do not pass CC into
     `getCenterFrequency` for the follow LO.
  2. Keep CC on the same tuner only when
     `isTunedFor(voiceParkCenter, {voice, cc})`. Otherwise one-RTL
     physical retune (pause CC) to that same voice park.
  3. Same-call hops use the voice park, not `{cc, voice}`.
  4. Two-channel `getCenterFrequency` stays in the header as the cited
     SDRTrunk routine; it is not the follow LO. 250 kHz stays gone.
  5. No hop/TTL/feed-gate change. Streaming DDC stays default-off.
- **Consequences:** 115315’s 421.975 follow would be `rfCenter≈421.96375`
  (offset ~11 kHz) with CC paused. 095450 voice 420.225 is still
  `≈420.21375` and CC still fits. File voicetest `--center` unchanged.

---

## DEC-0017 — Locked block-channelize hop is one superframe of fresh IQ

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - Capture `20260907_123525` live: 80+280 ms jobs (360 ms IQ / 80 ms hop
    = 4.5× CQPSK), `dsp=72–134 ms`, `worker-busy`, CADENCE duty **0.34**.
    File voicetest skip=20000: duty **0.64**.
  - Tried `chunk=minFresh=0.360 overlap=0`. Voicetest 105622 skip=97334
    `PASS_CONTINUOUS_AUDIO duty=0.685` → `PASS_PARTIAL_AUDIO drop=D
    duty=0.35`. 123525 skip=20000 duty 0.64→0.325. Independent 360 ms
    eyes lose DEC-0007 overlap catch-up.
  - Streaming DDC still fails 105622 duty 0.095 (DEC-0014 re-measured).
- **Decision:** Do not ship 360 ms fresh-only block hops. Restore
  DEC-0009 80+280. Next live hole is worker-busy skipping IQ, not hop
  size. Isolation (L2) and streaming-DDC-off unchanged until DEC-0018.

---

## DEC-0018 — Streaming DDC must not jump the dibit lattice to RF-sample time

- **Date:** 2026-09-08
- **Status:** accepted
- **Evidence:**
  - DEC-0014: `SDR_TOWN_P25_STREAMING_DDC=1` on 105622 skip=97334
    `PASS_PARTIAL_AUDIO drop=A duty=0.095 emptyWindows=80/89`. First hops
    extracted; later hops `p2bursts=0`. Locked hops were already 80 ms
    overlap 0 (not the 20260830 40 ms slice).
  - `decodeP25VoiceAudioBlock` maps `iqStartAbsolute * 6000 / inputSr` and
    calls `alignPhase2AbsoluteDibitCursor` before `processIq`. Streaming
    DDC FIR/resampler output lags that RF-time prediction. The old
    streaming branch treated a small forward gap as harmless and **set
    `m_phase2StreamDibits = chunkStartAbsolute`**, jumping the sticky
    HDQPSK lattice off the actual dibits. Block-channelize overlap still
    needs that rewind (DEC-0006).
  - DEC-0017 360/0 independent block eyes failed 105622 duty 0.35: overlap
    CQPSK is required unless the Costas/framer stream is truly contiguous.
- **Decision:**
  1. When streaming DDC is on, do not align the dibit cursor from input-IQ
     sample indices. Contiguous hops append actual DDC/CQPSK dibits.
  2. If align is still called, a forward gap ≤ 8 bursts must **not** jump
     the cursor; leave the actual dibit count. Larger gaps still reset
     (retune / dropped RF).
  3. Re-measured 2026-09-08 after this skip: 105622 env=1 still
     `PASS_PARTIAL_AUDIO` (duty 0.16 at 80 ms, 0.09 at hopms=160, 0.045 at
     hopms=360). Default-on stays off (DEC-0014). The jump was real; it was
     not the whole eye-loss. Block 80+280 remains the proven file path.
- **Consequences:** Block 80+280 (DEC-0009) unchanged. Isolation (L2)
  unchanged. Env=1 becomes the experiment to re-measure, not a hop/TTL
  guess.

---
