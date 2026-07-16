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
