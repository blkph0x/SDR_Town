# Online reference validation and robustness pass

This pass checked the Phase 2 voice path against public implementations/docs rather than guessing missing spec behaviour.

## References used

- OP25/boatbod `p25p2_tdma.cc`: Phase 2 TDMA burst handling extracts voice frames from 36 dibits at payload offsets 11, 48, 96 and 133, and tracks 4V/2V burst ordering plus ESS/MAC state before audio release.
- OP25/boatbod `p25p2_vf.cc`: Phase 2 voice frame handling deinterleaves one 72-bit voice codeword into AMBE C0/C1/C2/C3 rows, corrects C0/C1 with Golay, applies the AMBE pseudo-random C1 demodulator, then unpacks AMBE parameters.
- mbelib `ambe3600x2450.c`: `mbe_processAmbe3600x2450Framef()` performs C0 Golay correction, AMBE PRNG demodulation, C1 Golay correction, and extracts the 49 AMBE data bits internally before synthesizing 8 kHz PCM.
- mbelib README: mbelib supports P25 Phase 1 IMBE plus half-rate AMBE-family vocoders, with patent/licensing caution noted upstream.

## Source changes made in this robustness pass

- Fixed a concrete compile blocker in `P25LiveDecoder.cpp`: duplicate declaration of `const size_t pos` inside `processHardBits()`.
- Switched Phase 1 hard-bit sync scanning to the reusable `P25Phase1StreamingSync` wrapper while preserving separate best-sync tracking.
- Added short tail carry-over for C4FM and CQPSK symbol recovery so Gardner early/late interpolation has real adjacent samples at block boundaries instead of relying only on edge clamping.
- Updated AMBE comments to reflect the verified mbelib behaviour: the project must provide correctly deinterleaved C0/C1/C2/C3 hard bits, while mbelib performs the inner AMBE Golay correction and PRNG demodulation.
- Added a post-mbelib AMBE error gate for Phase 2 3600x2450 frames. Frames with excessive corrected/remaining errors are rejected instead of being pushed into the audio queue as artifact-heavy PCM.

## Remaining integration notes

- This package is still source-only `.cpp`; final verification must be done in your current full repo with the matching updated headers/build system.
- The current implementation intentionally does not copy GPL OP25 code into this project. It mirrors the publicly documented data flow and interleave layout already present in the project, while relying on mbelib for the inner AMBE ECC it already implements.
- Phase 2 encrypted traffic remains gated. No attempt is made to decode encrypted voice without keys.
