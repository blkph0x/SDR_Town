# P25 Phase 2 ACCH direct-CRC / RS-bypass field patch

Field symptom: strong RF and repeated Phase 2 telemetry (`p2sf=12`, `p2mask=12`, `p2vcw>0`) while `p2mac=0/0`, ESS remained unknown, and audio never decoded. This means the failure is below the audio/security gate.

sdrtrunk extracts the 320-bit Phase 2 timeslot, classifies the DUID, reconstructs the SACCH/FACCH/LCCH MAC message with Reed-Solomon correction, and then validates the MAC payload CRC. The local implementation already had the timeslot field offsets aligned, but it still required the local RS helper to succeed before trying the MAC CRC. If the local RS orientation/generator is wrong, even a perfect ACCH payload is discarded before the MAC CRC can prove it.

This patch adds a fail-safe inside `decodePhase2Acch()`:

- Build the raw ACCH information field directly from the sdrtrunk-aligned timeslot bit ranges.
- Check the Phase 2 FACCH/SACCH/LCCH CRC before requiring local RS recovery.
- If direct CRC passes, return a valid MAC PDU immediately with zero corrected symbols.
- If direct CRC fails and RS also fails, return one non-CRC diagnostic ACCH candidate instead of `nullopt`, so field logs can distinguish "ACCH reached but CRC/RS failed" from "ACCH never reached".

Expected next log shift:

- Before: `p2mac=0/0 p2acch=nom:0 ...`
- After, if ACCH bit extraction is now reached: `p2mac=0/N` or `p2mac=N/N`.

If it becomes `p2mac=0/N`, the remaining mismatch is CRC/body bit order, descrambling phase, or CQPSK dibit mapping. If it becomes `p2mac=N/N`, MAC/ESS/session handling should start progressing.
