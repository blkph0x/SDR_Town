# P25 Phase 2 continuity — capture 20260720_070533 (round 2)

## Capture

- Dir: `iq_test_captures/20260720_070533_207_iq_NFM_419_12500MHz_419.12500MHz_startstop`
- ~67.12 s gapless IQ @ 2.048 Msps, CC 419.125, voice 419.875
- Dual same-RF TDMA: TG **10330 slot1** early, then TG **10120 slot0**
- No newer capture than 070533; re-analyzed with current binary after round-1 silence bridge

## Full-chain gap map (grant→burst→feed→PCM→pending→push→ring→speaker)

| Stage | Finding (070533 hopms=40) |
|---|---|
| Tuner / IQ | Gapless capture; not the hole source |
| CQPSK / mask / ESS | Early settling → late settled clear + targetSession |
| Selected-slot VCW | Early: emit median gap **80 ms**, but **7 gaps ≥500 ms** (max **3600 ms**). Late: median 80 ms, max **710 ms** |
| Wrong-slot / empty hops | Opposite-slot dwell + waiting windows are normal; long droughts often show `oppVcw>0` / `targetVcw=0` (companion busy, our TG quiet) |
| AMBE / gate | invent-PLC still OFF; E/M → silence pad (20 ms cadence), not last-good |
| Pending→push | Round-1 could withhold sub-80 ms pending while ring drained → underrun |
| Silence bridge (r1) | 220 ms floor / 80 ms tick / 1250 ms hard stop — **too thin** vs early droughts and live starve |
| Speaker ring | Underruns when fill hits 0 between emit islands |

**Root cause of remaining disconnects (not invent-PLC):**

1. **Live ring starve** between selected-slot emit islands when pending push / bridge floor cannot cover hop variance (p90 ~160 ms) and worker holes.
2. **Multi-second selected-slot emit droughts** (early especially) — often correct dual-slot silence while the opposite slot carries other traffic; bridge cannot invent speech. After hard-stop the ring underruns until the next real emit.
3. **Tried 20 ms hops** (voicetest hopms=20 / proposed speaker sustain 20 ms): **regressed** early duty 0.40→0.34 and late 0.76→0.64 (target VCW recovery fell). Kept **40 ms** eyes.

Invent PLC / opposite-slot sustain remain **disabled** (SDRTrunk-aligned). Dual-slot opposite silence is correct.

## Fix round 2 (why / how / U / D / outcome)

1. **Why:** join selected-slot speech without manufacturing companion-slot audio; stop the ring hitting 0 on normal hop/worker holes.
2. **How:**
   - Jitter cap **0.85 s**; live target fill **500 ms**, push ceiling **720 ms**
   - **Low-water push:** when ring &lt; 100 ms, allow single **20 ms** frames out of pending (healthy ring still prefers 80 ms batches)
   - Silence bridge target **380 ms**, max **120 ms**/tick; hard stop **1600 ms**; empty-window kill **≥32** after **1100 ms**
   - Speaker decode: cadence **3 ms**, pending jobs **2** (one in-flight + one prestage); sustain chunk stays **40 ms**
   - Soft-join + E/M silence pads retained; invent-PLC still denied
3. **Upstream:** CQPSK/slot/ESS/AMBE unchanged; 20 ms hop experiment rejected (hurts VCW recovery).
4. **Downstream:** deeper pre-buffer + faster bridge refill + low-water pending drain → fewer underruns between hops; multi-second TG pauses still silent (correct).
5. **Outcome:** Catch2 `[p25]` PASS; verify scripts PASS; voicetest early PARTIAL / late CONTINUOUS at hop40 (decode duty unchanged — expected); live path ready for listen verify.

## Metrics before / after

| Metric | Before (r1 bridge) | After (r2) |
|---|---|---|
| Early voicetest | PASS_PARTIAL duty **0.399** (7.18 s / 18 s) | same **0.399** (decode path unchanged @ hop40) |
| Late voicetest | PASS_CONTINUOUS duty **0.761** (19.02 s / 25 s) | same **0.761** |
| Early emit gaps ≥500 ms | 7 (max 3600) | same (RF/talker structure) |
| Early WAV holes ≥100 ms | 5 (max 270) | same |
| Late WAV holes ≥100 ms | 11 (max 210) | same |
| hopms=20 experiment | — | early duty **0.341** / late **0.643** — **rejected** |
| Live ring / underruns | 220 ms bridge; pending could stick &lt;80 ms | 380 ms bridge + 500 ms target + low-water 20 ms push + 2-job prestage |

## Files

- `src/AudioEngine.cpp` — jitter 0.85 s
- `src/main.cpp` — push pacing, bridge, speaker cadence/pending
- `src/tools/verify_p25_phase2_playback_ring_target_fill.py` (+ related verify scripts)
- `docs/P25_PHASE2_070533_CONTINUITY_FIX_NOTES.md` — round 1
- This file — round 2

## Proof artifacts

- `build/p25_vt_070533_early_tg10330_s1.wav` / `.txt`
- `build/p25_vt_070533_late_tg10120_s0.wav` / `.txt`
- `build/p25_vt_070533_*_cont2.*` — hop20 experiment (worse; keep for evidence)
- `build/_p25_tests_070533_continuity2.txt` — Catch2 `[p25]` PASS

## Honest limits

- Opposite-slot dwell silence (~30 ms TDMA) is correct; do not invent companion speech.
- Multi-second early droughts with `targetVcw=0` while `oppVcw>0` are usually **other-slot traffic / our TG quiet**, not a ring bug.
- Voicetest WAV concatenates emit PCM only (no wall-clock silence); duty reflects recovered speech vs IQ span, not live underruns.
- Bridge/pacing fixes live underrun clicks and short hop holes; they cannot turn a silent selected slot into continuous talk.

## User verify

1. Rebuild/run Release GUI on 070533 or live follow.
2. During clear calls watch `ringFill` stay off the floor between emits; `underruns=` should climb much more slowly.
3. Expect soft tails into quiet, not splat/click; no opposite-slot ghost speech (`fed` tracks real target AMBE).
4. Long dual-slot pauses may still sound like islands — that is correct silence, not PLC.
