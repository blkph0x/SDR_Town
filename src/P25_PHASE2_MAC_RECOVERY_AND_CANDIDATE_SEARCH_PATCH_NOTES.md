# P25 Phase 2 MAC recovery and CQPSK candidate search patch

Field symptom addressed:

- Strong Phase 2 RF where sdrtrunk decodes continuously.
- Local logs show `p2sf=12`, `p2mask=12`, and `p2vcw>0`, but `p2mac=0/0`, `ess=unknown`, `decoded=0`, and no audio.

Root causes found in the local decoder path:

1. The CQPSK candidate search could fast-stop on untrusted Phase 2 telemetry alone. A wrong dibit permutation/mask-phase candidate can still produce repeated Phase 2 sync and voice-looking DUIDs. sdrtrunk does not treat that as proof; it continues through TimeslotFactory/MAC/ESS parsing. The local fast-stop now requires Phase 2 MAC CRC or trusted ESS.

2. The ACCH Reed-Solomon decoder was erasure-only. sdrtrunk's `ReedSolomon_63_35_29_P25` corrects unknown symbol errors as well as the punctured/shortened erasures. The local decoder now performs bounded unknown-symbol recovery for ACCH and ESS before CRC/security decisions.

3. Candidate ranking now prioritizes MAC/FEC/CRC and ESS over raw VCW counts. VCW-only telemetry remains useful for acquisition diagnostics, but it no longer outranks a candidate that actually produces ACCH/MAC evidence.

Expected log progression after this patch:

- If ordering/mask/demod are now right, `p2mac` should move from `0/0` to nonzero.
- Best case: `p2mac=N/N`, `p2acch=nom:N`, MAC_PTT/MAC_ACTIVE/END/IDLE/HANGTIME messages, then ESS/clear-audio release.
- If the next log shows `p2mac=N/0`, the remaining mismatch is probably CRC model, RS symbol order, or bit polarity in the ACCH message body.
