# P25 Phase 2 field-log follow-up patch

This patch was made after reviewing `Pasted text(83).txt`, where the receiver:

- accepted TG 30302 slot 1 on 420.600 MHz,
- immediately replaced it in the same millisecond with TG 30003 slot 0 on the same RF carrier,
- then spent ~18 seconds seeing p2sf/p2mask/p2vcw evidence with decoded=0/audio=0 and no MAC/ESS progress.

## Changes

1. **Same-RF TDMA slot handoff grace**
   - A single scanner receiver cannot decode both Phase 2 traffic slots the way sdrtrunk can.
   - The code no longer steals from a just-tuned same-RF Phase 2 slot before the arm + settle + first acquisition grace has elapsed.
   - This prevents the exact log pattern where a slot-1 follow is replaced by a slot-0 follow before DSP has even armed.

2. **Unknown Phase 2 grant history handling**
   - Service-option-less Phase 2 grants no longer inherit stale clear/encrypted talkgroup history into the current traffic-channel state.
   - The traffic channel is followed, but clear/encrypted speaker state must come from target-slot PTT/ESS.
   - This fixes misleading `Grant ENC=unknown` -> `enc=clear` transitions.

3. **Stronger pending-audio call key**
   - Pending Phase 2 PCM is now keyed by NAC, WACN, system ID, talkgroup, source ID, slot, frequency, and grant epoch.
   - This prevents queued audio from one traffic allocation being flushed by a later allocation on the same TG/frequency.

4. **Deeper diagnostics**
   - The voice diagnostic snapshot now tracks target-slot VCWs, opposite-slot VCWs, AMBE decode attempts, and accepted AMBE frames.
   - This separates `we saw aggregate VCWs on the carrier` from `we actually attempted AMBE on the followed slot`.

## Why

sdrtrunk's `P25TrafficChannelManager` can allocate and track independent traffic-channel resources per frequency/timeslot. In this codebase we still have a scanner-follow receiver, so rapid same-RF grants need arbitration and an acquisition grace. Without that grace, the receiver can chase the second grant on the same carrier and never give the first grant's slot a chance to produce PTT/ESS/audio.
