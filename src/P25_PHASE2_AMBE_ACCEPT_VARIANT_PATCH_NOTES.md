# P25 Phase 2 AMBE accept/variant patch

Field logs after the fast-arm patch showed the speed/frequency path was fixed and the decoder reached a real Phase 2 target-slot voice window:

```text
p2sf=12 p2mask=12 p2mac=1/12 targetVcw=4 oppVcw=4 ambe=4/0
```

That means the remaining audio blocker was no longer follow timing or MAC extraction; AMBE frames were being attempted but rejected before PCM could be queued/released.

Changes:

- Added a small live-safe AMBE 72-bit bit-order variant selector.
- Variants are scored using a throwaway `P25AmbeVoiceDecoder` so candidate trials do not corrupt the stateful live mbelib decoder.
- Only the winning variant is fed to the real `rx.p25AmbeVoiceDecoder`.
- Loosened `p25AmbeDecodeFrameLooksUsable()` to accept finite decoded mbelib frames, including low-energy repeat/silence frames.
- Loosened `p25AudioSamplesLookSafe()` to remove the minimum-RMS gate while keeping runaway/non-finite protection.

This keeps the preferred sdrtrunk-style queue-until-target-slot-PTT/ESS behavior, but allows the existing field probe to finally pass proven target-slot, masked AMBE PCM so the lower voice path can be verified.
