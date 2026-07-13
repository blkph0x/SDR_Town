# TDMA Slot Probe Apply-After-Reset Fix

This pass fixes the Phase 2 slot auto-probe not taking effect after repeated `Phase 2 wrong TDMA slot` diagnostics.

## What was wrong

`resetP25VoiceState()` can clear active voice metadata such as Phase 2 state, TDMA slot fields, mask/NAC/system metadata, and the live decoder instance. Some paths applied grant/slot metadata before calling the reset, which could leave the receiver repeatedly attempting acquisition with stale/default slot state.

The slot auto-probe also changed the slot and then called `resetP25VoiceState()`, which could erase the newly selected slot before the next decode window.

## What changed

- GUI auto-follow now calls `resetP25VoiceState()` before applying grant metadata.
- CLI follow path now does the same.
- Slot auto-probe now:
  - captures grant metadata,
  - resets decoder state,
  - restores Phase 2/slot/mask metadata,
  - rebuilds the Phase 2 live decoder,
  - reapplies mask parameters when known.
- Slot probe threshold is quicker for diagnostics: 2 wrong-slot checks and 5 s minimum between flips.
- Audio remains gated until MAC/ESS and AMBE validation prove safe output.

## Expected log

Look for:

```text
TDMA ACQ slot auto-probe: repeated wrong-slot diagnostics with VCWs present; switching slot 0 -> 1 ...
```

Then check whether later `TDMA ACQ check` lines advance from `sf=0 mask=0 mac=0/0 ess=unknown` toward mask/MAC/ESS lock.
