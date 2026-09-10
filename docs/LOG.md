# Development log

Newest at the top.

---

## 2026-09-10 — ISS-0004 / T-0009 closed (DEC-0040); push + PR

- Phases 0–8 complete on `refactor/iss-0004-split-main`.
- Gate: Release `SDR_Town` + `sdr_town_tests` (214/10194); `verify_p25_phase2_*.py` 129/129.
- Docs: ISS-0004 closed, T-0009 done; CODE_NOTES / SPEC_INDEX updated for new TUs.
- Follow-ups (not blockers): leftover session/decoder helpers still in `main.cpp`; optional MainWindow header/ctor split.

## 2026-09-10 — ISS-0004 Phases 6–8: P25VoiceTest / CliApp / MainWindow thin main

- Mechanical DEC-0040 split on `refactor/iss-0004-split-main` (no hop/feed/CADENCE changes).
- **Phase 6:** SigMF/WAV + replay follow/voicetest → `P25VoiceTest`.
- **Phase 7:** CLI batch helpers + `runCLI` → `CliApp` (`GuiRuntimeConfig`/`SavedFrequency` in header).
- **Phase 8:** logging/theme/instance → `AppBootstrap`; GUI class + `populateP25Table` → `MainWindow.h` (Q_OBJECT); `main.cpp` ~2.1k lines leftovers + `main()`.
- Build Release `SDR_Town` + `sdr_town_tests` green; 10194 assertions; 129/129 `verify_p25_phase2_*.py`.

---

## 2026-09-09 — Back to DEC-0035 live re-lock (DEC-0039)

- Operator: not like the DEC-0035 / 095846 ~95% path.
- 110941 TG 10301: max duty 0.452 (095846 was 0.947). Streaming detour was
  the wrong product path.
- **DEC-0039:** post-emit no-target (even with companion bursts) uses replay
  cand=16/240. Reopen GUI exe.

---

## 2026-09-09 — Streaming sticky Gardner; default-on still rejected (DEC-0038)

- Tried to ship SDRTrunk-style sticky HDQPSK as default.
- 060036 block duty **0.705**; stream env=1 after sticky Gardner **0.23**
  (lock-create trial **0.12**). essKnown stays no; p2macCrc=4 vs 156.
- **DEC-0038:** keep Gardner across streaming search; no weak lock freeze;
  no default-on until ≥0.65.

---

## 2026-09-09 — DEC-0036 always-advance chirped clear audio (DEC-0037)

- Operator: worse — little emits instead of ~95% continuous (full flip).
- 100909: max duty 0.40, 0× ≥0.65 (095846 was 0.947 / 10×).
- **DEC-0037:** restore clear-eye hold; advance only waiting-clear; hold
  without purging newer jobs.

---

## 2026-09-09 — Start silence = rolling cursor hold (DEC-0036)

- 095846 after DEC-0035: later clear TG 10301 max duty **0.947** (operator ~95%).
- Start TG 30302 unknown: targetVcw=14 fed=0 pending=0 → cursor hold + purge
  → silent rest of follow.
- **DEC-0036:** advance rolling cursor when VCWs were not queued/fed.

---

## 2026-09-09 — Live vs replay: cand=8 starves re-lock (DEC-0035)

- Operator: live unchanged; replay almost always good.
- 094846 live (DEC-0034 exe): drop A 60/62; emit>0 5; max duty 0.338.
- Same IQ voicetest TG 30302: duty **0.43** targetVcw=652 — RF is fine.
- Live worker after speak: cand=8/120; replay/voicetest: cand=16/240.
- **DEC-0035:** live eye-lost hops use replay caps; healthy eye keeps cand=8.

---

## 2026-09-09 — Capture 092250: clearBlock hint wipe after emit (DEC-0034)

- Operator: almost continuous clear regressed to one emit then silence.
- Live 092250: drop A 116; emit>0 9/131; max duty 0.416; 80+280 held.
- TG 12014: 10 s of targetVcw with fed=0, one emit, then p2bursts=0 forever.
- Root: DEC-0032 empty-eye called `clearBlockCqpskHint()` — block path’s only
  Costas continuity. DEC-0033 companion-only sticky was also ungated on block.
- **DEC-0034:** keep hint on empty-eye ForceMask; gate companion-only to
  streaming. File 060036 still PASS_CONTINUOUS duty=0.705. Reopen GUI exe.

---

## 2026-09-09 — DEC-0033 sticky HDQPSK (stop hop CPR)

- 083254: DEC-0032 80+280 held; hang is drop A / no-vcw after first emit.
- SDRTrunk/OP25: continuous Costas+Gardner + MessageFramer; we were discarding
  framer bursts while FSM stayed Cold and sticky-walking companion slots.
- **DEC-0033:** queue/commit persistent framer under streaming (anchor required);
  annotate with source dibits; no anchor wipe on one misalign; companion-only
  sticky fallthrough; GUI replay context=0 when streaming; voicetest diag.
- File: 060036 block **duty=0.705**; env=1 **duty=0.25** (better than
  historical ~0.09–0.16, still not continuous). 105622 IQ missing. Default-on
  stays off. Live prove still T-0010.

---

## 2026-09-09 — Capture 081701: DEC-0031 catch-up killed post-emit eye (DEC-0032)

- Operator: worse — single emit then hang on `no voice sync`.
- Live: drop A 234; emit>0 7/241. After cold emit, hops `fresh=120 ms`
  (backlogCatchUp) → immediate eye death.
- **DEC-0032:** restore DEC-0009 80+280 after speak; empty-eye soft rehunt
  without MaskEpochRepair steal; keep once-clear continuation.

---

## 2026-09-09 — Capture 062006: LO OK, DEC-0030 OK, once-clear holes (DEC-0031)

- Screenshot LO 421.964 vs CC UI 420.475 is one-RTL park (voice−11.2 kHz), not
  wrong tune. TG 30003 grant was 421.975.
- Live: drop A after one emit; DSP ~70 ms / 80 ms fresh (drain OK); rolling 4 s.
- File voicetest same IQ: **duty 0.46** drop D — dual-slot companion-louder /
  `unknown-waiting-clear` (keep DEC-0012). 060036 file still **0.705**.
- **DEC-0031:** backlogCatchUp before speaker-sustain; once-clear continuation
  without requireFedAudio chicken-egg. Next: live eye sustain if still A.

---

## 2026-09-09 — Capture 060036: live 2s rolling clamp vs file 0.705 (DEC-0030)

- ~392 s gapless, DEC-0029 exe. Quiet-return thrash reduced; voice still sparse.
- Live: max duty **0.639** then cliff; worker-busy 216; rolling stuck **4194304**.
- File voicetest same IQ TG 10301: **PASS_CONTINUOUS duty=0.705**.
- Root: DEC-0023 4.0 s active rolling still clamped to 2.048 s samples.
- **DEC-0030:** honor 4.0 s rolling (+ 16 s emergency hard cap).

---

## 2026-09-09 — Capture 053448: sparse islands / quiet-return thrash (DEC-0029)

- ~308 s gapless, SNR ~17 dB, DEC-0028 exe. Eyes OK; **0** ReturnEncrypted.
- Operator: almost no voice — maybe one small emit every few minutes.
- CADENCE: **16**/176 s with emit>0; **0×** ≥0.65. Drop **A** (162) / **D** (12).
- Smoking gun: TG 30302 emit=8 @ 15:35:28 (`callClearTrusted=yes`), then empty
  eyes; **15:35:33** `ended or went quiet` released traffic; **15:35:35**
  same TG cold-rearmed (`context=0`, generation++). Pattern repeats.
- Root: follow SM dropped 2.5s speaker grace without current structure, then
  3.5s activityGone mid clearTrusted call. structureNoVcw still cold ~484 ms.
- **DEC-0029:** clear-trusted 40s speaker hold + 15s activity silence; exit
  coldAcquire on sf+mask≥4; same-call unknown grant preserves clearKnown.

---

## 2026-09-08 — Capture 115603: perfect first emit then silence (DEC-0028)

- ~487 s gapless, SNR ~17.8 dB, DEC-0027 exe. Eyes OK; **0** ReturnEncrypted.
- TG 30302 @ 421.225: first emit **perfect** (duty 0.553 emit=28); operator
  heard response to a missed prior (encrypted TG 12068 on same RF skipped).
- Next hops dsp **470–605 ms** (structure/wrong-slot) → worker-busy; later
  companion-louder mixed → drop B (DEC-0012, correct isolation).
- Root: post-emit `emptyStreakReacq` (even emptyEye-only DEC-0027) arms cold
  240/64; the *following* structure/voice hop burns. emptyEye alone ~102 ms.
  **DEC-0028:** delete post-emit emptyStreak cold escalate; stay hot cand=8.

---

## 2026-09-08 — Capture 112922: structureNoVcw cold burn (DEC-0027)

- ~483 s gapless, SNR ~14.4 dB, DEC-0026 exe. Eyes OK; **0** ReturnEncrypted.
- CADENCE peak **0.639** (0× ≥0.65); worker-busy **417**; wrong TDMA **141**.
- DSP: structureNoVcw med **462 ms** (40× ≥400 ms); emptyEye med 101 ms;
  oppOnly med 202 ms. Soft mask rehunt already existed; cold 240/64 stacked.
- Root: `emptyStreakReacq` still cold-escalated on structureNoTarget /
  StructureNoTargetVoiceWindows / bare emptyStreak≥3.
  **DEC-0027:** cold escalate only on true emptyEye (+ DEC-0026 opp exclude).

---

## 2026-09-08 — Capture 110146: file continuous, live wrong-slot cold burn (DEC-0026)

- ~445 s gapless, SNR ~17 dB, DEC-0025 exe. Eyes OK; **0** ReturnEncrypted.
- 12 CADENCE ok seconds total. worker-busy **382**, wrong TDMA **238**.
- TG 20202 file duty **0.85** vs live 5 ok s; wrong-slot dsp p90 **~434 ms**.
- Root: after emit, opp-slot-only windows cold-escalated CQPSK (240/64).
  **DEC-0026:** exclude opposite-slot-only from structureNoTarget escalate.
- 30017 file 0.74 / live talkMed 0.303 — residual drop D still open.

---

## 2026-09-08 — Capture 103955: RID split is RF; false ReturnEncrypted (DEC-0025)

- Desktop after DEC-0024. Eyes: `context=81920` **0**, 280 ms dominant.
  Operator ~50/50; same TG **20202**, different RIDs.
- **0x1F83FF** @ 420.725: file duty **0.46** PARTIAL; SNR p10 **7.3**. Live
  `no voice sync`×30. Not a RID code path — IQ is hard.
- **0x1F95EB** @ 420.225: file `PASS_CONTINUOUS duty=0.83`; SNR p10 **12.8**.
  Live better then `ReturnEncrypted` while ess still logged clear.
- **DEC-0025:** Clear→Encrypted / grantEncrypted promotion needs MAC/PTT bar
  matching follow `trustedEncryptedEss`. Log ReturnEncrypted reason.
- Residual live drop D / worker-busy on good RF still open (T-0010).

---

## 2026-09-08 — Capture 101644: start/middle BAD = 40 ms catch-up eyes (DEC-0024)

- Desktop exe after DEC-0023. Soft-trim working (`context=573440` ~1000×;
  `163840` only twice). Companion-louder **0**. SNR ~13.8 dB.
- Operator: start/middle unusable; last voice ~90% good.
- First clear TG **30302** @ 420.225: after cold/280 ms eyes, planner
  switched to backlog catch-up `context=81920` (**40 ms**) for ~8 s →
  dutySec **0** / drop **A** / no voice sync. Recovered to 280 ms at
  20:17:07 (brief duty **0.681**) then choppy D/A. Late **10330** stayed
  on 280 ms context (148 hops) — matches last-voice quality.
- SDRTrunk Phase 2 traffic never shrinks CQPSK context to chase lag.
  **DEC-0024:** catch-up overlap **280 ms** (keep 120 ms fresh). Post-arm
  settle 80 ms not the named hole (cold eye already emitted). Live
  re-prove still required.

---

## 2026-09-08 — Capture 095936: good start then collapse = clipped overlap (DEC-0023)

- Desktop `build\bin\Release\SDR_Town.exe` (not stock cand path on voice).
  TG **30302** slot **0** @ 421.225. Companion-louder **0**. Voice SNR
  median **16.7 dB**. Operator: promising start, then unusable.
- Timeline: dutySec up to **0.60** then cliff ~20:00:18 drop **A**; return
  to CC 20:00:22. Talk median dutySec **0.40**, drop **D** dominant while
  speaking.
- Exact fail: rolling soft-trim protected only **80 ms**; live eyes fell
  from DEC-0009 **360 ms** to **160 ms** (110 hops). DEC-0009 already
  proved 160 ms eyes lose the lattice. Hard-cap then jumped the decode
  cursor under 2.6–2.9 s backlog.
- File voicetest of the same call skip=11000: `PASS_CONTINUOUS_AUDIO
  duty=0.685` — IQ is continuous; live path destroyed the eye.
- **DEC-0023:** protect **280 ms**; active rolling **4.0 s**; hard-cap must
  not jump cursor while protected. 105622 still duty **0.645**. Live
  re-prove required on new GUI capture.

---

## 2026-09-08 — Live audio diagnosis: 082235 + rejected speed trials

- Capture `20260908_082235` (stock **0.2.51**, `cqpskCand=32`): CC better
  (first follow ~10.5 s). Talk median dutySec **~0.24**; drop **D** on talk
  seconds; **54** worker-busy; eyes still **80+280** (`fresh=163840
  context=573440`); first job often **720 ms** cold. Companion-louder /
  DEC-0012 still heavy. File IQ of the same class still passes continuous
  because CLI **waits** (~17 s wall / 8 s span).
- **Not guessing hop/TTL.** Measured and rejected:
  - **DEC-0020** sustain 80/0 block eyes: wall 2.8 s, 105622 duty
    **0.645→0.055** drop=A.
  - **DEC-0019 cand=3** after speak (voicetest live proxy): wall ~7 s,
    duty **0.055** drop=A. Hard hint early-stop **kept**; cand stays **8**.
  - **DEC-0021/0022** streaming DDC: after first emit, wrong-slot then
    `p2vcw=0`. 160 ms sustain only reached duty **0.125**. Default-on
    still forbidden (DEC-0014).
- Desktop build retains DEC-0019 **hard** CQPSK hint stop only. **No
  release / push.** Operator must run `build\bin\Release\SDR_Town.exe`
  (not installed 0.2.51) for the next live CADENCE prove. T-0010 open.

---

## 2026-09-08 — Capture 075858: weak CC RF, not a DEC-0019 regression

- Capture `20260908_075858` (199 s, SNR **10.9 dB** vs 060221 **16.9**).
  GUI log `version=0.2.51`, `cqpskCand=32`, sticky=no — **stock release**,
  not the uncommitted DEC-0019 desktop build.
- **CC:** NID BCH-fail 21 vs 3; nid=none 9.6% vs 5%; ~86 s to first follow
  (vs ~12 s). High-correction grant TG 11108 waited hits=1/2 and never
  followed. Expected one-RTL CC pause while on traffic.
- **Voice:** same drop-D islands (talk median duty ~0.31, worker-busy 104).
  File TG 30017 slot 1 skip=85000 duty=**0.66**; TG 20202 skip=133000
  duty=0.50. Late TG 10330 grant=clear but file slice
  `PASS_ENCRYPTED_GATED` — live hung ~39 s without return-to-CC.
- No code change from this capture. Continuity hole remains T-0010 / DEC-0012.

---

## 2026-09-08 — Capture 060221 + DEC-0019 (hard hint stop; no release yet)

- Capture `20260908_060221` (148.8 s): same class as 053241 — DEC-0012
  companion-louder `fed=0` (14/14) + drop **D** (worker-busy 100, dsp
  median 161 ms). LO parks are clean 11.25 kHz. Not a tuner miss.
- SDRTrunk slot model: one HDQPSK stream + two AudioModules filtered by
  timeslot; no opp>target mute; no per-hop CQPSK grid.
- File peak TG 30302 slot 1 skip=128500: duty **0.922** continuous — IQ
  has the voice. First TG 10330 live call never opened (ESS/MAC gate then
  no-vcw); file skip=12000 duty 0.16.
- DEC-0019: block CQPSK hint with **hard** lock stops the grid; hot
  candidates capped at 3 after emit. Soft early-stop rejected (105622
  0.305). File duties match 0.2.51. **No push** until live CADENCE
  improves on a new GUI capture (T-0010).

---

## 2026-09-08 — Capture 053241 short islands are DEC-0012 + drop D (no push)

- Operator on v0.2.51 + `20260908_053241`: less garble, very short emit
  windows. Live: 151 s gapless, dual-TG same RF (TG 12068 slot 1 + TG 30003
  slot 0 on 421.975, LO 421.96375). CADENCE talk-seconds median duty ~0.30;
  worker-busy ~91; DSP median ~161 ms.
- Live DSP worker: 12 companion-louder `mac0 sf>0` hops all `fed=0` (DEC-0012
  hop-wide mute). Emit islands median length 1. Isolation is doing its job;
  dual-TG makes companion louder *normal*, so selected speech becomes short
  islands whenever the other TG dominates.
- Tried narrowing DEC-0012 to **overlap/context only** (fresh selected still
  feed). File voicetest: 053241 TG 30003 slot 0 skip=0 center=421.96375
  `PASS_CONTINUOUS_AUDIO duty=0.715` with companion-louder `fed>0`; but 105622
  fell to **duty=0.62** (stable on two reruns), 073304 **0.72** (was 0.795),
  and 041716 lost `unknown-waiting-clear` (8 companion-louder fresh emits —
  isolation regression). **Reverted.** Isolation outranks duty (SoT). Do not
  push. Next: live drop **D** (T-0010), not reopening DEC-0012.

---

## 2026-09-08 — DEC-0012 feed-only: companion-louder mixed MAC-dead after emit

- Capture `20260908_041716` (70.75 s gapless, SNR ~18 dB). Operator: one
  good emit, then wrong-slot garble. Call 2 seq=131 `opp=0 p2mac=5/6`
  `trusted-clear-pending-release`; seq=134 `target=6 opp=12 p2mac=0/0
  ess=clear` still `gate=emit`. DualSlotUntrusted stayed false (004206 ESS
  escape).
- DEC-0012 landed as **feed-only**, not hop `audio.clear()`. After the call
  has spoken, mixed windows with no selected MAC CRC **and companion louder**
  (`oppVcw > targetVcw`) do not feed unproven bursts.
- Voicetest after the companion-louder narrow (not the first blanket mixed
  mute, which dropped 105622 to 0.27):
  - 041716 TG 10330 slot 1 skip=32111 center=421.21375 8 s:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.87`. seq=134-class hops are
    `unknown-waiting-clear`. Selected-only / selected-dominant hops still emit.
  - 073304 TG 10330 slot 1 skip=107597 center=420.975: `PASS_CONTINUOUS_AUDIO
    duty=0.795` (was 0.76).
  - 105622 TG 30003 slot 0 skip=97334: `PASS_PARTIAL_AUDIO drop=D duty=0.645`
    (was 0.685; two frames under 0.65). Slot 1 same IQ duty=0.09 (was 0.11).
- Live drop **D** (worker-busy 4.5× CQPSK) unchanged. Equal mixed
  (`target==opp`) still ESS-authorized (T-0004). Did push 0.2.51: 041716
  isolation + 073304 duty improved; 105622 stays just under continuous.

---

## 2026-09-08 — DEC-0017 rejected; DEC-0018 does not recover streaming 105622

- Restored DEC-0009 80+280 after DEC-0017 360/0 independent eyes:
  105622 duty 0.685→0.35, 123525 skip=20000 0.64→0.325.
- Re-prove after revert: 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`.
- DEC-0018: streaming DDC no longer jumps the dibit lattice to RF-sample
  time. Env=1 on 105622 is still `PASS_PARTIAL_AUDIO` (0.16/0.09/0.045).
  Do not default-on. Live drop D remains 4.5× overlap CQPSK (T-0010).
- Capture cleanup: deleted 115315 (997 kHz edge), 010500, 064319, 104042,
  161748 (~14 GB). Folder now ~11 GB with the regression set plus a 90 s
  GUI capture `20260907_194706`.
- GUI `--gui-grant-test --p25-cc 420.475 --gui-start-iq-capture` 90 s:
  TG 30302 417.675 LO 417.66375. CADENCE on talk 0.94 / 0.80 then 0.11–0.52
  drop D/A. One worker `dsp=531 ms`. Not an improvement over 123525. Did
  not push.

---

## 2026-09-07 — 123525 capture: LO parked, emit gaps are drop D (+ opposite-slot 80 ms eyes)

- Capture `20260907_123525`: 258 s gapless, SNR ~15 dB. SigMF meta stays
  `center=420.475` (start LO); physical tuner retuned. First call TG 30003
  417.675 `rfCenter=417.66375` offset 11.2 kHz (`single-rtl-retune`).
- Live CADENCE 191 s: emit on 88 s, median duty **0.34**, only 7 s ≥0.65.
  On emit seconds drop **D** 59 / **A** 22 / **ok** 7. `fed≈emit` (1602/1608).
  `dups=2262` vs `target=3892`. Worker `iq=737280 fresh=163840 context=573440`
  (80 ms + 280 ms overlap). Ring `bridge=960` + climbing underruns.
- File voicetest same slice skip=20000 8 s `center=417.66375` slot 0:
  `PASS_PARTIAL_AUDIO drop=ok duty=0.64` (0.01 under the bar)
  `fed=260 emit=260 ambe=228/260 gaps=15`. Many hops `wrong TDMA slot`
  with `oppVcw` only. Not 997 kHz edge. Do not loosen quality/TTL.
  Next named hole: 80 ms independent eyes + overlap dups (T-0006 / T-0004),
  not a new LO.

## 2026-09-07 — 22:27 live follow: LO correct, gaps are soft-quality extract

- GUI `22:27:17`–`22:28:20`. No start/stop IQ file (`capture writer` never started).
- DEC-0016 park worked: TG **10128** voice **420.725** `centerFreqHz=420713751`
  `effectiveTargetOffsetHz=11249`.
- 29 validation hops: `targetVcw=212` `fed=190` `emit=102` `pcm=3.8s` over ~36 s
  (`duty≈0.11`). `iqRej=88`. Gate `hard-soft-quality-low` / `no-decoded-frames`
  / `Phase 2 AMBE rejected`. Eye `softDecisionQuality` mostly 0.33–0.42.
  Drop **A** (extract quality), not off-center tune. Need a real IQ capture
  to voicetest; do not loosen the soft-quality gate from this 1-minute listen.

## 2026-09-07 — 115315 bad audio is 997 kHz edge LO, not a missing gate

- Capture `20260907_115315`: 162 s gapless, SNR ~17 dB. Operator: really bad.
- DEC-0015 used SDRTrunk *set* `getCenterFrequency({cc, voice})`. TG 30003
  421.975 + CC 420.475 `canTune` (span 1.51 MHz < 2.007 MHz usable) →
  `rfCenter=420.97773` **offset=997.3 kHz**. CADENCE drop=D ~0.45 s then
  drop=A no-vcw. Same LO reused for 421.225. Capture 005246 already showed
  750 kHz-offset CQPSK dies.
- DEC-0016: follow LO is always the single-channel voice park (~11 kHz).
  Keep CC only if that LO still `isTunedFor` CC; else pause CC. No
  hop/TTL/feed change.

## 2026-09-07 — Follow LO matched SDRTrunk CenterFrequencyCalculator

- Operator: some follows looked off-center.
- Old park was `voice ± 250 kHz` and reuse of a CC-centered tuner whenever
  voice sat inside ~512 kHz. That is not SDRTrunk. Single-channel SDRTrunk
  center is `voice − 11249 Hz` (12.5 kHz channel + R820T 5 kHz DC hole + 1).
  CC+traffic on one tuner recomputes the LO so neither channel overlaps DC
  (095450: 420.475 CC + 420.225 voice → rfCenter ≈ 420.21375 MHz).
- Code: `include/P25SdrtrunkTune.h` + `p25Phase2LowIfTrafficCenterHz` wrapper.
  Same-wideband follows nudge without pausing CC. Physical one-RTL /
  dedicated / CLI waitgrant use the single-channel park. No hop/TTL/feed
  change. Streaming DDC stays default-off (DEC-0014).
- File voicetest of 105622/073304 still uses capture `--center`, not this LO.
  Re-prove after rebuild: 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`;
  073304 `duty=0.76 timelineOk`. Close/reopen GUI on
  `build\bin\Release\SDR_Town.exe` to see the new LO. Isolation on 095450
  dual-TG remains T-0004.

## 2026-09-07 — 095450 slot bleed is mixed independent eyes, not a missing TTL

- Operator capture `20260907_095450`: dual grant TG 30013 slot 1 + TG 30003
  slot 0 on 420.225 MHz. Sounded like slot bleed, wrong cadence, timing.
- SDRTrunk HDQPSK.receive is one contiguous Costas+Gardner stream, 2048
  samples @ 25 kHz ≈ 82 ms, one AudioModule per timeslot, queue until
  PTT/ESS. Our live path re-channelized 360 ms overlapping eyes (`dsp`
  327–573 ms, seq=2 `decode-wall-timeout`) and fed `targetVcw≈oppVcw`
  with `p2mac=0/x` `ctxDrop=0`.
- **Tried** default-on streaming DDC (DEC-0014). Voicetest 105622 TG 30003
  slot 0 skip=97334 fell duty **0.685 → 0.095** (`emptyWindows=80/89`).
  Same eye-loss as 20260830. Reverted default-on. Env `=1` still opts in;
  locked streaming hops are 80 ms not 40 ms.
- **Kept:** CADENCE `1s` no longer dumps 53 s of hops as one second
  (095450 `windows=593 dutySec=2.320` was `lastLogMs==0`).
- Re-prove after revert: 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`;
  073304 TG 10330 skip=107597 `duty=0.76 timelineOk`.
  095450 TG 30013 slot 1 skip=3741: `PASS_PARTIAL duty=0.625`
  `oppAmbe=759/920` `plc=180/290 concealmentOk=no` — companion still
  reaches the selected vocoder on MAC-dead mixed windows (T-0004 /
  DEC-0012 deferred: that mute dropped 105622 to 0.38).
- Restart GUI on `build\bin\Release\SDR_Town.exe`. T-0010 live CADENCE
  still needs a listen on this binary.

## 2026-09-07 — 073304 repeats were overlap replay, not a missing timeout

- Live `20260907_073304`: audio better after DEC-0008/0009, still garbled +
  repeats. TG 10330 seq=389 ctxDrop=0 after a good emit; CADENCE dups=78–100/s.
- DEC-0010/0011 lock-only after emit stopped those repeats but starved 105622
  missed-eye catch-up (duty 0.735→0.11). Superseded.
- DEC-0013: de-dupe by ISCH lattice `(slot, burstIndex, voiceIndex)` within
  1800 dibits (~300 ms). 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`. 073304
  repeat slice `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`. Mixed MAC-dead
  garble (DEC-0012) deferred — applying it dropped 105622 to 0.38.
- Restart GUI on `build\bin\Release\SDR_Town.exe` to hear it. T-0010 live
  re-prove still open.

## 2026-09-07 — Phase 2 extract: sticky lattice was walking the wrong eye

- ISS-0001 voicetest on `20260905_105622` TG 30003 slot 0 skip=97334 8 s was
  `PASS_PARTIAL_AUDIO drop=D duty=0.28` (fed=112). Hop 2 (720 ms) had real
  Voice2/4; sustain hops were `p2sf/p2mask` with `p2vcw=0` or wrong-slot.
- Cause: block-channelize resets CQPSK/Gardner every hop, then the sticky
  stream-space superframe walk reused the previous eye’s dibit lattice
  (DEC-0008). 160 ms sustain eyes often locked one burst (DEC-0009).
- Fix: sticky lattice / anchor-aligned locks only when streaming DDC is on.
  Locked block-channelize sustain overlap 280 ms so overlap+fresh = one
  360 ms superframe. Hop/minFresh unchanged (80/40).
- Gate: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735` (fed=294, p2macCrc=122).
  Same IQ slot=1 duty=0.11. Live CADENCE on 161748 still T-0010.
- BN-0002: rebuilt Release `SDR_Town.exe` + tests.

## 2026-09-07 — Bring Athanor method to SDR Town; stop P25 hotfix circle

- Athanor repo not edited. Copied process only (DEC-0001).
- Holistic read: analog/CC/follow work; Phase 2 voice is partial (~50%).
  Root pattern is gates fighting (continuity vs anti-garble), overlap+dedupe
  starving unique frames, and ring/bridge filling islands — not one missing
  timeout.
- Landed SoT, cause/effect map, trackers. Retracted roadmap “continuous done”.
- ISS-0001 remains open: need a HEAD voicetest/live CADENCE `drop=` before
  any feed/emit/playout patch (T-0002).
- Started REQ-P2.0 classifier so the next session cannot honestly guess.
- BN-0001: MSVC 19.44.35227 Release; 206 unit tests / 10166 assertions PASS;
  SDR_Town.exe compiles. AppData captures classified 2026-09-07: dominant live
  CADENCE is A (`no-vcw-from-live-window`); HEAD voicetest of 105622 TG 30003
  is PASS_PARTIAL_AUDIO drop=D duty=0.28. Opposite-only dual-slot-untrusted
  predicate corrected (DEC-0003); duty on that slice did not move.

---

## Older P25 work

See `docs/p25_phase2_regression_tracker.md` and `src/*NOTES.md` for July–August
2026 hotfix archaeology. New facts go here.
