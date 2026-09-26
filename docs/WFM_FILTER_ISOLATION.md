# WFM filter isolation - DEC-0151

## Method

`python scripts/test_wfm_filter_oracle.py` verifies the NumPy FFT causal
convolution against direct convolution (including one-sample input), coefficient
symmetry/normalization, and phase-discriminator scaling on a known carrier.

`python scripts/analyze_wfm_filter.py --output build/wfm-filter-oracle.json`
evaluates 24 cases. This is an independent double-precision signal model, NOT
the actual application PCM output or a CPU benchmark. It models the current
coefficient formula, including zero Kaiser endpoints, float coefficients, the
rounded decimation factor, and retained positions 0,M,2M,... . It reports
blocker-only residue before decimation and clean/mixed discriminator errors.
Ideal FM reference is delayed by the FIR group delay in input samples.

Candidates: 2049/4097 taps, unchanged 90 kHz cutoff for a 180 kHz channel,
Kaiser beta for 80 dB. Tap counts are experimental choices, not a guaranteed
attenuation specification. Background: https://www.liquidsdr.org/doc/firdes/.
No SciPy is used: this host's installed SciPy warns of a NumPy incompatibility.

## Observed 2026-09-27

10 MS/s, 180 kHz channel, 50 kHz deviation, +166078.43 Hz blocker at +40 dB:

| Taps | Blocker/desired after FIR dB | Mixed discriminator error dB | Clean vs ideal error dB | Delay us |
|---|---:|---:|---:|---:|
| 321 (current model) | +14.16 | +2.50 | -79.06 | 16.0 |
| 2049 | -52.40 | -52.62 | -103.05 | 102.4 |
| 4097 | -62.06 | -62.18 | -102.92 | 204.8 |

The modeled leakage exists before downsampling. Therefore a real selectivity
weakness has been isolated; blaming only output resampling is insufficient.
These discriminator errors differ from application PCM errors because the
speech LPF, de-emphasis and output resampling are deliberately absent here.

At 75 kHz deviation the longer filter does NOT solve this offset: residual
blocker/desired is still about +21 dB for 2049 taps. Wide FM spectra overlap
the filter transition/wanted region. Do not classify every sweep failure as
something a sharper anti-alias filter can eliminate while preserving desired
bandwidth. Narrower selection is a user-visible tradeoff, not a hidden fix.

## Candidate rejected for direct full-rate adoption

At 10 MS/s, 2049 full-rate taps require 20.49 billion complex-by-real tap
accumulations per second versus 3.21 billion for 321 taps (6.38x arithmetic).
These are operation counts, not measured wall-clock times or CPU instructions.
Do not multiply the existing timing by this ratio and claim a measured result.

Next implementation: a rate-aware, staged channelizer or retained-output FIR.
Preserve explicit channel power/squelch semantics: current code observes all
full-rate filtered samples. Keep the RDS branch's stream identity, reset and
rate contract. Validate actual C++ PCM/MPX against reference, partitions, clean
and blocked signals, realistic stereo/RDS modulation and CPU before release.

No DSP source or published binary was changed. This closes root-isolation work,
not the production repair. P25/NFM/HF are untouched.
