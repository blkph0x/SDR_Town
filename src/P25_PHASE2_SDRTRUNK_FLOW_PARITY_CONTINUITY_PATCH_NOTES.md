# P25 Phase 2 sdrtrunk flow parity continuity patch

This patch fixes a remaining live-flow mismatch found by comparing the current C++
Phase 2 path against sdrtrunk's continuous traffic-channel pipeline.

sdrtrunk keeps the Phase 2 traffic channel as a continuous stream of
SuperFrameFragment -> Timeslot -> P25P2MessageProcessor -> P25P2AudioModule
objects. It does not jump over undecoded traffic bursts when the UI/DSP loop falls
behind.

The C++ rolling IQ path kept a rolling buffer, but takeUndecoded() could still do
this when the pending region exceeded maxSamples:

    first = samples.size() - maxSamples;
    lastDecodeAbsolute = endAbsolute;

That jumps to the newest chunk and marks the entire buffer consumed. On busy Phase
2 traffic this can skip the exact 2V/4V bursts that should carry AMBE continuity,
which matches the field log: one clean decoded clear voice block followed by empty
or isolated windows.

New behavior:

    first = max(firstNew - overlap, 0);
    count = min(samples.size() - first, maxSamples);
    lastDecodeAbsolute = startAbsolute + first + count;

So the decoder consumes the oldest not-yet-decoded fresh samples with controlled
pre-roll and only advances to the end of the returned chunk. This more closely
matches sdrtrunk's continuous-source semantics while preserving bounded per-window
CPU.

Expected field effect:

    p2bursts=1 p2vcw=4 targetVcw=0 ambe=0/0

should occur less often after a good superframe anchor, and isolated/following
bursts should be decoded in stream order instead of being skipped when the worker
falls behind.
