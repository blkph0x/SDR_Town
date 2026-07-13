# Production hardening stage 3 / buildfix14

This pass continues the production-audit cleanup using only the cpp package.

## Changes

### P25 control safety
- `P25ControlChannelAnalyzer::ingestTsbk()` no longer silently zero-pads short 10-byte/manual TSBK blocks into 12-byte messages.
- Live decoder paths already pass FEC/CRC validated 12-byte TSBKs. Manual/CLI/API paths now fail closed instead of synthesizing false grants from partial input.

### Updater hardening
- Added a process-wide single-flight guard so repeated clicks cannot start parallel installer downloads.
- Added downloaded payload size validation before writing to disk.
- Added incomplete-write detection and cleanup.
- The in-flight guard is cleared on every error and after SHA-256 verification/launch handling.

### Spectrum/UI thread safety
- Added mutex protection around key `SpectrumWidget` mutable state setters: center frequency, sample rate, frequency range, color range, view bandwidth, squelch/live levels.
- Added mutex protection around wheel zoom and waterfall resize state mutation.
- This reduces data races between UI events, paint/update paths, and incoming spectrum updates.

### Capture finalization
- `stopLiveIqCapture()` now closes the events JSONL stream and flushes/closes the ring-health CSV, instead of only flushing events.
- This makes long start/stop captures more robust on shutdown and process exit.

## Notes
- This stage does not move P25 processing fully off the GUI timer yet. That needs a larger worker-queue refactor and header-level ownership cleanup.
- This stage does not relax the Phase 2 MAC/ESS gate. Clear audio still requires valid MAC/ESS release evidence.
