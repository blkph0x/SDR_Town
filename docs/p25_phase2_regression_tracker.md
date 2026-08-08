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

### 2026-08-08 — Sprint 0 P25 Phase 2 clear TX shell (no RF)

**Program:** Full P2 trunk clear TX (PTT) — multi-sprint. Sprint 0 only.

**Delivered:**
- `P25TxConfig` arm gates (RID/TG/NAC + device index; clear-only)
- `P25TxSession` pure SM: Idle→Armed→Requesting→WaitGrant→Tuning→Voice→Hang
- Encrypted grant refused; max PTT timeout path
- DeviceManager `DeviceInfo::canTx` + TX antenna probe (no writeStream)
- GUI: Arm + hold-PTT panel under P25 controls (logs SM only)
- CLI: `tx status|arm|disarm|config|ptt on|off`
- Unit tests `[p25][tx]`

**Not yet:** mic, AMBE encode, superframe TX, H-CPM, writeStream, trunk request RF.

### 2026-08-08 — Garble from latch bleed + dual-slot untrusted (015254 / 021134)

**Field:** two captures post-dae961e. More `gate=emit` (169 on 021134) but audio
mostly **garble/blocky** with rare clear words over 5–10 min.

**Evidence:**
- 62/151 emits `ess=unknown/unknown` with `action=trusted-clear-release`
- Dual-slot: `tv=8 ov=6 fed=8 ws=6 mac=0/0` (and worse) still emitted
- CADENCE `block=vcw-present-but-no-sf-mask-yet` while emit>0 (64×)
- TG handoffs 30304→30302→12542 kept Clear latch / hadSuccessfulEmit

**Root causes:**
1. Call-boundary dedupe reset did **not** clear `callSecurityLatch` or
   `sustain` → prior call Clear opened feed on new TG without ESS/MAC.
2. Dual-slot untrusted garble gate returned false once mbelib was already fed.
3. stickySuperframe-only feed trusted wrong XOR epoch after re-lock.

**Fixes:**
- Call identity change: reset latch, sustain, pending, recent security.
- Dual-slot untrusted mutes speaker even after feed; requires ESS/MAC+structure.
- Feed trusted requires maskPhaseLock/MAC or real superframeLock (not sticky-only).
- Latch/post-emit open only with hard epoch and not dual-slot-untrusted.

**Watch:** near-zero `trusted-clear-release` with ess=unknown on dual-slot;
clear words continuous when targetEss=clear; no garble wash between TG hops.

### 2026-08-08 — Continuous clear feed + CQPSK re-lock (012422)

**Field `20260808_012422_300` (post speaker-grace/rolling fix):**
- ~7.6 min capture; **gate=emit=10** islands only; dutySec max ~0.48 then drought
- **All logged VOICE WORKER windows with p2vcw>0 had fed=0** (incl. mac=2/2, ess=clear)
- After each island: p2bursts=0 for tens of seconds (dsp ~20–30 ms lock-starved hops)
- ACQ watchdog still ~12s after emit; preempt after 37s “no decoded audio”
- clearKnown=yes / callClearTrusted throughout waiting-clear-grant droughts

**Root causes (confirmed in code+log):**
1. `securityProvedClearForFeed` required ESS/session every hop; continuous-clear
   OR-list (latch / clearKnown structure) sat **behind** that gate → Voice2/4
   with xor/sf/mask and even mac=2/2 never fed mbelib.
2. Security audio gate `trustedClear` ignored Clear latch alone → decoded PCM
   wiped when hop had ess=unknown.
3. Post-emit hot CQPSK budget 6@50ms cannot re-lock after block-channelize
   clears Costas (empty streak stayed at p2bursts=0).
4. Speaker follow/preempt grace 5s still shorter than empty-hop re-acquire.

**Fixes:**
- Open securityProvedClearForFeed on Clear latch, post-emit clear grant, or
  clear-grant + target MAC CRC; continuous feed ORs the same.
- Drain pending AMBE on latch/MAC/post-emit (not ESS-only).
- trustedClear accepts latch Clear (and post-emit clear grant).
- Empty-streak CQPSK escalate to cold 160ms/32 after emit; speaker-hot 90ms/16.
- Speaker grace + preempt hold 15s; continuationAnchor uses effectiveLastActive.

**Watch:** fed≈targetVcw on clear islands; multi-second dutySec; no ACQ return
within 15s of emit; empty streak re-locks (p2bursts>0) without CC bounce.

### 2026-08-08 — Follow SM false return + rolling stuck (010625)

**Field `20260808_010625` (post quality-passband retune):**
- emit=3 empty=626 busy=312 waiting-fresh=265 absKnown=no storm
- ACQ watchdog "no Phase 2 VCWs" **13–18s after real gate=emit audio**
- Preempt stalled after 67–164s "no decoded audio" despite earlier speaker PCM
- Quality retune 418.625→419.875 **did** fire (rfCenter=419.625) — good
- 418.875: sumVcw=58 sumFed=0 (waiting clear grant)
- absDup+fed0=0 (success-only remember holds)

**Root causes (confirmed in code+log):**
1. `recentSpeakerOutput` computed in follow SM but **never used** → false
   ReturnNoVoiceCodewords after empty diagnostic windows.
2. Stall preempt used current diag window only (no speaker grace).
3. `takeUndecoded` with absKnown=no could sit at live edge with minFresh unmet
   while rolling ballooned to 4–6M samples.
4. waiting-clear-grant with clear latch / ESS not always continuous-feed.

**Fixes:**
- Follow SM: speaker grace 5s blocks all return-to-control actions; lastActive
  includes recentSpeakerOutputMs; voiceStillLooksActive ORs speaker.
- Preempt: require !recentSpeakerHold (5s).
- takeUndecoded: recover sample-index cursor; soften minFresh at live edge.
- continuousSelectedClearFeed: open when p25VoiceClearKnown + structure.

**Watch:** zero watchdog-after-audio; waiting-fresh rare; fed≈target while clear.

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
