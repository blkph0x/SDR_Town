# Forward Hardening Additions

This pass adds capture-session metadata that makes future debugging easier:

- Stable session IDs for every start/stop IQ capture.
- Live status JSON updated on each capture poll.
- Final summary JSON with a simple health verdict.
- Byte counters and file-write-error counters.
- Maximum single ring gap and zero-append poll counters.
- Replay helper text file beside the capture.
- Dependency-free offline analyzer under `tools/analyze_iq_capture.py`.

These additions are intentionally format-stable so CI or a field tester can archive captures, run replay, and compare decoder behavior against the same sample ranges later.
