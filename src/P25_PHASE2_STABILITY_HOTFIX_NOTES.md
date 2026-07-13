# P25 Phase 2 stability hotfix

This hotfix backs the realtime decoder away from the expensive recovery paths that can stall the GUI/DSP worker.

Changes:

- Disabled the multi-target Phase 2 offset probe in the live DSP loop. The probe ran several complete P25 decodes per window and could freeze on false Phase 2 telemetry from a selected control channel.
- Restored non-blocking receiver/state/DSP mutex behavior for voice arm. If the DSP worker owns the decoder, the existing retry timer defers the arm instead of blocking the Qt thread.
- Made non-deep ACCH recovery use direct CRC plus cheap erasure RS only. Full Berlekamp-Massey/unknown-symbol recovery remains in the code for deep/offline use, but is no longer used for every live ACCH hypothesis.
- Forced realtime Phase 2 burst decode to use non-deep ACCH hypotheses.

Goal: selecting a control channel and clicking UI controls must remain responsive. Once live logs are stable again, re-enable deeper recovery behind an explicit debug/offline capture path rather than in the hot loop.
