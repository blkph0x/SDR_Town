# Phase 2 Audit Implementation Summary

## Files inspected
- AudioEngine(19).cpp
- ClassifierModelBackend(18).cpp
- Demod(18).cpp
- DeviceManager(18).cpp
- P25Control(18).cpp
- P25LiveDecoder(18).cpp
- Receiver(17).cpp
- SignalClassifier(18).cpp
- SpectrumWidget(18).cpp
- UpdateManager(18).cpp
- main(19).cpp
- review_notes.txt

## Changes made

### P25LiveDecoder(18).cpp
- Changed the local `percentile` helper from pass-by-value to pass-by-reference:
  - `double percentile(std::vector<double>& values, double p)`
  - This removes the hidden heap copy on every percentile call and matches the Demod/SignalClassifier scratch-vector pattern.
- Added a reusable `SecondOrderTimingLoop` wrapper and routed both C4FM Gardner-style recovery and CQPSK/HDQPSK complex-symbol recovery through it.
- Added non-allocating streaming correlator helpers:
  - `StreamingBitCorrelator48` for P25 Phase 1 48-bit frame sync.
  - `StreamingDibitCorrelator40` for Phase 2 burst/ISCH sync candidates.
- Reworked Phase 1 frame sync scanning to use a rolling 48-bit shift register and popcount BER check rather than rechecking every 48-bit window bit-by-bit.
- Reworked Phase 2 sync-hit discovery to use rolling dibit correlation against the configured Phase 2 sync patterns, accepting up to 3 bit errors.

### AudioEngine(19).cpp validation
- Confirmed `AudioEngine::startDevice` maps `cfg.pUserData` directly to `ActiveOutput*` rather than the owning `AudioEngine*`.
- Confirmed the callback interprets `pUserData` as `AudioEngine::ActiveOutput*`, matching the fixed layout.
- Confirmed teardown is already staged: active outputs are invalidated under lock, swapped out, then stopped/uninitialized outside the mutex while shared ownership keeps callback storage alive.
- Confirmed ring wrap has a power-of-two static assertion and runtime power-of-two guard.

### main(19).cpp validation
- Confirmed decoded mbelib 8 kHz PCM is resampled through `resampleDecodedP25Pcm(...)` before audio is pushed to the selected native output rate.
- Confirmed Phase 1 and Phase 2 voice decode paths both resample decoded PCM before insertion into playback buffers.

## Notes / constraints
- Only `.cpp` and notes files were uploaded, so changes that require class/member layout updates in headers, such as `alignas(64)` on `AudioEngine::Ring::readPos/writePos`, cannot be directly applied in this package without the matching header file.
- The mbelib Phase 2 unpacker already outputs a `char ambeFrame[4][24]` where each byte is normalized to 0 or 1. The upstream Phase 2 codeword-to-AMBE matrix was left intact and the package keeps that mapping in place.
- I could not run a full project compile because the corresponding headers/CMake project were not included in the upload. I did run structural sanity checks on brace balance for the modified files.

## Sprint implementation update - active streaming state machine

Clean file names were restored before packaging; upload suffixes such as `(17)`, `(18)`, and `(19)` were removed.

Implemented/tightened in this package:

1. **Symbol synchronization state machine**
   - Expanded the existing second-order timing loop into an explicit Type-II symbol timing PLL with `omega`, `mu`, bounded loop bandwidth, and damping controls.
   - Wired the timing loop into the existing C4FM Gardner-style TED path and CQPSK complex symbol recovery path.
   - Added per-recovery timing telemetry fields so recovered streams are explicitly treated as symbol-rate outputs.

2. **Sliding bit cross-correlators**
   - Added non-allocating `P25Phase1StreamingSync` and `P25Phase2StreamingSync` wrappers around the 48-bit and 40-bit shift-register correlators.
   - Phase 1 sync continues to use the configured BER threshold via `maxFrameSyncBitErrors`.
   - Phase 2 sync detection was relaxed to accept up to 3 bit errors for weak-signal TDMA slot recovery.

3. **mbelib unpacker matrix guardrails**
   - Added validation that AMBE 3600x2450 input is already unpacked as 0/1 bits before calling mbelib.
   - Documented the strict upstream requirement: raw TDMA dibits/codewords must pass frame sync, mask removal, trellis/FEC recovery, ESS/RS handling, and voice-codeword validation before matrix unpacking.
   - Reworked the final 4x24 copy loop into explicit row/column unpacking to make the mbelib contract obvious.

4. **8 kHz to device-rate audio upsampling**
   - Added a dedicated `upsampleMbelib8kMonoToDeviceRate()` function in `main.cpp`.
   - `resampleDecodedP25Pcm()` now routes mbelib’s fixed 8 kHz mono output through that explicit upsampler before `AudioEngine::pushAudioToActiveOutputs()` receives samples.

Notes:
- The provided upload set contains `.cpp` files only. Header-owned structure changes, such as adding persistent symbol-loop members to `P25LiveDecoder.h` or `alignas(64)` to `AudioEngine::RingBuffer` fields, still need to be applied in the matching headers in the repository.
- The existing code already had a significant Phase 2 parser/FEC foundation: RS helpers for ESS/MAC handling, trellis decode paths, CQPSK lock state, duplicate voice-codeword suppression, and audio resampling call sites.
