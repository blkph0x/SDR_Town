# Phase 2 Streaming State Machine Fix Pass

This package keeps the cleaned source filenames (no upload suffixes like `(18)`).

## Implemented/tightened

- `AudioEngine.cpp`
  - Fixed the `spdlog` call that passed an argument without a `{}` placeholder.
  - Changed `stopDevice()` to remove the stale active-output entry from `m_active` while locked, then stop/uninit the device outside the lock.

- `main.cpp`
  - Replaced frame-local AMBE PCM resampling with a stateful per-`Receiver` interpolator.
  - The resampler preserves fractional phase and the previous sample across 20 ms mbelib frames so 8 kHz mono output converts cleanly to 44.1/48 kHz device-rate output without per-frame timing resets.

- `P25LiveDecoder.cpp`
  - Added persistent timing-loop storage keyed by `P25LiveDecoder*` and cleared it on `reset()`.
  - Wired C4FM Gardner timing recovery and CQPSK timing recovery to reuse loop `omega` and fractional phase across processing blocks where compatible sample rates are seen.
  - Made Phase 2 sync tolerance config-driven with a production floor of 3 bit errors and a clamp of 6.
  - Kept the 64-bit/40-bit non-allocating sync correlator path in the block scanners.
  - Preserved mbelib’s required unpacked 4x24 AMBE matrix conversion and validation.

## Remaining integration dependency

Phase 2 voice-codeword FEC/interleave is constrained by the structures and tables available in this `.cpp`-only package. The current path now prevents obvious non-unpacked data from entering mbelib and gates audio on TDMA/mask/ESS/audio-lock conditions, but a standards-complete Phase 2 AMBE voice-codeword FEC implementation still needs the project’s authoritative Phase 2 voice interleave/FEC tables or corresponding header/API support if those are not already present elsewhere in the repo.
