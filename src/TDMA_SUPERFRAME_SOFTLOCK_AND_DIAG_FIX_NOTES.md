# TDMA Superframe Soft-Lock and Diagnostic Fix

This pass addresses the field captures where Phase 2 voice codewords were repeatedly detected
on the granted voice frequency, but the decoder stayed at `sf=0 mask=0 mac=0/0 ess=unknown`
and the GUI kept reporting `Phase 2 wrong TDMA slot`.

## What changed

- `P25LiveDecoder.cpp`
  - Phase 2 superframe candidate generation no longer assumes sync evidence must begin from
    only the preferred burst slots `{2,3,6,7,10,11}`.
  - It now generates candidate epochs from all twelve burst positions and scores preferred
    slots higher.
  - Added a small +/-2 dibit slip tolerance when scoring burst-spaced sync hits.
  - Added a demoted "soft lock" path when several burst-spaced syncs exist but do not satisfy
    the older preferred-slot assumption.
  - Audio is still gated by MAC/ESS, so this does not open noisy audio on soft locks.

- `main.cpp`
  - Pre-superframe/late-entry voice codewords are no longer classified as `wrong TDMA slot`.
  - If `grantSlotKnown` is false, the diagnostic is now treated as missing audio lock rather
    than wrong-slot evidence.
  - Wrong-slot slot flipping is reserved for cases where the decoder has an actual burst slot
    assignment to compare against the grant.

## Why

The latest captures show the receiver is:
- in passband on the granted voice frequency,
- seeing Phase 2 VCWs,
- but never deriving superframe/mask/MAC/ESS.

The previous wrong-slot diagnostic was too early. It was firing before a real superframe epoch
existed, causing repeated slot flipping without addressing the actual blocker: superframe
acquisition.

## Expected logs

After this build, repeated VCWs with no superframe should look more like:

```text
diag=Phase 2 audio lock missing ... p2vcw>0 sf=0 mask=0 mac=0/0 ess=unknown
```

If the new soft-lock path finds a superframe candidate, the desired progression is:

```text
sf>0 mask>0 mac=... ess=...
```

Audio remains muted unless the final clear-audio gate is satisfied.
