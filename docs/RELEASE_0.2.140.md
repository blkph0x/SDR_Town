# 0.2.140: easier receive-only Satcom range scanning

Experimental field-test release. Existing FUBAR 1.1.52 remains compatible;
this update does not change the control DLL ABI or P25 receive/audio path.

## Range Workflow

- Separate **Range Scan**, **Satellite Passes** and **Activity** tabs.
- Saved choices for legacy 243-270 MHz downlinks and broad 240-270,
  240-322 and 335.4-399.9 MHz receive searches.
- Clear MHz From/To fields, saved custom ranges, and advanced mode,
  bandwidth, tune-step and dwell controls.
- Pending edits survive polling. Choosing or saving a range never starts RF;
  Start scan explicitly applies it. Pass arming uses its own catalogue entry.
- Radio, playback, recording and Stop remain outside the scrollable tabs.
- Display nominal tune count and dwell-only lower bound, not an unrealistic
  full sweep promise. The spectrum represents the current capture, not the
  entire search range; idle shows No live capture.
- Validate scan settings before radio acquisition. Named web sessions use the
  same validation and accept integer dwellMs. Scan-start logs include the
  applied range, mode, bandwidth, dwell, step, position count and radio lease.
- Preserve previous user settings/custom presets during upgrade.

## Important Limits

The broad ranges include mixed services/link directions. New UHF presets use
editable analog NFM starting settings. This is not a MUOS WCDMA/DAMA decoder
and does not decrypt traffic or enable transmission. A single radio scans
sequentially and holds a detected signal until Next signal or Stop; it does
not monitor 82 MHz simultaneously or add automatic silence-based resumption.
See [the receive-range guide](SATCOM_RANGE_SCAN.md) for sources and operation.

## Validation

New configuration/GUI fixtures cover migration, tune counts, invalid inputs,
polling-safe drafts, explicit start, persistence and named web controls.
Existing satellite-pass and receiver tests remain regression gates, together
with the frozen-P25 guard. BUILD_NOTES.md records executed local results;
the release's Actions run records package qualification and public download
verification. No on-air MILSAT or physical rotator/TX acceptance is claimed.
