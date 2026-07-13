# Capture P25 Log Sink Fix

This pass fixes a capture-side diagnostic regression found in the 20260616_063314 capture.

## Problem
The SigMF IQ capture was healthy, but the per-capture `_p25_log.txt` contained only headers.
The cause was the rolling GUI log retention. The capture finalizer attempted to reconstruct
"lines added during capture" from the retained GUI buffer using `startP25LogIndex`; once the
GUI buffer had already reached its rolling cap, new lines displaced old lines and the index
could point to the end of the capped buffer.

## Fix
- Added a capture-owned `p25LogDuringCapture` sink.
- `appendP25LogLine()` now mirrors each P25 line into the active capture session regardless
  of whether the log dialog is visible.
- The capture-owned sink is capped independently at 2000 lines, with a dropped-line counter
  recorded in `_p25_log.txt`.
- The final P25 capture log now writes:
  - lines retained during the capture window,
  - dropped-line count if the cap was exceeded,
  - startup context retained at capture start.

## Result
Future captures will keep TDMA acquisition diagnostics in the capture folder even when the
visible P25 log window is closed or the GUI text buffer rolls over.
