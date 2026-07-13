# Thread Safety / GUI Lag Fix Notes

This pass focuses on the lag introduced after the TDMA acquisition and continuous IQ capture work.

## Root cause found

The capture timer was running on the Qt GUI thread and, every 250 ms, could request a 2-5 second IQ window, copy tens of megabytes, write IQ one complex sample at a time, flush the IQ file, flush CSV, flush JSONL events, and rewrite live status JSON.

That is safe from a data-race perspective because it runs on the GUI thread, but it is not UI-safe from a responsiveness perspective. Disk I/O and large vector copies on the GUI thread cause visible lag.

## Fixes made

- Reduced the live capture pull window from 2-5 seconds to a bounded sub-second window.
- Replaced per-sample IQ writes with one bulk `std::complex<float>` block write.
- Reduced forced file flushes to every fourth poll and final poll.
- Kept per-poll CSV detail but reduced JSONL ring poll events to periodic snapshots, gaps, errors, and final poll.
- Reduced retained GUI P25 log lines from 500 to 300.
- Set `QTextDocument::setMaximumBlockCount(300)` on the visible P25 log window.
- Avoided updating the QTextEdit unless the log dialog is actually visible.

## Thread-safety observations

- Audio callback remains lock-free and does not do heap allocations.
- Live capture state remains GUI-thread-owned.
- Device IQ ring reads continue through `DeviceManager::getRecentIQWindowWithCursor()`, which is the correct synchronized access point.
- Receiver vector mutations remain protected by `receiversMutex` and receiver internals by `Receiver::stateMutex`.
- The remaining architectural improvement is to move capture disk writes to a dedicated writer thread. This pass avoids adding new header-visible types and keeps the source-only package compatible.

## Expected result

Continuous IQ capture and TDMA acquisition logging should feel noticeably less laggy while still preserving enough oversight to diagnose ring gaps, file errors, AFC/SNR, and TDMA lock state.
