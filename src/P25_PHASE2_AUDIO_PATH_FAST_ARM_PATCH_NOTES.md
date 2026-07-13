# P25 Phase 2 Audio Path Fast-Arm Patch

This patch is focused on getting live Phase 2 clear audio out of the speaker while keeping the UI stable.

Changes:

- Phase 2 voice arm delay is now 0 ms. The decoder is committed immediately after a grant; `p25VoiceSettleUntilMs` is now only a speaker mute, not a delay before feeding IQ into the TDMA decoder.
- Control-channel decode cadence is reduced from 360 ms to 120 ms so scanner-follow sees grants earlier.
- Same-RF Phase 2 slot/TG changes no longer force RF retune, live decoder replacement, rolling-IQ clear, or a new arm/settle cycle. The selected talkgroup/slot metadata is switched in-place, preserving the traffic decoder stream.
- Unknown-grant field audio probe is enabled for bring-up: if target-slot, XOR-masked, superframe-locked AMBE produces at least a full 4-frame safe PCM block and no encrypted ESS/grant exists, PCM may play even while MAC/ESS is still missing. This does not mark the call clear and explicit encryption still fails closed.
- DSP voice-loop logs now include target-slot VCW, opposite-slot VCW, AMBE attempts, and AMBE accepted counts.

This keeps the sdrtrunk-like preferred path (queue until target-slot PTT/ESS) but adds a practical field fallback so the AMBE/audio path can be proven while ACCH/MAC/ESS work continues.
