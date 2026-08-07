# P25 Phase 2 regression tracker

Track intentional policy and cadence changes so field regressions are easy to bisect.

## Field evidence (2026-07-15 captures)

| Observation | Implication |
|-------------|-------------|
| Repeated `Auto-follow deferred Phase 2 … OP=0x02 unknown update; waiting for OP=0x00` | Almost no retunes: systems re-issue OP=0x02 for the call, OP=0x00 often never reappears |
| `p2vcw=706` `dupSuppressed=666` `decodedFrames=40` over ~8s voicetest | Overlap re-extraction is expected; unique feed was only ~40 frames (~0.8s audio) → sparse unique feed + gate OK |
| Live DEEP DIAG `decode=no phase2=no maskParamsKnown=no` after grants | Follow never armed → no mask → no voice path |
| `gate=explicit-clear-grant-validated-release` with `speaker=empty-audio` | Gate open, but mbelib feed empty (dups or no forward abs) |

## Changes

### 2026-08-08 — Streaming DDC on realtime Phase-2 + backlog catch-up (chop/fast)

**Field:** capture `20260807_232020_926` — feedRatio≈1.0 on TG30302 but dutySec≈0.24–0.48;
audio islands of 80 ms every 200–350 ms (“faster” but broken/choppy). Worker logs:
`iq≈106k fresh≈65k` (~32 ms RF), `cqpskCandidates=32`, `demodState=Cold`,
`totalMs=200–400`.

**Root causes (confirmed):**
1. `enableStreamingChannelDdc=false` forced **block channelize**, which clears CQPSK/
   framer/mask-phase every `processIq` hop and re-runs a 32-candidate cold search.
2. Speaker sustain only advanced ~32–60 ms RF per hop → at most one Voice4 when lucky.
3. `p25Phase2UndecodedBacklogSamples` returned 0 without absolute cursor → catch-up never ran.

**Fixes:**
- Realtime Phase-2 voice config enables **streaming channel DDC** (forensic stays off).
- GUI/CLI chunk planner uses streaming plan (overlap=0 contiguous fresh) when DDC on.
- Speaker sustain 120 ms / catch-up 200 ms; backlog threshold ~40–50 ms on clear path.
- Backlog helper uses `effectiveDecodeAbsolute()` for absolute and sample-index modes.
- CLI `activeSpeakerClearPath` includes `phase2SessionHadBurstEye` (GUI parity).

**Watch:** CADENCE `dutySec` → talk-time (near 1.0 while speaking); `decodeProfile`
`demodState` not stuck Cold; worker `context=0` after first eye; sticky CQPSK logs.

### 2026-08-01 — Streaming continuity + MAC lock + security sticky (P0/P1 audit)

**Field:** capture `20260801_100006_265` — clear `gate=emit` islands then drought; **417x** `waiting-fresh-iq` with `minFresh=1474560` (== full rolling 720 ms); `p2mac=0` dominant; worker-busy with mega `dsp=656ms` jobs.

**Root causes (confirmed):**
1. After first job advanced the decode cursor via `markDecodeSubmitted`, firstColdEye / wide-reacquire still demanded **minFresh = 720 ms**, so small RF advances never met the bar.
2. Speaker backlog catch-up threshold was **720 ms**, producing monolith jobs then starvation.
3. `macCrcLock` FEC/CRC set was **overwritten** by session-only mask (`ptt|active|crc`), dropping FEC lock.
4. `trafficClearRelease` computed but unused for `sessionAudioRelease`.
5. Sticky security TTL refreshed on superframe/mask lock alone (no new MAC/ESS/PTT).

**Fixes:**
- Shared `p25Phase2PlanVoiceDecodeChunk`: after cursor advances, minFresh is sustain/acquire (20-120 ms), never full-window.
- Wide reacquire: large max context, **small** minFresh; post-cursor demotes cold eye.
- Backlog catch-up threshold **80 ms** speaker / **180 ms** otherwise; catch-up chunk **120 ms** max.
- Pending jobs floor **2** (3 when speaker live).
- `macCrcLock` ORs session activity; never clears FEC lock.
- `sessionAudioRelease` includes MAC_ACTIVE clear traffic SO (`trafficClearRelease`).
- Recent security TTL only on MAC/ESS/PTT evidence.

**Watch:** `waiting-fresh-iq` minFresh ~40-160k samples post-arm (not full rolling); CADENCE `dutySec` continuous during talk.

### 2026-07-16 — GUI freeze / "Not Responding" on follow

**Problem:** After follow for a while the UI froze; Windows reported the app unresponsive.

**Root causes:**
1. GUI thread called `waitForCenterTuneApplied(..., 650)` (busy-sleep up to 650 ms) on one-RTL traffic retune and same-call hops.
2. GUI paths used **blocking** `lock_guard` on `receiversMutex` / `stateMutex` while the Phase-2 voice worker held those locks for long decodes (retune apply, return-to-control clear, control mute, in-band target commit).

**Fix:**
- All GUI Soapy retune waits are **non-blocking** (`timeoutMs=0`); pre-roll absorbs pending tune.
- Retune / clear / mute / return-to-control / in-band target use **try_to_lock** + QTimer retry; never block the Qt event loop on DSP/state locks.
- Voice worker queue clear on return also try_to_lock.

**Log keys:** `p25-return-clear-busy`, `p25-same-call-tune-pending`, `p25-same-call-hop-state-busy`, `p25-retune-state-deferred` (existing).

### 2026-07-16 — OP=0x02 unknown retune (no hard defer)

**Problem:** Hard defer of all new follows on OP=0x02 until OP=0x00 made live clear voice rare.

**Change:** On `GroupVoiceUpdate` without service options, **retune** for a new TG/channel, but force `encryptionKnown=false` so sticky clear cannot open the speaker. Audio still waits for traffic-channel PTT/ESS/MAC (strict gate).

**Log keys:**
- Old (removed): `auto-defer-p2-op02-unknown`
- New: `auto-follow-p2-op02-unknown` — “retuning, speaker waits for traffic-channel PTT/ESS”

**Arm:** Phase 2 no longer promotes sticky TG clear alone into `p25VoiceClearKnown` (Phase 1 still may).

**Regression tests:**
- `python src/tools/verify_p25_phase2_clear_grant_op02_preservation.py`

### 2026-07-16 — Cadence rollup diagnostics (low overhead)

**What:** Relaxed atomic counters on each Phase-2 voice diagnostic publish; ~1 Hz GUI log:

```text
P25 CADENCE 1s: TG=… windows=… vcw=… targetVcw=… fed=… emit=… dups=… gaps=… reject=… feedRatio=… dutySec=… block=…
```

**How to read:**
- `feedRatio = fed/targetVcw` — near 0 with high `dups` means overlap re-decode (ok if `emit` still ~50/s when talking).
- `dutySec` ≈ `emit * 0.020` — should approach talk time (near 1.0 per second of continuous speech).
- High `reject` with low `fed` → gate/slot/mask path still filtering.
- `block=` same DEEP DIAG reason codes.

**Cost:** atomics + at most one log line/sec when follow status is active.

## Prior continuity work (summary)

- Status-strip clean 160-dibit payload; voice starts 1/38/86/123; swapped dibit packing; canonical mbelib interleave.
- Rolling IQ takeUndecoded advances fresh cursor (`firstNew + maxSamples`); pre-roll for context only.
- Established clear feed path; ordered burst feed; abs-dibit de-dupe for mbelib state.
- Explicit clear grant release / late-entry paths; encrypted fail-closed.
- Frame compare fields: `expVcw` / `fed` / `emit` / `gaps` on DSP/DEEP logs.

## Next targets if clear still rare after OP=0x02 fix

1. Mask seed on arm always (`p25MaskParamsKnown` from site registry).
2. Raise unique VCW density (DUID/Voice2/4 classification / mask phase).
3. MAC CRC recovery when sf/mask high (p2mac=0).
4. Ensure commitDecodeAbsolute always advances on successful job drain.
