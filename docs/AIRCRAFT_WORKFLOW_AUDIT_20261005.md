# Aircraft workflow audit, DEC-0197

## Implemented repairs

- Inmarsat map consumes validated groundTrackDeg from the tracking engine.
  Clockwise rotation follows ground track, not aircraft magnetic heading.
  Unknown motion has a neutral marker; received voice activity retains green.
- ADS-B validates track independently of position and publishes trackValid.
- New ADS-B settings default to2.4 MS/s, not20 MHz. Saved deliberate requests
  and the captureBandwidthMHz API remain compatible. The status/API report
  appliedSampleRateHz from the actual device, not the requested plan.
- Explicit off-capture spectrum browsing permits1529.000-1530.000 MHz watches.
  Unreceived areas remain blank. Panning never retunes; the existing watch
  planner owns tuning, passband coverage and channel-worker selection.
- ADS-B TC20-22 geometric-altitude reports previously reached a TC19-only
  velocity function. They now use the existing airborne CPR position path.
  Reference: FlightAware dump1090 mode_s.c decodeESAirbornePosition/dispatch:
  https://github.com/flightaware/dump1090/blob/master/mode_s.c

## Confirmed existing paths, preserved

InmarsatAero accepts valid ACARS signal units, tracks link direction and invokes
the application decoder before attaching coordinates. Aircraft identity must
match the decoded position. Text/identity alone never creates a map position.
InmarsatMessageStore retains identity separately from position time; a newer
position without a ground vector clears the previous motion flag.

InmarsatTracking distinguishes RF ADS-C and optional online positions, labels
estimates and age, and supplies validated active aircraft IDs. Received call
activity and selected speaker identity are separate; highlighting is explicitly
not sample-exact. Unknown identity cannot be assigned to a guessed aircraft.

The watch planner keeps data/voice simultaneous when all channels fit the
usable capture and worker budget. Otherwise it visits data groups, then voice
groups, returning to data after configured idle/acquisition/refresh limits.
The new1529.5/1542.5 MHz fixture checks distant groups and idle return. No timing
constants, decoder state or speech gates were altered.

## Limits, not completion claims

- RF position availability depends on actual messages received; selecting10500
  does not guarantee positions. No fresh live L-band capture was supplied here.
- Physical SDRplay/RTL readback and reception need tester acceptance; software
  fixtures do not measure RF sensitivity or coax bandwidth.
- Local Mode-S remains a bounded112-bit DF17/18 decoder, not complete dump1090
  parity. Surface CPR, altitude validity/display and non-ICAO DF18 address
  provenance need separate work before claiming full aircraft tracking parity.
- Report freshness and ground-track freshness should ultimately be independently
  visible. ADS-B presently retains local motion for the track lifetime.
- Existing persisted20 MS/s requests are not silently changed. Select2.4 MS/s
  when a narrow capture is desired; actual applied rate is shown after tuning.
- P25 receive, grant following and audio are untouched in this pass.
