# Audio gate / TDMA no-lock hardening

This pass addresses the reported symptom: after a P25 auto-follow retune the audio gate can briefly open with white noise, then close with no TDMA lock.

Changes:
- Hard-mutes the receiver before every P25 retune.
- Clears the AudioEngine ring buffer before/after P25 voice arming.
- Extends P25 voice settle/discard windows after retune, especially for Phase 2.
- Requires decoded P25 voice blocks to pass a final `p25VoiceBlockMayEmitAudio()` gate before pushing samples to the audio engine.
- Blocks Phase 1 IMBE false-positive audio when the same window shows Phase 2/TDMA evidence or lacks validated NID lock.
- Reduces GUI P25 log retention from 1200 to 500 visible lines and throttles repeated Phase 2 burst/no-TDMA lines more aggressively.

The IQ capture remains the source of truth for long diagnostics; the GUI log is now intentionally smaller so capture sessions are easier to run.
