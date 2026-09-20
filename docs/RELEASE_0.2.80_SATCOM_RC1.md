# SDR Town v0.2.80-satcom-rc1

This prerelease is a test build for the selected-satellite map and automatic pass-capture work. It is intentionally published separately from the existing v0.2.80 release and does not move `master`.

## Included

- Selected catalogue satellites are propagated from their TLEs and drawn at their current sub-satellite positions on the observer map.
- Map markers identify valid/invalid TLE state, whether the satellite is above the configured minimum elevation, and the currently armed satellite.
- The TLE/pass UI refresh deadlock is fixed: cached TLE age and pass rows update even before a pass is armed.
- Optional automatic capture chooses the best supported selected satellite currently in range, arms Doppler tracking without forcing another tuner owner away, and saves detected audio.
- Existing decode paths are used where available: APRS/AX.25, NOAA APT, and ISS SSTV. ISS passes are monitored and the SSTV decoder saves images/reports when an SSTV transmission is actually present; non-SSTV ISS audio is still retained as a capture.
- Per-pass recordings use unique UTC/satellite/downlink filenames and have JSON sidecar metadata.
- TLE refresh failures preserve the last good in-memory/cache data rather than blanking the planner.

## Automatic mode safety

Automatic capture is enabled from the Satcom panel and only owns sessions it armed itself. It does not use a forced tuner lease, respects manually armed passes, and finalises its recording/decoder output at LOS or when disabled.

## Validation

Windows CI builds the application, core tests, and Qt/live-decoder tests; runs the release verifier; creates a clean deployment staging directory; runs `windeployqt`; then publishes the portable ZIP and SHA-256 checksum. Obsolete P25 source-string verifier gates were removed from this workflow; semantic compiled tests remain blocking.

## Fallback

The unchanged pre-work tree is retained at:

`backup/pre-satcom-auto-20260920`

The implementation branch is:

`feature/satcom-auto-map-decode`
