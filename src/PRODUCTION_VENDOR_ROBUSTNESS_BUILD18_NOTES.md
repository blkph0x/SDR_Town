# Production Vendor Robustness Build 18

This pass hardens Phase 2 operation for mixed-vendor and weak-signal systems without weakening the clear-audio release gate.

## What changed

### 1. Vendor-aware encryption metadata on Phase 2 MAC grants/users
- Standard Phase 2 MAC Group Voice User messages now apply service options from the MAC payload.
- Motorola MFID `0x90` regroup/user/grant/update paths now apply the service-options byte when present.
- This helps prevent known-encrypted activity from being followed or held because the encryption flag was missing from vendor-specific grants.

### 2. DUID-independent ACCH/MAC recovery
- When TDMA superframe and XOR mask lock are present, the decoder no longer trusts a noisy DUID as the only MAC layout selector.
- The nominal burst kind is still attempted first.
- If no CRC-valid MAC appears, the decoder tries alternate ACCH interpretations:
  - SACCH scrambled / clear
  - FACCH scrambled / clear
  - LCCH clear
- Existing conservative hypotheses remain in place:
  - swapped dibit bit order
  - small ACCH dibit slips
  - inverted dibit polarity

### 3. Release gate remains strict
- These alternate ACCH paths do **not** release audio unless the MAC PDU passes CRC.
- Voice-only ESS remains diagnostic unless backed by valid MAC context.
- Encrypted ESS still forces audio closed.

## Why this matters

Your captures have already proven tuning, TDMA superframe, and XOR mask can work (`sf=12 mask=12`), while MAC stayed `0/0`. That usually means the remaining issue is not tuning or slot selection, but ACCH/MAC extraction. This build makes that extraction less dependent on one potentially noisy DUID classification and more tolerant of vendor/system variations while keeping the safety gate intact.

## What to look for next

Good progress:

```text
p2sf=12 p2mask=12 p2mac=1/N p2ess=clear
```

Still blocked:

```text
p2sf=12 p2mask=12 p2mac=0/0 p2ess=unknown
```

If it still stays blocked after this build, the next target is the deeper ACCH interleave / RS symbol placement table itself and should be validated against golden OP25/known-good captures per vendor.
