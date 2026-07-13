# P25 Phase 2 sdrtrunk RS/BM parity patch

This patch tightens the Phase 2 MAC/ESS recovery path to match sdrtrunk more closely.

## Why

Field logs showed strong Phase 2 RF/TDMA evidence, including repeated superframe and mask locks, but no MAC/ESS progression.  The earlier local ACCH decoder still depended mostly on an erasure-oriented RS helper plus a small brute-force unknown-symbol search.  sdrtrunk uses a full Berlekamp-Massey RS(63,35,29) decoder over GF(2^6), leaves shortened/punctured entries as zero-valued symbols, and lets the RS decoder correct unknown symbol errors before MAC CRC validation.

## Changes

- Added a direct C++ port of sdrtrunk's `ReedSolomon_63_P25` / `BerlekempMassey` logic for RS(63,35,29) over GF(2^6).
- ACCH RS decode now first uses the sdrtrunk-style Berlekamp-Massey decoder before falling back to the older erasure/brute-force path.
- ESS RS decode uses the same path through `rs63DecodeWithUnknownSymbolErrors`, which now tries the sdrtrunk-style decoder first.
- Non-CRC direct ACCH probes remain visible in diagnostics, but they no longer steer CQPSK candidate arbitration.  Only CRC-valid MAC, trusted ESS, or RS/FEC-recovered ACCH candidates influence the candidate ranking.

## sdrtrunk references

- `ReedSolomon_63_P25` / `BerlekempMassey`: full unknown-symbol RS correction over GF(2^6).
- `SacchTimeslot`, `FacchTimeslot`, `LcchTimeslot`: leave punctured/shortened RS symbols at zero and invoke `ReedSolomon_63_35_29_P25`.
- `EncryptionSynchronizationSequenceProcessor`: uses the same RS family for the shortened ESS codeword.

## Expected field-log effect

Before this patch, a strong signal could remain at:

```text
p2sf=12 p2mask=12 p2vcw=N p2mac=0/0 ess=unknown decoded=0 audio=0
```

After this patch, if timeslot/DUID/mask order is correct, MAC candidates should progress to one of:

```text
p2mac=0/N   # ACCH reached but CRC still failing; next target is polarity/mask/CRC/body order
p2mac=N/N   # MAC CRC valid; PTT/ACTIVE/IDLE/HANGTIME should start appearing
```

