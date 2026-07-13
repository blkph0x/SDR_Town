# Capture Logging Fix Notes

This pass fixes issues exposed by the first real start/stop IQ capture package:

- `session_id` was empty because the generated ID was never assigned during start-capture setup.
- `_ring_health.csv` wrote 18 values per row but only 14 column names. The header now includes `bytes_written`, `zero_append_polls`, `max_single_gap_samples`, and `file_write_error_polls`.
- `_live_status.json` stayed `active: true` after Stop because the final status file was last written during the final poll before finalization. Stop now writes a final inactive status record.

The uploaded capture itself was gapless and usable; these fixes improve oversight and downstream analysis reliability.
