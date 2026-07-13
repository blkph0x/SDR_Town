# TDMA MAC/ESS Watchdog + Late-Entry ESS Soft Acquisition

This build targets the state observed after buildfix9:

- Voice frequency and passband are correct.
- Phase 2 superframe lock is present (`sf=12`).
- XOR mask is being applied (`mask=12`).
- MAC remains `0/0` and ESS remains `unknown`.
- Auto-follow can stay pinned to the stale voice talkgroup after traffic ends.

## Changes

### 1. Late-entry ESS soft acquisition without MAC PTT

`P25LiveDecoder.cpp` now keeps five internal ESS hypotheses for possible first-4V slot positions.

When a Phase 2 burst has superframe/mask lock but no MAC PTT has established `first4vSlot`, the decoder now tries all valid first-4V hypotheses and accumulates ESS B fragments independently. If one hypothesis successfully decodes ESS, it promotes that hypothesis to the active session.

This does **not** open audio by itself. Audio is still gated until ESS proves clear and the AMBE frame path passes existing safety checks.

### 2. Auto-follow no-progress watchdog

`main.cpp` now detects the specific stalled state:

- TDMA superframe/mask lock present,
- no valid MAC CRC,
- ESS unknown,
- no decoded audio,
- enough time has passed since the retune.

If this persists, the GUI returns to the control channel instead of hanging indefinitely on the voice frequency.

New log line:

```text
TDMA ACQ watchdog: sf/mask lock present but MAC/ESS did not progress ... returning to control channel
```

### 3. No-VCW post-retune watchdog

If the app retunes to a Phase 2 voice frequency but no Phase 2 VCWs appear after the initial acquisition window, it returns to the control channel instead of remaining stuck.

## What to check next

After rebuilding, look for one of these outcomes:

1. `ess=clear` appears after `sf=12 mask=12` — late-entry ESS recovery is working.
2. `TDMA ACQ watchdog... returning to control channel` appears — no hang, but MAC/ESS still needs deeper parser work.
3. `mac` becomes nonzero but `ess` remains unknown — MAC extraction is partially working and ESS parsing is the next target.
