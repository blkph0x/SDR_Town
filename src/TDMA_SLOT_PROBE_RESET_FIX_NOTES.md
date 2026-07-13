# TDMA Slot Probe Reset Fix

This pass addresses the latest 421.850 MHz capture, which showed:

- capture health was `ok_gapless`
- streaming `_p25_log.txt` worked on the fly
- receiver was correctly tuned to the voice frequency
- Phase 2 VCWs were present
- diagnostics repeatedly stayed at `Phase 2 wrong TDMA slot`
- sf/mask could appear in retained context, but MAC/ESS never advanced

## Root cause fixed

The TDMA slot auto-probe state was keyed only by TG/frequency. If a previous grant for the same TG/frequency had already flipped once, later re-grants for the same TG/frequency could be blocked from probing again because the static `slotProbeFlipped` flag remained true.

## Changes

- Slot-probe state now also tracks `p25AutoFollowTunedAtMs`, so every new auto-follow arm/re-grant gets a fresh probe budget.
- Wrong-slot accumulation no longer requires `sf == 0`; repeated wrong-slot with Phase 2 VCWs is enough to probe. This matches the latest capture where sf/mask can appear but all voice codewords are still rejected as the wrong grant slot.
- Replaced one-shot probing with a rate-limited probe budget:
  - up to 4 slot flips per long call
  - at least 10 seconds between flips
- If the grant slot is unknown, the probe now enables slot testing instead of doing nothing.
- Probe logs now include previous slot, new slot, sf, mask, MAC, and ESS state.

## What to watch in the next capture

Look for:

```text
TDMA ACQ slot auto-probe: repeated wrong-slot diagnostics with VCWs present; switching slot X -> Y ...
```

Then check whether the following `TDMA ACQ check` lines move from:

```text
diag=Phase 2 wrong TDMA slot ... mac=0/0 ess=unknown
```

to one of:

```text
diag=Phase 2 late entry ... mask>0 ... mac>0
```

or:

```text
diag=decoding clear voice ... ess=clear
```

If both slot directions still show `wrong TDMA slot`, the next likely fault is the burst-slot convention inside `P25LiveDecoder` itself rather than the control-channel grant slot value.
