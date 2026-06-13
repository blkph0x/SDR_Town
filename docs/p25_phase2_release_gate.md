# P25 Phase 2 Release Gate

Updated: 2026-06-13

## Current Status

The current P25 Phase 2 path can:

- Detect and follow Phase 2 TDMA voice grants from the control channel.
- Track talkgroup, slot, NAC, WACN, SYSID, and clear/encrypted grant state when those fields are observed.
- Detect candidate TDMA 2V/4V bursts on the followed voice channel.
- Lock and annotate a 12-burst TDMA superframe when enough periodic Phase 2 sync evidence is present.
- Decode S-ISCH and I-ISCH codewords into sync/channel/location/free-access/ultraframe-counter diagnostics when the 40-bit codeword is visible.
- Generate and apply the Phase 2 XOR mask from NAC, WACN, and SYSID once those fields are known from the control channel.
- Decode candidate SACCH/FACCH/LCCH MAC PDUs through the Phase 2 RS/CRC path and expose `p2mac=valid/total` diagnostics.
- Track ESS clear/encrypted state from MAC PTT/ACTIVE state and voice-burst ESS reconstruction.
- Feed Phase 2 voice-follow diagnostics with a 0.75 second recent-IQ window instead of 25 ms fragments, so a full superframe can be observed.
- Keep control-channel audio muted while monitoring and while returning from auto-follow.

The current P25 Phase 2 path must not be released as complete clear voice yet. Clear AMBE audio is hard-gated unless a burst has passed TDMA superframe timing, real mask application (`xorMaskApplied=true`), MAC/ESS clear-state validation, and AMBE plausibility checks. The code path is implemented, but it still needs real clear Phase 2 IQ validation before we publish a GitHub release asset claiming audible Phase 2 voice.

## Required Before Publishing A Phase 2 Voice Asset

1. Superframe lock
   - Locate the 2160-dibit TDMA superframe start. Basic periodic sync scoring is implemented.
   - Decode ISCH state for the twelve 180-dibit bursts. S-ISCH/I-ISCH field decode is implemented and still needs on-air validation.
   - Track slot confidence and reject bursts from the wrong slot.

2. TDMA mask
   - Generate the Phase 2 XOR mask from NAC, SYSID, and WACN. Implemented with a local known-vector test.
   - Apply the mask at the correct superframe and burst offset. Implemented for metadata-known voice follow.
   - Mark `xorMaskApplied=true` only after the mask is actually applied. Implemented.

3. MAC/ACCH state
   - Decode SACCH/FACCH/LCCH with FEC and CRC validation. Implemented conservatively; needs real capture proof for all three ACCH forms.
   - Decode MAC PTT/ACTIVE/END/HANGTIME state. PTT/ACTIVE/END/HANGTIME scaffolding is present; field mapping must be checked against live PDUs.
   - Establish encryption state from MAC/ESS before voice samples are released. Implemented as a hard audio gate.

4. Voice extraction
   - Extract 2V/4V voice codewords after slot, mask, and burst-order validation.
   - Validate AMBE FEC/error rate before audio output.
   - Maintain persistent AMBE decoder state per followed slot and talkgroup.

5. Real capture validation
   - Test with real clear Phase 2 IQ captures from at least one known-good local system.
   - Verify no audio is emitted on control channels, encrypted calls, wrong slots, or failed mask/MAC/ESS validation.
   - Verify clear calls produce stable 8 kHz PCM that resamples cleanly into the audio engine.

Until all five areas pass, GitHub releases should describe Phase 2 as control/grant/burst diagnostics, not complete clear voice.
