# Production Hardening Stage 4 / Build 15

## Goal
Move the heaviest P25 control-channel decode work out of the fast Qt GUI update timer so spectrum/UI updates stay responsive while P25 control decode continues in the background.

## Changes

- Added a dedicated P25 control decode worker path in `main.cpp`.
- The GUI timer now only:
  - checks whether a completed P25 result is available,
  - schedules a worker decode when no result is pending,
  - applies decoded control-channel events/logging on the GUI thread.
- Heavy `P25LiveDecoder::processIq()` for control-channel monitoring now runs on a managed background `std::thread` instead of the GUI timer.
- Added a separate `p25ControlWorkerDecoder` instance so the worker owns its decoder state.
- Added a bounded single-result handoff:
  - at most one pending result is kept,
  - stale pending results are dropped and counted if the GUI cannot keep up,
  - the latest result is kept.
- Worker thread is joined during shutdown; it is not detached.
- Control decoder worker state is reset when P25 monitoring/follow state is reset.

## Why this helps

Previously, every P25 control decode window could run inside the Qt timer callback. Even with rate limiting, `processIq()` can be expensive and can stall painting, input handling, device UI, and capture controls. This stage moves the expensive DSP decode step off the UI thread while preserving existing UI-side registry/log/event handling.

## Remaining future work

- Move the post-decode P25 event ingestion/logging into a queue-based controller as well.
- Replace the single-result worker with a long-lived worker thread and condition variable if deeper queueing is needed.
- Add regression tests for GUI responsiveness under active P25 control decode and IQ capture.
