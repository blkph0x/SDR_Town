# P25 Phase 2 ACCH/MAC payload fix

Field symptom:

- Strong Phase 2 traffic signal.
- Repeated `p2sf=11/12`, `p2mask=11/12`, `p2vcw>0`.
- `p2mac=0/0`, `ess=unknown`, `decoded=0`, `audio=0`.
- Watchdog eventually returns to control because MAC/ESS never progresses.

Root cause fixed here:

`decodePhase2BurstAt()` now correctly treats a Phase 2 timeslot payload as 320 bits / 160 dibits after the 40-bit ISCH word.  However, `decodePhase2Acch()` still rejected payloads smaller than 170 dibits and used the old half-ISCH-included ACCH extraction offsets.  That made MAC extraction impossible even when the superframe and XOR mask counters were perfect.

Fix:

- Accept 160-dibit Phase 2 timeslot payloads.
- Extract FACCH/SACCH/LCCH using sdrtrunk's 320-bit timeslot field positions:
  - SACCH/LCCH: bits 2..73, 76..183, 184..243, 246..317.
  - FACCH: bits 2..73, 76..135, fragmented INFO_23 bits 136,137,180..183, bits 184..201, 202..243, 246..317.
- Keep the existing RS(63,35,29) symbol placement and CRC checks.
- Preserve alternate bit order, polarity, and small slip probes, but only CRC-valid MAC can promote release state.

Expected log improvement:

- If RF/symbol/mask are correct: `p2mac` should progress from `0/0` to at least non-zero FEC attempts, and eventually CRC-valid MAC when a signaling burst is present.
- If `p2mac` becomes nonzero but CRC stays zero, the remaining issue is likely mask phase, dibit polarity/bit order, or ACCH interleave, not the previous 170-dibit hard reject.
