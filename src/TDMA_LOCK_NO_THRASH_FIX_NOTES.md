# TDMA lock no-thrash fix

This pass addresses a real capture where Phase 2 acquisition reached `sf=12` and
`mask=12`, but the diagnostic still said `Phase 2 wrong TDMA slot` and the
slot auto-probe flipped the receiver away from an already-valid TDMA epoch.

Changes:

- Slot auto-probe now only runs before TDMA epoch/mask lock.
- If `p2sf >= 6` and `p2mask >= 6`, the decoder treats the TDMA epoch/mask as
  useful lock evidence and stops slot-flipping.
- Added a throttled `TDMA ACQ note` explaining that superframe/mask lock is
  present but MAC/ESS has not been recovered yet.
- Audio remains gated until MAC/ESS proves clear voice.

Reason:

On dual-slot Phase 2 channels, a receiver may see voice bursts from the other
slot while following one TG/slot. Flipping slots after superframe/mask lock can
thrash between slots and prevent stable late-entry/MAC/ESS acquisition.
