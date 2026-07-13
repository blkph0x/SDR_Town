# Production Hardening Pass

This package is a source-only production-hardening pass for the current clean `.cpp` package. It assumes the matching headers and build environment in the main repository already define the public decoder/audio structures referenced by these implementation files.

## Implemented in this pass

### P25 streaming state safety
- Replaced the previous unsafe global timing pointer pattern with copy-in/copy-out decoder stream state guarded by a mutex.
- Decoder timing state is now loaded into a local working object, mutated during block processing, then committed back after the block.
- Reset now clears all associated stream state for the decoder instance.

### C4FM / CQPSK timing continuity
- Preserved C4FM and CQPSK timing state across blocks without holding a global mutex during DSP work.
- Reloaded timing state after the C4FM path inside `processIq()` so CQPSK processing does not overwrite fresh C4FM timing state with stale data.

### Phase 1 frame-sync continuity
- Added a bounded Phase 1 bit tail to detect 48-bit frame sync sequences that cross block boundaries.
- Suppressed sync windows that are fully contained in retained tail data to avoid reprocessing old frames.
- Tail is updated on both short and normal blocks.

### Phase 2 TDMA sync continuity
- Added a bounded Phase 2 dibit scan prefix from the existing decoder dibit tail so TDMA burst/superframe sync can be detected across block boundaries.
- Suppressed Phase 2 hits whose full burst is entirely in prefix/tail history, while allowing bursts that start in tail and finish in the current block.

### Audio device-rate accuracy
- `AudioEngine::startDevice()` now stores the actual initialized miniaudio device sample rate rather than assuming the requested config rate was accepted.
- This improves downstream 8 kHz-to-device-rate resampling accuracy and reduces long-run queue drift when the hardware chooses a different output rate.

## Remaining production validation required

These items cannot be proven from the source-only package without your full repository, headers, compiler flags, linked mbelib/miniaudio versions, and RF/audio captures:

1. Full compile/link verification in your current build environment.
2. Long-run runtime soak testing on 44.1 kHz and 48 kHz output devices.
3. Capture-based decode verification for:
   - P25 Phase 1 clear voice
   - P25 Phase 1 encrypted voice rejection
   - P25 Phase 2 slot 0 clear voice
   - P25 Phase 2 slot 1 clear voice
   - weak Phase 1/Phase 2 sync with 1-3 bit errors
   - high AMBE error rejection
4. Validation that the Phase 2 AMBE C0/C1/C2/C3 mapping matches known-good captures from your target systems.

## Recommended next repository-level step

Move the currently file-local streaming state into explicit private members of `P25LiveDecoder` once headers are being edited. The current implementation is safe enough for `.cpp`-only integration, but decoder-owned private members are cleaner for production lifetime management and easier to unit test.
