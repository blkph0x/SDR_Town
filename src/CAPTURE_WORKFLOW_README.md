# IQ Capture Workflow for Production Validation

This package adds stronger capture oversight so each RF test run is reproducible and reviewable.

## Capture flow

1. Tune the receiver and wait for live spectrum/metrics to settle.
2. Click **Start IQ Capture**.
3. Exercise the receiver path being tested: Phase 1 voice, Phase 2 slot 0/1, weak signal, encrypted rejection, etc.
4. Click **Stop IQ Capture**.
5. Review the generated capture directory under Qt AppData `iq_test_captures`.

## Files generated per capture

- `*.sigmf-data`: raw `cf32_le` IQ stream.
- `*.sigmf-meta`: SigMF metadata with SDR Town fields.
- `*_events.jsonl`: start/stop/poll event stream.
- `*_ring_health.csv`: ring cursor, gap, and write-health data per poll.
- `*_live_status.json`: current capture status while recording.
- `*_summary.json`: final health summary.
- `*_p25_log.txt`: synchronized UI/P25 log snapshot.
- `*_replay.txt`: helper command for replay.

## Health field

- `ok_gapless`: no ring gaps or file write errors were detected.
- `warning_ring_gaps_present`: capture is usable but has missing IQ samples.
- `bad_file_write_errors`: disk write path reported failure.
- `bad_empty_capture`: capture has no useful samples.

## Offline check

```bash
python tools/analyze_iq_capture.py /path/to/capture_directory
```

The script exits non-zero when the capture health is not `ok_gapless`.
