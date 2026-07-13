# P25 Traffic Source Stale Receiver + Cursor Pre-roll Patch

This patch addresses a field log from 2026-06-25 where the new P25 Traffic Source mode was enabled but audio still did not open reliably.

Observed log pattern:

- A same-wideband traffic source was started for TG 30302 on 420.600 MHz while the control channel stayed at 420.350 MHz.
- Later, a one-RTL retune traffic source was started for 421.850 MHz.
- A delayed DSP log from the old 420.600 MHz traffic source still appeared after the 421.850 MHz source had replaced it.
- The first same-wideband traffic-source decode only contained about 40 ms of IQ (`iq=81920` at 2.048 Msps), which is too little context for reliable Phase 2 superframe/mask acquisition.

Changes:

1. Added a monotonic P25 traffic-source generation.
   - Every new P25 traffic receiver receives the current generation.
   - DSP/status paths ignore and deactivate stale traffic receivers whose generation no longer matches.

2. Mark old traffic receivers inactive before erasing them.
   - This prevents stale `shared_ptr` snapshots in the DSP worker from continuing to decode/log old traffic after a newer grant replaces the source.

3. Added `DeviceManager::setReceiverCursorBeforeLiveEdge()`.
   - Same-wideband/existing wideband logical traffic sources now start with about 420 ms of pre-roll from the IQ ring.
   - This mirrors sdrtrunk's continuous channelized source behavior more closely: when the traffic channel is already inside the sampled RF passband, the decoder can immediately see traffic context around the grant.
   - One-RTL physical retune sources still start at live edge so old control-channel IQ is not decoded as traffic.

Expected log improvement:

- No delayed DSP logs from an old traffic source after a new traffic source is started.
- Same-wideband traffic sources should start with a larger first `iq=` block instead of only ~40 ms.
- Better chance of early `p2sf/p2mask` lock and target-slot AMBE on in-passband traffic grants.
