# P25 Phase 2 continuity fix — capture 20260720_070533

## Capture

- Dir: `iq_test_captures/20260720_070533_207_iq_NFM_419_12500MHz_419.12500MHz_startstop`
- ~67.12 s gapless IQ @ 2.048 Msps, CC 419.125, voice 419.875
- Dual same-RF TDMA: TG **10330 slot1** early, then TG **10120 slot0** (held through 8 s acquisition grace)

## Full-chain diagnosis (early-bad vs late-clear)

| Stage | Early (TG10330 s1) | Late (TG10120 s0) |
|---|---|---|
| Tuner/retune | Same wideband low-IF | Same |
| CQPSK / mask | Settling; ess often unknown | Settled; ess=clear, targetSession=yes |
| Slot map | Physical slot1; opp slot0 busy | Physical slot0; targetSession proof |
| AMBE feed | fed≈emit; some E/M drops | Higher unique feed; fewer islands |
| PCM gate | clear-grant release | trusted session + ESS |
| Live speaker | **Ring starve 250–1000 ms between emit islands; underruns 879→1711** | Same mechanism, lower underrun delta |
| Voicetest (post-fix) | PASS_PARTIAL duty **0.40** (7.18 s / 18 s) | PASS_CONTINUOUS duty **0.76** (19.02 s / 25 s) |

**Discontinuity root cause (not invent-PLC):** between selected-slot emit windows the AudioEngine ring drained to underrun. The clock-only silence playout bridge was too thin (60 ms target / 20 ms/tick) and aborted after only 8 empty-feed windows — normal for TDMA opposite dwell + worker empty-audio. Mid-call re-prime then waited another 120 ms, reopening audible holes. Island **tail glitches** were underrun clicks / hard sample→silence splices (plus occasional mid-island E/M frame cuts).

Invent PLC / opposite-slot sustain remain **disabled** (SDRTrunk-aligned).

## Fix (why / how / U / D / outcome)

1. **Why:** join real selected-slot speech islands without inventing companion-slot audio.
2. **How:**
   - Silence bridge target **220 ms**, max **80 ms**/tick
   - Empty-window bridge kill relaxed to **≥24** empties after **900 ms** (hard stop still **1250 ms** since last real emit)
   - Mid-call hot re-prime **40 ms** when ring is empty but fresh PCM is ready (cold prime still 120 ms)
   - Soft-join: ramp last real sample → 0 over ~3 ms into silence bridge
   - E/M spike reject mid-window pads **silence** (not last-good PLC) to keep 20 ms cadence
3. **Upstream:** unchanged dibit/CQPSK/slot/ESS/AMBE path; invent-PLC still denied.
4. **Downstream:** ring stays clocked across worker holes → fewer underruns; tails soft-join into silence; next real AMBE still plays immediately.
5. **Outcome:** Catch2 `[p25]` PASS; voicetest early PARTIAL / late CONTINUOUS WAV proof; live path ready for user listen verify.

## Files

- `include/P25ReceiverSession.h` — last-emitted sample on audio tail
- `src/main.cpp` — bridge, hot prime, soft-join, E/M silence pad
- `src/tools/verify_p25_phase2_playback_ring_target_fill.py`
- `src/tools/verify_p25_phase2_scheduler_optimizations.py`

## Proof artifacts

- `build/p25_vt_070533_early_tg10330_s1.wav` / `.txt`
- `build/p25_vt_070533_late_tg10120_s0.wav` / `.txt`
- `build/_p25_tests_070533_continuity.txt`

## User verify

1. Rebuild/run Release GUI on same capture or live follow.
2. Listen: speech islands should no longer click/pop between segments; tails should soft into quiet, not splat.
3. Watch P25 log: `underruns=` should climb far more slowly during active clear calls; `ringFill` should sit near the bridge floor (~20%+) between emits instead of collapsing to 0.
4. Confirm no opposite-slot “ghost” speech (`fed` must track real target AMBE; invent-PLC still off).

## Follow-up

Round 1 bridge alone was not enough. See `docs/P25_PHASE2_070533_CONTINUITY_ROUND2_NOTES.md` for deeper gap quantification, hop20 rejection, and ring/pacing/prestage fixes.
