# P25 Phase 2 sdrtrunk parity final pass

This patch fixes structural differences found while comparing the current code with sdrtrunk's P25 Phase 2 flow:

1. **Call-key lifetime now matches traffic-channel tracker semantics.**
   Pending Phase 2 audio is keyed by NAC/WACN/SYS + traffic RF + TDMA slot + talkgroup.  It no longer includes grant epoch or source ID, so repeated grant updates for the same call extend the same queued audio instead of resetting it.

2. **Explicit clear grants are honored at the final Phase 2 audio gate.**
   The decoder already allowed explicit clear grants to feed AMBE; the final security gate now also treats `p25VoiceClearKnown && !p25VoiceEncrypted` as a trusted clear state unless target-slot ESS later proves encryption.

3. **Unknown-security field fallback releases accumulated target-slot AMBE sooner.**
   PCM only enters the queue after target-slot, descrambled AMBE decodes to finite audio.  The field fallback can now release a short safe queued run after two frames, while explicit encrypted grants/ESS still fail closed.

4. **One-RTL traffic-channel watchdog no longer refreshes on stale partial TDMA telemetry.**
   Partial evidence such as repeated `p2sf=5 p2mask=5 p2vcw=6` without MAC/ESS or decoded audio no longer extends `lastActive` forever.  The follow state machine returns to control quickly on no-progress Phase 2 traffic, matching scanner/one-tuner trunking behaviour.

Sources checked against sdrtrunk:
- `P25TrafficChannelManager`: traffic calls are tracked by frequency/timeslot and updated by traffic/current-user messages rather than recreated on every grant.
- `P25P2AudioModule`: Phase 2 voice timeslots are queued until PTT or ESS establishes clear/encrypted state, then queued voice is processed or discarded.
- `P25P2MessageProcessor`: per-timeslot ESS processors reset on PTT/END/IDLE/HANGTIME and traffic messages are processed continuously from timeslot objects.
- `TunerManager`: traffic channel decoding is driven by a tuner/channel-source abstraction.
