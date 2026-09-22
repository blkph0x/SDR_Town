# SDR Town 0.2.84

## Satcom and TLE recovery release

This release repairs the Satcom and TLE regressions found after the broader non-P25 hardening work.

### Fixed

- Existing TLE cache files load immediately and remain active while a network refresh runs.
- A failed, slow, partial, HTML, proxy, or malformed TLE response can no longer clear valid cached or in-memory TLE data.
- The three CelesTrak feeds are fetched in parallel with bounded WinHTTP timeouts.
- Both true two-line TLE files and normal three-line name/TLE records are accepted.
- Cache writes use a temporary file and replacement step instead of overwriting the working cache first.
- The TLE button has a watchdog and always returns from the downloading state; cached data remains usable after a network timeout.
- Manual Satcom Start and Arm Pass automatically take over an ordinary active Listen receiver, without requiring the user to disable it first.
- The previous Listen stream, enabled state, and centre frequency are restored when Satcom stops or a pass ends.
- Satcom waits through DeviceManager's normal asynchronous `opening hardware (stub active)` transition instead of treating it as a permanent failure.
- The selected SDR is pinned for the full Satcom session.
- The Satcom receiver selector clearly shows LIVE, READY, and AVAILABLE receivers so a second SDR can be selected deliberately.
- Apply Location now gives visible confirmation and immediately recalculates passes.
- Satcom status identifies the active receiver and live hardware state.

### Safety

- No P25 or protected shared-pipeline file was changed.
- The frozen-P25 guard passed.
- MSVC application build, core tests, Qt/live-decoder tests, SSTV tests, release verification, clean staging, ZIP creation, checksum creation, and artifact upload passed on the exact repair commit before merge.
- Rollback branch: `backup/pre-satcom-tle-recovery-20260922`.

Real SDR hardware, local proxy behavior, and live satellite passes still require field confirmation, but the repaired lifecycle, cache, UI, tests, and release packaging have been validated in CI.
