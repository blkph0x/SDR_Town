# RF fail-closed build 23

This build removes silent stub fallback from explicit real-hardware starts.

Changes:
- `startStreaming(index, true)` no longer starts the internal stub first.
- The waterfall receives no IQ until Soapy open/setup/activate succeeds.
- Hardware open failures set `runtimeState = hardware open failed (no stub)` and log an error.
- Real `readStream()` faults set `runtimeState = hardware read failed (no stub)` and stop the stream instead of falling through into flat mock noise.
- Stub mode is still available only through `startStreaming(index, false)` for demo/testing.

Why:
The flat white-noise waterfall after removing the fake stub carrier proved the app was still showing mock/no-hardware IQ somewhere in the GUI path. This build makes that impossible for real user starts: either real SDR IQ appears, or the stream fails visibly.
