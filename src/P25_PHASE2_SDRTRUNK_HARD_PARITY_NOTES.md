# P25 Phase 2 hard sdrtrunk parity pass

This pass tightens the remaining scanner-follow shortcuts that diverged from
sdrtrunk's traffic-channel/audio model.

## Changes

1. **Persistent per-timeslot Phase 2 session state**
   `P25LiveDecoder` now retains independent ESS/MAC/PTT state for TDMA slot 0
   and slot 1 across decode calls. The legacy single session remains only as a
   summary view.

2. **Unknown grants are decode-only, not release proof**
   Unknown Phase 2 grants can still be followed and decoded, but AMBE
   plausibility no longer clears ESS/metadata/late-entry flags. Speaker release
   still requires trusted clear grant or current-call session audio release.

3. **No raw scrambled AMBE in release/queue path**
   If the XOR mask is not applied, voice codewords are treated as acquisition
   evidence only and are not fed into the audio release path.

4. **ESS security state is explicit**
   Clear ESS sets clear/unencrypted; trusted encrypted ESS sets encrypted and
   does not mark clear-known.

These are still within a single-receiver scanner-follow architecture, but they
move the Phase 2 state ownership and audio-release rules closer to sdrtrunk:
traffic first, per-timeslot state, queue/hold unknown, release only clear.
