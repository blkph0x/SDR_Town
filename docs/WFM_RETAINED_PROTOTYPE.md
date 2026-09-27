# Retained-output WFM prototype - DEC-0152

Test-only implementation: tests/WfmRetainedFirPrototype.h. No application source
includes it. Causal contiguous history computes outputs at 0,M,2M,...; empty
blocks preserve phase. Reset zeros history and restores the original lattice.

## Verification

Release sdr_town_tests, `[wfm][retained],[.wfm-retained-benchmark]`:
478 assertions in three test cases PASS (2026-09-27).

- Exactly matches WfmSpeechFir full-rate convolution followed by selection,
  for factors 1/7/12/51 and lengths 1/65/2049, random IQ and tiny/empty blocks.
- Whole/split cascaded PCM counts match; difference <1e-5. Multiplex arrays
  match exactly and firstSample is contiguous at 2.4/10 MS/s.
- Composite includes audio, 19 kHz pilot, 38 kHz modulated subcarrier and 57 kHz
  tone. This is NOT an encoded RDS reception test or stereo decoder validation.

## Actual downstream PCM and cost

Candidate first filter: 80 dB Kaiser beta, 90 kHz cutoff, ~102.4 us delay,
493 taps at 2.4 MS/s / 2049 at 10 MS/s. Retained IQ is passed to the existing
Demodulator at its reduced sample rate, including its additional channel FIR.
This is a cascade prototype, NOT a drop-in replacement or identical transfer
function to replacing only the original FIR.

Repeated runs at 180 kHz BW, +/- first image minus 30 kHz, +40 dB blockers:

| Input | Deviation | PCM difference vs same-path clean | Wanted gain error | Processing/input |
|---|---|---|---|---|
| 2.4 MS/s | 50 kHz | -86.64 / -86.41 dB | <0.0001 dB | ~0.08 |
| 10 MS/s | 50 kHz | -85.68 / -84.90 dB | <0.0001 dB | ~0.29 |
| 2.4 MS/s | 75 kHz | -4.91 / -5.19 dB | -2.41 / -2.32 dB | ~0.08 |
| 10 MS/s | 75 kHz | -3.37 / -3.36 dB | -3.21 / -3.20 dB | ~0.29 |

50 kHz cases assert difference below -40 dB. Overlapping 75 kHz cases remain
characterization, not a passing quality claim. Timing covers streaming FIR and
downstream demod calls, not coefficient design, IQ generation, UI, hardware or
encoded RDS decoding. Allocation costs within the streaming loop are included.
Signal metrics repeated exactly across three executions; timings are host-only.
Reproduce with `sdr_town_tests.exe "[.wfm-retained-benchmark]"`.

## Remaining production gates

1. Decide post-filter power estimator and squelch semantics explicitly: retained
   samples are not the existing full-rate sample set. Test strong/weak/no-signal
   calibration consistency and external spectrum-level override behavior.
2. Share or separately preserve MPX history/epoch/rate/reset contracts. Run valid
   encoded-RDS fixtures end-to-end through the candidate, not only tone tests.
3. Expand clean/reference comparison and configuration changes to all supported
   bandwidths/rates before replacing the fixed 180 kHz experimental design.
4. Benchmark total GUI/CLI work and physical listening; then guarded production
   patch, full CI and verified release. Until then v0.2.110 stays unchanged.
