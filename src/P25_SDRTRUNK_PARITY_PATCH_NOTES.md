# P25 / sdrtrunk parity patch notes

This patch addresses the highest-impact workflow/state gaps identified in the P25 Phase 2 review and the field logs that showed clean waterfall activity, full VCW/superframe/mask evidence, but delayed/random audio.

## Fixed in this patch

1. **Unknown-grant Phase 2 audio security gate**
   - Follows unknown Phase 2 traffic grants so the receiver can acquire current-call MAC/ESS.
   - Does not release unknown-grant PCM directly to the speaker.
   - Holds decoded PCM in a bounded pending queue and flushes it only after current-call clear state is established.
   - Drops the pending queue immediately when trusted encrypted state arrives or the call identity changes.

2. **MAC_IDLE / MAC_HANGTIME session reset**
   - Treats MAC types 2, 3, and 6 as session-ending or session-reset events.
   - Clears stale ESS/encryption/session state on END_PTT, IDLE, and HANGTIME.
   - Labels MAC type 6 as `MAC_HANGTIME` instead of reserved/unknown.

3. **Slot-probe stability after any accepted PCM**
   - Slot probe now suppresses a flip after any current accepted PCM, not only a full speaker-release block.
   - Speaker output still requires the stricter no-probe-blips threshold.
   - This prevents a valid slot from being flipped away simply because the same RF superframe also contains opposite-slot VCWs.

4. **Different-call dwell steal when current Phase 2 call is unacquired**
   - The scanner no longer waits the full 12-second different-call dwell when the current Phase 2 follow has no decoded audio, no MAC CRC, and no ESS.
   - Same-RF slot handoff behavior is retained.

5. **I-ISCH mask-phase guard tightened**
   - The mask phase anchor now rejects invalid I-ISCH channel values (`channel > 1`) before using I-ISCH evidence.

6. **Regression suite cleanup**
   - Updated stale validators that still expected old 80 ms cadence, hard AMBE `totalErrors <= 24`, and full-block-only slot-hold behavior.
   - Added `verify_p25_phase2_sdrtrunk_security_session_gate.py`.

## Still not equivalent to sdrtrunk

The application remains a single-receiver scanner-follow design. sdrtrunk’s parent control decoder plus traffic-channel manager can allocate independent traffic channel sources and track by frequency/timeslot. Reaching full parity will require a future `TrafficChannelSource` abstraction keyed by `(frequency, timeslot)`.

