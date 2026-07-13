# P25 Phase 2 Unknown-Audio Queue + Watchdog Patch

Field log `Pasted text(88)` showed that the traffic source reached the AMBE layer on a one-RTL retune grant:

- `p2bursts=11 p2vcw=14 targetVcw=10 ambe=8/2`
- but the final block was still `decoded=0 audio=0`

The reason was security-gate accounting: accepted AMBE from unknown-security calls was queued and then the current block's `decodedFrames` was reset to zero unless the same block had at least four accepted frames and current MAC/ESS proof. This made small late-entry AMBE chunks look like total silence, and the follow watchdog sat on the traffic source until the long no-progress timeout.

Changes:

- Unknown-security Phase 2 PCM is queued first under the existing call key.
- The field-audio probe can now release accumulated queued PCM once at least four safe 20 ms AMBE frames have been collected from the followed masked/superframe slot.
- `decodedFrames` is preserved as a diagnostic/acquisition signal while audio is held, so accepted AMBE no longer looks like silence.
- `P25VoiceDiagSnapshot` now carries `updatedMs`.
- The follow watchdog only refreshes activity from fresh diagnostics, so a stale traffic snapshot cannot keep a one-RTL traffic source parked forever.
- Partial sf/mask no-MAC/no-ESS timeout is shortened/loosened for no-decoded-audio cases.
