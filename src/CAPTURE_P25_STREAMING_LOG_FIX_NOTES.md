# Capture P25 Streaming Log Fix

This pass changes the per-capture P25 log from an in-memory retained line list to an on-the-fly file sink.

## Why

The previous capture-owned P25 log sink retained up to 2000 lines in memory and wrote `_p25_log.txt` when capture stopped. That was safer than relying on the rolling GUI buffer, but long TDMA acquisition/debug sessions can still produce many lines and should not consume capture-session memory.

## What changed

- `LiveIqCaptureSession` now owns an `std::ofstream p25LogStream`.
- `_p25_log.txt` is opened at capture start.
- `appendP25LogLine()` mirrors active-capture P25 log lines directly to disk.
- Capture-time P25 lines are no longer accumulated in `QStringList`.
- The file is flushed every 64 lines and on capture finalization.
- Stop metadata appends final capture status to `_p25_log.txt` instead of rewriting the file.
- Summary/live-status JSON includes:
  - `p25_log_lines_written`
  - `p25_log_write_errors`

## Threading/performance notes

The write still happens on the GUI thread because the current source package does not introduce a new writer thread/header interface. To keep it lightweight, writes are line-buffer style and flushes are throttled. The next deeper hardening step would be a dedicated asynchronous disk writer shared by IQ blocks, ring CSV, JSONL events, and P25 log lines.
