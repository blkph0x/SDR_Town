# Production Hardening Stage 5 / Build 16

This pass finishes the remaining production-audit list items that were still practical in the cpp-only package.

## Completed

### 1. Full Soapy open identity
- Added a cpp-local `makeSoapyKwargsForDevice()` helper.
- Real hardware open now uses the most specific preserved identity:
  - `driver`
  - `serial` when available
  - fallback `hardware` and `label` when serial is absent
- Real-open logging now prints the Soapy kwargs used for easier device-mismatch diagnosis.
- Probe/unmake and real-open cleanup are more consistently serialized through the global Soapy API mutex.
- Cleanup now attempts `deactivateStream()` before `closeStream()`.

### 2. Unified squelch/SIG/NF spectrum scale
- The squelch drag line now uses the same dB-to-Y mapping as SIG and NF.
- `yFromSquelchViz()` and `squelchVizDbFromY()` remain as compatibility wrappers but delegate to `yFromDb()` / `dbFromY()`.
- This fixes the misleading visual mismatch where SQ could appear above/below SIG/NF even though the actual gate used different values.

### 3. Phase 2 MAC/ESS acquisition robustness
- ACCH/MAC decode now tries normal dibit bit order first.
- If no CRC-valid MAC PDU is recovered, it retries with swapped dibit bit order.
- Audio release remains conservative: only CRC-valid MAC/ESS can release audio.
- This gives Phase 2 systems a chance to recover MAC PDUs when CQPSK slicer bit ordering/polarity differs while preserving the hard release gate.

## Still requires validation
- Run a clean Phase 2 capture where logs previously showed `sf=12 mask=12 mac=0/0 ess=unknown`.
- The desired result is any movement in `mac`, for example `mac=1/1`, followed by ESS state moving away from unknown.
- If MAC still remains `0/0`, the remaining likely issue is ACCH deinterleave/RS layout rather than mask phase, tuning, or slot selection.
