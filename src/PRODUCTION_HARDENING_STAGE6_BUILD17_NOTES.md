# Production Hardening Stage 6 / Build 17

Focus: the remaining Phase 2 blocker after the receiver reaches stable `sf=12 mask=12` but still reports `mac=0/0 ess=unknown`.

## Changes

- Expanded Phase 2 ACCH/MAC decode hypotheses in `P25LiveDecoder.cpp`.
- The decoder still tries the nominal ACCH layout first.
- If RS/FEC appears possible but no CRC-valid MAC PDU is found, it now tries conservative alternate hypotheses:
  - swapped dibit bit order,
  - small ACCH dibit slips of ±1 and ±2,
  - inverted dibit polarity,
  - inverted polarity combined with swapped bit order and small slip.
- Audio release remains strict: only CRC-valid MAC/ESS can promote audio.
- Non-CRC/FEC-only attempts are returned only for diagnostics/counters and cannot open audio.

## Why

Recent captures showed tuning, TDMA superframe lock, and XOR mask lock working, but MAC/ESS remained stuck at `mac=0/0 ess=unknown`. At that point the likely failures are ACCH/MAC extraction layout, small epoch/slip mismatch, or dibit polarity/order. This build explores those alternatives without weakening the safety gate.

## What to watch in logs

Before this build, the stalled state was:

```text
sf=12 mask=12 mac=0/0 ess=unknown
```

After this build, the first sign of progress is any increase in MAC PDU/FEC/CRC counters:

```text
p2mac=0/N
```

or ideally:

```text
p2mac=1/N p2ess=clear
```

If `p2mac` stays `0/0`, the next suspect is the deeper ACCH interleave/RS layout rather than tuning, slot, mask, polarity, or small burst slip.
