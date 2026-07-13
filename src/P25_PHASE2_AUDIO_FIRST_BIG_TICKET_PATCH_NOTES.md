# P25 Phase 2 audio-first big-ticket patch

This patch targets the remaining live blocker seen in the strong-signal field logs:
Phase 2 superframe/mask/VCW telemetry appears, but MAC/ESS and AMBE audio never become usable.

Changes:

1. **Do not apply frozen control-channel AFC to Phase 2 voice traffic**
   - Phase 2 scanner-follow now centers the traffic decoder on the granted RF frequency.
   - The previous path could log `cf=voice` while `target=voice+4.276kHz`, which can leave sync/mask telemetry visible while ACCH/MAC and AMBE are degraded.
   - This better matches sdrtrunk's independent traffic channel source semantics.

2. **Phase 2 target-offset recovery probe**
   - If the first pass sees Phase 2 telemetry but no MAC/ESS, the voice decoder tests a small set of nearby target offsets from the same decoder baseline.
   - It keeps the candidate with the strongest MAC/ESS/FEC/voice evidence and commits that decoder state.
   - This is meant to get audio quickly when the traffic channel is slightly miscentered.

3. **Clear-grant audio recovery fallback**
   - If the control-channel grant explicitly says clear, the followed slot is superframe locked, XOR mask is applied, and AMBE produces usable PCM after a short grace period, audio may release even if MAC/ESS is still missing.
   - This fallback does not apply to unknown or encrypted grants.
   - Preferred release remains target-slot PTT/ESS; this is a field-recovery path to get clear audio while lower ACCH/MAC parity continues to be diagnosed.

These are deliberately practical audio-first changes. The full sdrtrunk-style architecture is still a real traffic-channel manager with independent traffic sources, but this patch makes the existing scanner-follow architecture much less brittle.
