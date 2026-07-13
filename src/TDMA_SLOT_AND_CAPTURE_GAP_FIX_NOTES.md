# TDMA slot acquisition and capture-gap fix

This pass addresses two issues exposed by the 20260616_062323 capture.

## Findings from the capture

- The capture was centered correctly on the granted Phase 2 voice frequency: 418.05000 MHz.
- The decoder repeatedly reported `inPassband=yes`, `symbolRate=6000Hz`, and Phase 2 voice codewords (`p2vcw > 0`).
- Every acquisition check stayed at `diag=Phase 2 wrong TDMA slot`, `sf=0`, `mask=0`, and `ess=unknown`.
- The IQ capture showed ring gaps again because the previous fixed 0.40 second pull window was too short when the Qt timer was delayed by GUI/disk/log load.

## Code changes

1. Added dynamic IQ capture pull sizing.
   - The pull window now scales from the actual elapsed time since the previous poll.
   - Delayed GUI ticks request a larger safety window so the previous cursor remains inside the returned ring window when possible.
   - The timer interval is now 125 ms to drain the ring more often.

2. Reduced per-capture P25 log bloat.
   - The capture P25 log now writes only lines added during the capture window.
   - It no longer duplicates the full retained log at capture start and stop.

3. Added a one-shot TDMA slot auto-probe.
   - If the receiver is on the voice frequency, sees Phase 2 voice codewords, and repeatedly reports `Phase 2 wrong TDMA slot` with no superframe lock, it flips the selected TDMA slot once.
   - Audio remains gated until MAC/ESS proves clear.
   - The log line is: `TDMA ACQ slot auto-probe: ... trying alternate slot=...`.

## Expected next test result

After this patch, a clear Phase 2 voice grant that previously stayed stuck on `Phase 2 wrong TDMA slot` should either:

- advance to superframe/XOR/MAC/ESS lock after the alternate slot probe, or
- prove that the remaining issue is not grant-slot convention but burst/superframe detection itself.

The capture health should also return closer to `ok_gapless` unless the GUI thread or disk falls behind for more than the device ring can retain.
