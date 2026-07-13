# IQ Capture Start/Stop + Ring Oversight Implementation Notes

This pass changes the GUI capture workflow from a timed snapshot button to explicit **Start IQ Capture** and **Stop IQ Capture** controls.

## What changed

- Replaced the old single **Timed IQ Capture** GUI button with two controls:
  - `Start IQ Capture`
  - `Stop IQ Capture`
- Capture now starts at the current live IQ absolute sample cursor and continuously drains new samples from `DeviceManager::getRecentIQWindowWithCursor()`.
- IQ is streamed directly to SigMF `cf32_le` data on disk instead of holding the full capture in RAM.
- A 250 ms GUI timer polls the live recent-IQ ring while capture is active.
- Every poll writes synchronized oversight records to:
  - `<capture>_events.jsonl`
  - `<capture>_ring_health.csv`
- Final metadata is written on stop to `<capture>.sigmf-meta`.
- A P25/UI log snapshot is written at stop, including log state at both capture start and stop.
- The root `manifest.jsonl` now marks these sessions as `start_stop_iq_ring_capture`.

## Ring oversight fields

Each ring-health row records:

- UTC wall-clock timestamp
- poll number
- ring window absolute start/end
- cursor position before poll
- appended absolute start/end
- samples appended during that poll
- detected gap samples if the ring overwrote unread data
- total written samples
- RF level, noise floor, SNR, and AFC offset

This allows replay/debug tooling to line up RF samples, P25 events, GUI logs, and ring-overrun conditions.

## Important operational note

This implementation is still bounded by the live recent-IQ ring depth. If the GUI timer or disk cannot keep up, the capture does not silently pretend it is continuous; it logs `gap_samples` and accumulates `ring_overrun_samples` in both JSONL and SigMF metadata.
