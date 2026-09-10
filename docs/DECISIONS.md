# Decisions

Format: ID, date, status, evidence, decision, consequences.
A decision is recorded **before** code that depends on it is written.

---

## DEC-0040 — Mechanical split of `main.cpp` (ISS-0004 / T-0009)

- **Date:** 2026-09-09
- **Status:** accepted (complete 2026-09-10)
- **Evidence:**
  - `src/main.cpp` was ~35k lines: shared P25 helpers, voicetest, `MainWindow`,
    `runCLI`, and `main()` in one TU (ISS-0004). After Phases 0–8: ~2k leftovers
    + `main()`; orchestration in dedicated TUs.
  - String-lock verifiers use `src/tools/p25_orchestration_sources.py`.
  - ISS-0004: must not mix a rewrite with feed-gate behavior changes.
- **Decision:**
  1. Mechanical move-only split into focused TUs:
     `P25AppGlobals`, `P25TalkgroupRegistry`, `P25VoiceTiming`, `P25RollingIq`,
     `P25VoiceDecode`, `P25VoiceTest`, `CliApp`, `MainWindow`, `AppBootstrap`,
     thin `main.cpp`.
  2. No hop/TTL/CADENCE/feed-gate/audio behavior changes in split commits.
  3. Verifiers read concatenated orchestration sources via
     `src/tools/p25_orchestration_sources.py`.
  4. Build + `sdr_town_tests` + verifier sweep after each extract phase.
  5. Branch `refactor/iss-0004-split-main`; push + PR when green.
- **Consequences:** Maintainability for P25/GUI/CLI; follow-up may further
  split the `MainWindow` constructor after this lands.

---

## DEC-0039 — Post-emit no-target counts as eye-lost (DEC-0035 refine)

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Operator: latest live not like DEC-0035 / 095846 (~95%). Capture
    `20260909_110941` TG 10301 slot 1: max duty **0.452**, **0×** ≥0.65
    (095846 same TG/slot max **0.947**, 10× ≥0.65).
  - 110941: brief emits then long `no voice sync` / drop A; 80+280 held.
    Some hops had structure/companion bursts with `targetVcw=0` — DEC-0035
    required bursts==0, so those stayed cand=8 and starved re-lock.
  - Streaming default-on still rejected (DEC-0038 duty 0.23). Product path
    remains block + DEC-0035-class live re-lock.
- **Decision:**
  1. Keep DEC-0035 replay caps (cand=16/240) on eye-lost after speak.
  2. Expand eye-lost: after `hadSuccessfulEmit`, `targetVcw==0` and
     `decodedFrames==0` counts as lost even if companion/structure bursts >0.
  3. Healthy target eye still cand=8/120. No hop/TTL change. No streaming
     default-on.
- **Consequences:** Live should recover toward 095846 continuous clear after
  first emit; prove with a new start/stop capture.

---

## DEC-0038 — Streaming sticky Gardner; do not default-on (060036 still 0.23)

- **Date:** 2026-09-09
- **Status:** accepted (implemented; default-on still gated)
- **Evidence:**
  - Operator ask: match SDRTrunk sticky HDQPSK so live is continuous like
    replay. File bar: 060036 TG 10301 skip=261000 center=421.96375.
  - Block (unset): `PASS_CONTINUOUS duty=0.705` essKnown=yes p2macCrc=156.
  - Stream env=1 before this DEC (DEC-0033): duty **~0.25**, cqpskLock=0.
  - Root in tree: unlocked CQPSK search set `candidateTiming.cqpskValid=false`
    every candidate, cold-acquiring Gardner on each 80 ms eye and defeating
    streaming Costas continuity (SDRTrunk keeps Gardner across chunks).
  - Tried post-commit discrete CQPSK lock create / soft-latch: duty **0.12**
    (worse) — froze a weak map while search stop engaged.
  - After sticky Gardner + warm differential-only shaping: stream duty
    **0.23**, targetVcw=132, p2macCrc=4, essKnown=**no**, still ≪0.65.
- **Decision:**
  1. Under `enableStreamingChannelDdc`, do **not** clear `cqpskValid` during
     unlocked CQPSK candidate search (keep sticky Gardner/Costas).
  2. When streaming Gardner is already warm, skip absolute/conjugate CQPSK
     maps and limit dibit permutations (π/4 DQPSK-shaped like SDRTrunk).
  3. Do **not** create/freeze discrete CQPSK lock from soft structure or
     post-commit alone on streaming (measured regression).
  4. **Do not default-on** streaming (DEC-0014 gate). Env=1 still fails
     duty ≥0.65 + essKnown on the continuous-clear file bar.
- **Consequences:** Streaming mechanics closer to SDRTrunk timing continuity;
  product path stays block-channelize (live DEC-0035 class) until env=1 hits
  ≥0.65. T-0010 / ISS-0001 remain open for streaming default-on.

---

## DEC-0037 — Restore clear-eye rolling hold; never purge on hold

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - After DEC-0036 always-advance, capture `20260909_100909`: operator heard
    little chirps instead of ~95% continuous. CADENCE max duty **0.40**,
    **0×** ≥0.65 (was **0.947** / 10× on 095846). emit>0 36/77 but duty
    mostly 0.04–0.16.
  - DEC-0036 fixed unknown-grant start silence but broke the clear sustain
    path that needed a one-hop MAC/ESS retry on the same RF.
- **Decision:**
  1. Restore `return false` (hold) for normal clear eyes that have selected
     VCWs neither fed nor queued.
  2. Advance only for `waitingForClearGrant` / late-entry waiting /
     `WaitingForClearGrant` diag (095846 TG 30302 class).
  3. On hold: keep `holdDecodeAbsolute` but **do not** purge newer publish
     results or worker jobs (that purge was the start-silence killer).
- **Consequences:** Expect clear follows back near 095846 continuity; unknown
  starts should not wipe the pipeline if a hold still occurs.

---

## DEC-0036 — Do not hold rolling cursor when VCWs were not queued

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Capture `20260909_095846` after DEC-0035: later TG 10301 clear follow
    excellent (max duty **0.947**, 10× ≥0.65). Operator: ~95% good with small
    gaps/lag; **quiet follows at start**.
  - First follow TG **30302** ENC=unknown: window with `targetVcw=14` fed=0
    pendingQueued=0 → `rolling cursor hold` + purge newer jobs → then
    permanent `p2bursts=0` / drop A for ~60 s until TG 10301.
  - Root: `p25Phase2RollingDecodeWindowConsumed` returned false when selected
    VCWs were visible but neither fed nor queued (waiting MAC/ESS). Hold
    re-decodes the same RF and **purges** newer live work.
- **Decision:**
  1. If selected VCWs were not fed and not queued, **advance** the rolling
     cursor (return true). Overlap still re-sees recent dibits next hop.
  2. Do not soften DEC-0012. Keep DEC-0035 eye-lost replay caps.
- **Consequences:** Unknown-grant starts must keep extracting after the first
  eye instead of holding silent. Mid-call small gaps (ctxDrop / feedRatio~0.3
  from 280 ms overlap) remain a separate tighten if still audible.

---

## DEC-0035 — Live eye-lost re-lock uses replay CQPSK caps

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live CADENCE re-prove pending)
- **Evidence:**
  - Operator: live still one-emit cliff; file/GUI replay almost continuous.
  - Capture `20260909_094846` on DEC-0034 exe: live CADENCE drop **A=60**/62,
    emit>0 **5**/62, max duty **0.338**. TG 30302 @ 417.675: brief VCW →
    emit → permanent `p2bursts=0` while hops stay 80+280.
  - **Same IQ** CLI voicetest TG 30302 skip=1300 center=417.66375 slot 0:
    `PASS_PARTIAL_AUDIO duty=0.43` with `targetVcw=652` / `p2bursts=711`
    (RF has continuous clear; live extract starved).
  - Live GUI worker after speak: hot **cand=8 / 120 ms**. CLI voicetest +
    GUI IQ-replay hot path: **cand=16 / 240 ms** (`kP25ReplayHot*`).
  - DEC-0034 (keep block CQPSK hint) did not change 094846 — wrong lever for
    this live/replay split.
- **Decision:**
  1. On live block-channelize `hotPhase2TrafficJob` after speak: if the
     previous hop has no Phase-2 eye (`p2bursts=0` and no target VCW/mask),
     use **replay** CQPSK caps (16/240). If the eye is present, keep
     DEC-0019 cand=8/120 (060221 worker-busy).
  2. Do not soften DEC-0012. Do not invent hop/TTL. Do not default-on
     streaming.
- **Consequences:** Reopen GUI on new exe. T-0010 prove = live CADENCE must
  keep extracting after first emit on 094846-class follows (not only file).

---

## DEC-0034 — Post-emit empty eye must keep block CQPSK hint

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Capture `20260909_092250` on DEC-0033 exe (~139 s): emit>0 **9**/131;
    drop **A=116** / B=11 / D=4; max dutySec **0.416**; fresh **80+280** held.
  - TG **12014** slot1: ~10 s of strong extract (`targetVcw` 12–72) with
    `fed=0` (dual-slot / ESS unknown gates), then one `gate=emit`, then
    immediate permanent `p2bursts=0` / drop A while hops stay 80+280.
  - DEC-0032 post-emit empty-eye streak≥2 called `clearBlockCqpskHint()`.
    Block-channelize has no sticky Costas — the hint is the only carrier
    continuity across hops. Clearing it after the first speak matches the
    one-emit-then-silence operator report.
  - DEC-0033 companion-only sticky fallthrough was **not** streaming-gated
    and could thrash dual-slot sticky epochs on the default path.
  - File bar after fix: 060036 TG 10301 skip=261000 still
    `PASS_CONTINUOUS_AUDIO duty=0.705`.
- **Decision:**
  1. Post-emit empty-eye soft rehunt keeps `ForceMaskEpochRehunt` only —
     do **not** call `clearBlockCqpskHint()`.
  2. Companion-only sticky fallthrough stays behind
     `enableStreamingChannelDdc` (DEC-0033 streaming path only).
  3. Do not soften DEC-0012. Do not invent hop/TTL/PLC. Default-on
     streaming still gated (DEC-0033).
- **Consequences:** Reopen GUI on new `SDR_Town.exe`. T-0010 live CADENCE
  must not cliff to all-A after first emit while RF continues (092250 class).

---

## DEC-0033 — Sticky HDQPSK: use persistent framer; stop hop CPR

- **Date:** 2026-09-09
- **Status:** accepted (implemented; default-on still gated)
- **Evidence:**
  - Capture `20260909_083254` (DEC-0032 exe): post-emit hops stay **80+280**;
    rolling **4 s**; CADENCE drop **A=147**/152, emit>0 **5**/152. First TG
    30302 emit then ~0.6 s `no voice sync`. ForceMask never logged. Dominant
    failure is extract after emit, not hop geometry.
  - SDRTrunk `P25P2DecoderHDQPSK.receive`: continuous DDC → sticky Costas +
    Gardner → `P25P2MessageFramer` for channel life. OP25 CQPSK: sticky FLL/
    Gardner/Costas; hop resets frame sync only, not Costas.
  - Our default block path clears Costas every hop (`P25LiveDecoder.cpp`
    channelize). Streaming DDC (`SDR_TOWN_P25_STREAMING_DDC=1`) keeps Costas
    but still failed 105622 duty ~0.09–0.16 (DEC-0014/0018): after first emit,
    wrong-slot / `p2bursts=0` alternation.
  - Root in tree: `P25Phase2Framer` is fed and `m_pendingFramerBursts` filled,
    but commit only consumed them when `demodState >= TrackingSoft`. Carrier
    confidence only rose on `cqpskCarrierLoopApplied`, so state often stayed
    **Cold** → pending bursts cleared → sticky lattice walk early-returned on
    companion-slot bursts. Always-queue without anchor / empty annotate /
    clearing anchor on one misaligned burst regressed 060036 env=1 to duty
    **0.02**; those holes are fixed below.
  - **Measured after fix (060036 TG 10301 skip=261000 center=421.96375):**
    block 80+280 `PASS_CONTINUOUS duty=0.705` (held). env=1 stream
    `PASS_PARTIAL duty=0.25` (was ~0.09–0.16 class on 105622; no wrong-slot
    islands). 105622 IQ not on disk. Default-on still rejected.
- **Decision:**
  1. Stop hop/TTL/grace/PLC CPR for ISS-0001 continuous clear. Do not soften
     DEC-0012. Do not default-on streaming until env=1 hits duty ≥0.65 on a
     continuous-clear file bar + live CADENCE.
  2. Streaming DDC queues persistent-framer bursts for commit; commit consumes
     them only when a superframe anchor exists. Annotate with source dibits.
     Misaligned framer bursts skip, do not wipe the anchor. Carrier/timing FSM
     notes structure under streaming.
  3. Sticky lattice early-return must not stick on opposite-slot-only VCWs
     when preferred TDMA slot is known — fall through to re-lock (Costas stays).
  4. GUI IQ-replay must not prepend overlap context when streaming DDC is on.
  5. Voicetest prints cqpskLock / residHz / framerBurst / demodState when
     stream contiguous DDC is on.
- **Consequences:** Block 80+280 remains shipped default. Env=1 improved but
  still partial (0.25 on 060036). T-0010 stays open until live prove.

---

## DEC-0032 — Post-emit keep DEC-0009 80+280; soft empty-eye rehunt (revert DEC-0031 planner)

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_081701` (DEC-0031 exe, ~264 s, SNR ~8.1 dB). Operator:
    single little emit at follow start then hang on `no voice sync`.
  - Live CADENCE: drop **A** 234 / D 4 / B 3; emit>0 only **7**/241 s.
  - TG **10120** @ 421.975: cold eye emit=12 duty 0.239, then immediately
    worker hops `fresh=245760` (**120 ms** backlog catch-up) with
    `diag=no voice sync` for tens of seconds. DSP median ~70 ms.
  - Root: DEC-0031 planned backlogCatchUp **before** speaker-sustain. Cold
    emit dsp~428 ms left backlog → next hop left DEC-0009 80+280 → eye death.
  - Second hole: every post-emit empty eye raised `MaskEpochRepairHoldWindows`,
    which makes the planner skip speaker-sustain (steal geometry).
- **Decision:**
  1. Restore speaker-sustain / active-clear **before** backlogCatchUp
     (DEC-0009 80+280 after speak). Catch-up 120+280 only off that path.
  2. Post-emit empty-eye streak≥2: `ForceMaskEpochRehunt` +
     `clearBlockCqpskHint()` — do **not** set MaskEpochRepairHoldWindows.
  3. Keep DEC-0031 once-clear `requireFedAudio=false` continuation.
  4. Do not reopen post-emit cold 240/64, streaming default-on, or DEC-0012.
- **Consequences:** Live prove = after first emit, hops stay fresh=80 ms
  context=280 ms and CADENCE must not cliff to all-A `no voice sync` while
  talk continues (T-0010 / 081701).

---

## DEC-0031 — Once-clear: backlog catch-up before speaker-sustain; continuation without fed chicken-egg

- **Date:** 2026-09-09
- **Status:** accepted (planner part **superseded by DEC-0032**; once-clear continuation kept)
- **Evidence:**
  - Capture `20260909_062006` (DEC-0030 exe, ~46 s, SNR ~13.8 dB). LO correct
    (`rfCenter=421.96375` = voice 421.975 − 11.2 kHz). Rolling reaches
    **8192000** (4 s) — DEC-0030 held.
  - Live CADENCE: drop **A** 41 / **D** 1; one emit island duty **0.139**; then
    `no voice sync` with windows still running. DSP median ~**70 ms** on 80 ms
    fresh (drain OK; absStart “jumps” were rate-limited log artifacts).
  - File voicetest same IQ TG **30003** slot 1 center 421.96375: 
    `PASS_PARTIAL_AUDIO drop=D duty=0.46`. Emit islands alternate with
    `unknown-waiting-clear` / companion-louder (`oppVcw > targetVcw`, fed=0)
    — DEC-0012 isolation class, not missing RF.
  - Planner: `p25Phase2PlanVoiceDecodeChunk` early-returned DEC-0009 80+280
    whenever `activeSpeakerClearPath`, so DEC-0024 backlogCatchUp (120+280)
    was unreachable after speak.
  - Security gate called `SameCallSelectedTimeslotContinuationSafe(...,
    requireFedAudio=true)` — chicken-egg vs dual-slot mute clearing audio
    before continuation can keep trustedClear (DEC-0003 once-clear).
- **Decision:**
  1. Plan backlogCatchUp **before** speaker-sustain early-return (still
     DEC-0024 120 ms fresh / 280 ms overlap; no 180 ms live-edge catch-up).
     **Superseded by DEC-0032** (caused 081701 post-emit eye death).
  2. Once latch Clear / hadSuccessfulEmit / callHadSpeakerAudio, evaluate
     continuation with `requireFedAudio=false` so dual-slot untrusted does
     not collapse once-clear into `unknown-waiting-clear`. **Kept.**
  3. Do **not** soften `PostEmitMixedMacDead` (DEC-0012 / 041716). Do not
     reopen streaming default-on, hot cand=3, or post-emit cold.
- **Consequences:** Re-measure 062006 file duty (expect ≥0.46, hope toward
  0.65 if continuation recovers selected-dominant mixed hops). Live prove =
  after first emit, CADENCE must keep extracting when RF has voice (T-0010).
  Live eye vs file extract gap on 062006 remains open if still drop A.

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

## DEC-0019 — Block CQPSK hint that framed Phase 2 stops the grid

- **Date:** 2026-09-08
- **Status:** accepted (hard early-stop only; cand=3 rejected)
- **Evidence:**
  - SDRTrunk `P25P2DecoderHDQPSK.receive`: one Costas + Gardner stream;
    `main()` feeds 2048@25 kHz ≈ 82 ms. Two `P25P2AudioModule`s filter by
    `message.getTimeslot()` — no companion-louder mute, no per-hop CQPSK
    re-search.
  - Our block-channelize path clears Costas every hop but keeps
    `m_blockCqpskHint`. Capture `20260908_060221`: dsp median **161 ms**
    on 80 ms fresh (14% ≥360 ms), **100** worker-busy, CADENCE talk median
    duty **0.30**, emit islands median length 1. Same class as 053241.
  - File voicetest of 060221 TG 30302 slot 1 skip=128500 center=420.21375:
    `PASS_CONTINUOUS_AUDIO duty=0.922` — IQ has continuous voice; live
    hole is drop **D**, not missing RF.
  - Soft-evidence early-stop trial: 105622 duty **0.645→0.305**, 041716
    **0.87→0.5**. Rejected (20260729 wrong-hint freeze).
  - Hard early-stop + file cand=16: 105622 duty **0.645** (kept).
  - Hot cand=**3** after speak (voicetest mirror of live): wall 17→7 s but
    105622 duty **0.645→0.055** drop=**A**. Same eye-loss class as DEC-0020.
    Cand=3 rejected; live hot stays **8**.
- **Decision:**
  1. After evaluating the block CQPSK hint, if it already produced
     **hard** Phase 2 lock evidence this window, set `stopCqpskSearch`.
  2. Do **not** cap hot CQPSK candidates at 3 after emit.
  3. Do not default-on streaming DDC (DEC-0014). Do not change
     hop/TTL/overlap (DEC-0009). Do not reopen DEC-0012 hop-wide mute.
- **Consequences:** Live drop D still open (T-0010). Shrinking eyes (DEC-0020)
  and cand=3 both buy wall clock by destroying extract. Next measured path
  must keep duty ≥0.65 while cutting per-hop DSP another way, or fix
  streaming DDC eye loss (DEC-0014/0018).

---

## DEC-0021 — Streaming lock-only cand=1 is premature on our Costas

- **Date:** 2026-09-08
- **Status:** rejected (insufficient; eye dies with cand=16 too)
- **Evidence:**
  - env=1 105622: after first emit, cand=1 → wrong-slot / p2bursts=0.
  - Disabling lock-only (always hot cand=16): window 5 still wrong-slot;
    windows 6+ stuck `p2bursts=1 p2vcw=0`. Duty **0.01**. Not cand=1 alone.
  - Transition window 4→5 is also **160 ms → 80 ms** fresh. Windows 2–4 at
    160 ms extracted voice; 80 ms sustain did not.
- **Decision:** Do not treat lock-only disable as a fix. Next trial is
  DEC-0022 (160 ms streaming sustain). Restore prior lock-only code shape
  only if a later DEC needs it; leave disabled while measuring 160 ms.
- **Consequences:** Streaming still not default-on (DEC-0014).

---

## DEC-0030 — Active rolling clamp must honor DEC-0023 4.0 s (not 2.048 s)

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_060036` (~392 s gapless, SNR summary ~6.8 dB, DEC-0029
    exe). Eyes 80+280. Quiet-returns down (DEC-0029 held). Still sparse voice.
  - CADENCE: emit>0 only **10**/215 s; max duty **0.639**; drop **A** 209;
    worker-busy **216**. Total speaker PCM ~**2.24 s**.
  - TG **10301** @ 421.975: cold `context=0` emit=32 duty **0.639**, then
    rolling stuck at exactly **4194304** samples while worker-busy; all later
    hops `p2vcw=0` / drop A / bridge top-ups.
  - **File voicetest** of the same IQ (skip≈261000, slot 0, center 421.96375):
    `PASS_CONTINUOUS_AUDIO duty=0.705` (5.64 s audio / 8 s span). Live silence
    after the first island is starvation, not missing RF.
  - Root: DEC-0023 set `kP25Phase2VoiceDecodeActiveRollingSeconds = 4.000` but
    both GUI/CLI rolling clamps still used `4194304` (= **2.048 s** @ 2.048
    MHz). Soft-trim could not keep the 4 s window DEC-0023 required.
- **Decision:** Route live rolling sizing through
  `p25Phase2VoiceRollingMaxSamples` with hard cap **16 s** (4× active per
  DEC-0023). Eye/emit backlog allowance matches the 4.0 s active window (was
  1.2 s / 4194304). Do not invent hop sizes, reopen streaming default-on, or
  post-emit cold escalate.
- **Consequences:** Live prove = after a cold emit island, rolling may exceed
  4194304 and CADENCE must keep extracting (not cliff to duty 0). Re-check
  105622 when available.

---

## DEC-0029 — Clear-trusted follow hold + structure exits coldAcquire

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_053448` (~308 s gapless, SNR ~17 dB, DEC-0028 exe).
    Eyes OK (0× 40 ms; 1365× 280 ms). **0** ReturnEncrypted.
  - Operator: very little / sparse voice — one small emit every few minutes.
  - CADENCE: emit>0 only **16** of **176** s; **0×** duty≥0.65. Drop **A**
    dominant (162), **D** (12). Block mostly `no-vcw-from-live-window`.
  - Timeline TG **30302** @ 421.225: cold mega-eye finds VCWs; emit island
    (emit=8 duty **0.151**); then empty eyes; at **+5 s**
    `ended or went quiet` releases traffic source while `callClearTrusted=yes`;
    next grant cold-rearms `context=0` / generation++ → another sparse island.
  - Follow SM: after emit, `kSpeakerImmediateGraceMs=2500` required current
    traffic structure; empty eyes dropped speaker grace, then
    `activitySilenceLimitMs=3500` fired ReturnActivityGone. Clear-trusted
    calls need the full 40s speaker grace (field 032428) without a live VCW
    window, plus 15s activity silence (same as
    `kP25Phase2ClearTrustedUnacquiredDwellStealGraceMs`).
  - DSP: emptyEye med ~67 ms (DEC-0028 helped); structureNoVcw med **~484 ms**
    — coldAcquire still required target VCW, so structure-only peaks kept
    burning 240/64.
- **Decision:**
  1. Clear-trusted / traffic-audio-open holds: full 40s speaker grace without
     current VCW/structure; activity silence 15s (not 3.5s).
  2. Exit `coldAcquireJob` once sustain has sf+mask ≥4 (epoch-grade
     structure), not only target VCW / hard acquire.
  3. Same-call metadata commit must not wipe traffic-proven clearKnown on
     unknown OP=0x02 grants (mirror in-place follow).
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = no quiet-return
  within ~15s of clear emit while clearTrusted (T-0010 / 053448 pattern).
  Do not reopen streaming default-on, hot cand=3, or post-emit cold escalate.

---

## DEC-0028 — Never cold-escalate CQPSK after first emit (remove emptyStreakReacq)

- **Date:** 2026-09-08
- **Status:** accepted (pending measure)
- **Evidence:**
  - Capture `20260908_115603` (~487 s gapless, SNR ~17.8 dB, DEC-0027 exe).
    Eyes OK (0× 40 ms). 0 ReturnEncrypted.
  - Operator: first emit perfect (response to missed prior), then rest of
    conversation unheard.
  - Timeline TG **30302** @ 421.225: 21:58:53 emit=28 duty **0.553** (good);
    next workers dsp **605 / 470 / 481 / 472 ms** on structure/wrong-slot;
    then drop **B** (tv=16 fed=0) on companion-louder mixed window (DEC-0012
    isolation — correct). CADENCE peak still **0.599**; 0× ≥0.65.
  - structureNoVcw still med **465 ms** after DEC-0027 — emptyEye streak≥2
    armed cold 240/64 on the *prior* hop; the next structure/voice hop ran
    that cold grid. emptyEye itself is ~102 ms on hot.
  - `emptyStreakReacq` required `hadSuccessfulEmit`, so it **only** fired
    post-emit (pre-emit already uses `coldAcquireJob`). DEC-0026/0027 could
    not make post-emit cold safe.
- **Decision:** Delete post-emit `emptyStreakReacq` cold path. After speak,
  always hot cand=8 / 120 ms (DEC-0019). Soft mask rehunt + coldAcquireJob
  remain. Do not change hop geometry / TTL / DEC-0012 companion-louder bar.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = CADENCE after
  first emit without 400+ ms structure/opp hops (T-0010).

---

## DEC-0027 — Cold CQPSK escalate only on true emptyEye (not structure/opp)

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0028 on 115603)
- **Evidence:**
  - Capture `20260908_112922` (~483 s gapless, SNR ~14.4 dB, DEC-0026 exe
    mtime after BN-0015). Eyes OK (0× 40 ms; 2805× 280 ms). 0 ReturnEncrypted.
  - CADENCE: **0** seconds ≥0.65; peak **0.639** drop=D. worker-busy **417**,
    wrong TDMA **141**. Talk emit>0 median duty **0.318**.
  - DSP by eye class (112922):
    - emptyEye (b=0,tv=0): med **101 ms**, 0× ≥200 ms
    - oppOnly: med **202 ms** (DEC-0026 helped vs prior p90 burn)
    - **structureNoVcw** (b>0,tv=0,ov=0): n=61 med **462 ms**, 40× ≥400 ms
    - target>0: med **241 ms** (worse than 110146 202 ms) — starved by above
  - Soft mask-epoch rehunt already exists for structureNoVcw
    (`p25Phase2StructureNoTargetVoiceWindows >= 3` →
    `invalidatePhase2StickyMaskEpoch`). Cold 240/64 was stacked on top.
  - `emptyStreak >= 3` alone also cold-escalated during opp/structure silence
    (re-opened DEC-0026 hole without needing structureNoTarget).
- **Decision:** `emptyStreakReacq` = hadEmit && emptyEye && emptyStreak≥2 &&
  !oppositeSlotOnlyEye. Remove structureNoTarget and
  StructureNoTargetVoiceWindows from the cold CQPSK path. Keep soft mask
  rehunt. Keep hot cand=8 (DEC-0019). Do not change hop geometry / TTL.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  (T-0010). StructureNoVcw dsp should fall toward hot budget (~100–200 ms).

---

## DEC-0026 — Opposite-slot-only eyes must not cold-escalate CQPSK

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0027 on 112922)
- **Evidence:**
  - Capture `20260908_110146` (~445 s, DEC-0025 exe): eyes OK (0× 40 ms;
    2472× 280 ms). No `ReturnEncrypted` (DEC-0025 held). Companion-louder 0.
  - Only **12** CADENCE ok seconds all session. Dominant live: drop **A/D**,
    `worker-busy` **382**, wrong TDMA **238**.
  - Best IQ (TG **20202** RID 0x1F95EB @ 421.975): file
    `PASS_CONTINUOUS duty=0.85`; live **5** ok seconds, worker-busy 54,
    wrong-TDMA follow ×69. Wrong-slot worker hops dsp med **~173 ms**
    p90 **~434 ms**.
  - TG **30017** @ 420.225: file duty **0.74**; SNR p10 **13.3**; live
    talkMed **0.303** / dsp med **203 ms**.
  - Hot path after emit treated `structureNoTarget` (bursts>0, targetVcw=0)
    + empty feed streak as cold reacq (**240 ms / 64 cand**) — that is
    normal opposite-slot TDMA silence, not a lost eye.
- **Decision:** Exclude opposite-slot-only eyes (`Phase2WrongSlot` diag or
  oppVcw>0 with targetVcw=0) from `structureNoTarget` cold escalate. Keep
  DEC-0019 hot cand=8; do not reopen cand=3. Do not change hop geometry.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  (T-0010). Weak-RF calls (SNR p10 ~5) remain hard. **112922:** opp-only
  improved, but structureNoVcw + bare emptyStreak≥3 still cold-burned
  (DEC-0027).

---

## DEC-0025 — Clear→Encrypted latch needs MAC/PTT bar (match follow trusted ESS)

- **Date:** 2026-09-08
- **Status:** accepted (110146: zero ReturnEncrypted)
- **Evidence:**
  - Capture `20260908_103955` after DEC-0024: `context=81920` **0**; 280 ms
    eyes held. Operator 50/50; same TG **20202**, different RIDs.
  - RID **0x1F83FF** @ 420.725: live choppy; file voicetest duty **0.46**
    `PASS_PARTIAL` / no voice sync. Ring SNR p10 **7.3 dB**.
  - RID **0x1F95EB** @ 420.225: live better (7× CADENCE ok); file
    `PASS_CONTINUOUS duty=0.83` essEncrypted=no. SNR p10 **12.8 dB**.
  - Same “good” call ended `ReturnEncrypted` at 20:42:17 while last follow /
    DEEP DIAG still `p2ess=clear` / `encrypted=no` and PCM emit 18 ms prior.
  - Follow SM `trustedEncryptedEss` already requires `phase2MacCrcValid > 0`.
    Security-gate `thisWindowObservedEncrypted` could Clear→Encrypted latch
    (then `grantProvesEncrypted`) without that MAC bar.
- **Decision:** Treat this-window target ESS encrypted as latch/follow proof
  only with target/any MAC CRC **or** PTT-derived security state — same bar
  as `trustedEncryptedEss`. Log which ReturnEncrypted clause fired. Do not
  change hops/settle/DEC-0024. Do not invent TTL.
- **Consequences:** Re-measure 105622 ≥0.645; re-file both RID windows; live
  prove = new GUI capture (T-0010). Weak-RF RID remains drop D until Costas
  continuity improves (not this DEC).

---

## DEC-0024 — Backlog catch-up must keep DEC-0009 overlap (280 ms), not 40 ms

- **Date:** 2026-09-08
- **Status:** accepted (file 105622 ok; live 103955 proves eye fix)
- **Evidence:**
  - Capture `20260908_101644` (desktop exe after DEC-0023): soft-trim
    `context=573440` dominant (~1000 hops); old 80 ms protect almost gone.
  - Operator: start/middle BAD; last voice ~90% good.
  - First clear TG **30302** slot 0 @ 420.225: cold 720→280 ms eyes, then
    from 20:16:59.378 for ~8 s every hop was `context=81920` (**40 ms** =
    `kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds`). CADENCE dutySec
    **0** / drop **A** / `diag=no voice sync` until 20:17:07 when eyes
    returned to 280 ms and duty briefly hit **0.681**.
  - Late TG **10330**: almost all hops `context=573440` (148 vs 2–3 smaller)
    — matches the “last voice good” report.
  - SDRTrunk `P25P2DecoderHDQPSK`: continuous Costas+Gardner traffic source;
    no shrink-context catch-up when the worker lags.
- **Decision:** Backlog catch-up may still take **120 ms fresh / 80 ms
  minFresh**, but overlap is **280 ms** (DEC-0009), not 40 ms. Do not
  lengthen `kP25Phase2PostArmSettleMs` (80) without a settle-specific
  measure — 101644 cold eye already produced audio before the 40 ms spiral.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  on desktop exe (T-0010). Do not reopen 80/0 or streaming default-on.

---

## DEC-0023 — Rolling soft-trim must protect DEC-0009 sustain overlap (280 ms)

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0024 on 101644)
- **Evidence:**
  - Capture `20260908_095936` (desktop `SDR_Town.exe`, TG 30302 slot 0 @
    421.225): promising start (dutySec up to **0.60**, gate=emit,
    companion-louder **0**), then collapse. Talk median dutySec **0.40**
    still drop **D**; cliff ~20:00:18 → drop **A** / return-to-CC.
  - Worker eyes: early **360 ms** (`fresh=163840 context=573440`), then
    dominant **160 ms** (`context=163840` = 80 ms) — 110 submitted hops.
    DEC-0009 already measured 80 ms overlap → duty 0.40/0.51.
  - Rolling backlog hit **2.6–2.9 s** while worker-busy; soft-trim constant
    `kProtectedOverlapSamples = 163840` (**80 ms**) matches the clipped
    context. Comment in `RollingIqWindow::append` already names this as the
    20260901_042425 stutter path; protect depth was never raised to DEC-0009.
  - Voice-period ring SNR median **16.7 dB** (summary 5.2 dB is late CC).
    Not an RF fade for the good-start window.
- **Decision:** Soft-trim protected pre-roll is **280 ms** (573440 samples @
  2.048 MHz = DEC-0009 speaker-sustain overlap), not 80 ms. Active rolling
  window while speaker-live is **4.0 s** (was 2.0 s) so soft-trim can keep
  that overlap under the lag seen on 095936. Hard-cap must not jump the
  decode cursor while undecoded RF is protected; emergency cursor jump only
  above 4× rolling. Do not invent a new hop size. Do not reopen DEC-0012.
- **Consequences:** Re-measure 105622 block duty (must stay ≥0.645). Live
  prove = new GUI CADENCE on desktop exe after this change.

---

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - env=1 after 160 ms sustain: 105622 duty **0.125**, 073304 **0.22**,
    041716 **0.365** (was ~0.01 / 0.125 / 0 with 80 ms). Better, still
    far below PASS_CONTINUOUS 0.65. Wall ~7 s / 8 s span.
  - Root: after first emit, sticky HDQPSK still goes wrong-slot /
    `p2vcw=0`; longer slices only delay the starve.
- **Decision:** Revert to DEC-0014 80 ms streaming slices. Do not default-on
  streaming. Block 80+280 remains the file-proven path.
- **Consequences:** Live drop D needs a Costas/channelize fix that keeps
  duty, not another hop-size guess.

---

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - Capture `20260908_082235` (stock 0.2.51): CC OK; talk median dutySec
    **0.189**; worker-busy; dsp median **185 ms** on 80+280 eyes; drop **D**.
  - File voicetest with DEC-0009 80+280 waits (~16.6 s wall / 8 s span) so
    duty stays 0.645 while live cannot wait.
  - Trial: after emit, block eyes **80 ms fresh + 0 overlap** (voicetest
    `speakerSustainContextMs=0` + live chunk planner same). Wall **2.8 s**
    (fast enough) but 105622 duty **0.645→0.055** drop=**A**; 073304
    **0.045**; 041716 **0.085**; 060221 **0.09**. Same class as streaming
    DDC eye loss (DEC-0014) / independent-eye DEC-0017 — not a live win.
- **Decision:** Do **not** shrink block sustain eyes to 80/0. Keep DEC-0009
  80+280. Live drop D needs either (1) faster DSP on 360 ms eyes (DEC-0019
  path) measured on the **desktop** GUI, or (2) streaming DDC that keeps the
  CQPSK/dibit eye (DEC-0014/0018 still open). Do not invent TTL/grace/PLC.
- **Consequences:** Reverted. T-0010 remains open.

---
