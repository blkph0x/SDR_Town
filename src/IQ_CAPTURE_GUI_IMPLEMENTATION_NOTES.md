# IQ Capture GUI Implementation Notes

This pass adds a GUI timed-IQ capture workflow for production P25 testing.

## Added UI

- Added a **Timed IQ Capture** button beside the existing classifier training capture button.
- The button asks for:
  - capture label
  - duration in seconds, 0.25 to 120.0
- The capture records the next requested time window by waiting for the requested duration and then cutting the matching recent-IQ ring window.

## Output location

Timed captures are stored under the Qt app data directory:

```text
<app-data>/iq_test_captures/<UTC-stamp>_<label>_<freq>MHz_<seconds>s/
```

Each capture contains:

- `<base>.sigmf-data` — raw `cf32_le` IQ samples
- `<base>.sigmf-meta` — SigMF metadata with SDR Town test fields
- `<base>_events.jsonl` — machine-readable aligned event log
- `<base>_p25_log.txt` — human-readable P25/UI log snapshot
- `manifest.jsonl` in the root capture directory for indexing all captures

## Time/sample alignment

Metadata includes:

- UTC capture start/end timestamps
- device center frequency and tuned target frequency
- sample rate
- absolute IQ ring sample start/end counters
- requested duration and actual captured duration
- signal/noise/SNR/AFC metrics
- P25 log snapshot at the time the IQ window is cut

`appendP25LogLine()` now stamps each decoder/UI event with both local time and ISO UTC time so capture events can be correlated with IQ windows and replay output.

## Notes / limitations

- This implementation uses the existing recent-IQ ring, so the maximum reliable duration is limited by the ring capacity configured in `DeviceManager`.
- If the requested duration exceeds available recent-ring history, the capture still saves but reports that the sample window is shorter than requested.
- This is intentionally non-invasive: it does not add a new continuous disk writer thread or require header changes beyond the existing `DeviceManager::getRecentIQWindowWithCursor()` API already present in the codebase.
