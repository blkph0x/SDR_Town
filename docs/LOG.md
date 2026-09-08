# Development log

Newest at the top.

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
