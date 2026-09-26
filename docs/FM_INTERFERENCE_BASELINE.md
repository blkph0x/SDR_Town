# FM interference and cost baseline - 2026-09-27

DEC-0146 / T-0074. Production DSP is unchanged from user-accepted0.2.108.
This is a synthetic baseband test, not receiver certification, SINAD, BER or
evidence of front-end overload. No filter tuning is justified by RMS alone.

## Reproduce

Build the Release sdr_town_tests target, then run:

```powershell
python scripts/benchmark_fm.py --exe build/bin/Release/sdr_town_tests.exe --output build/fm-benchmark.json --repeat 3
python scripts/test_benchmark_fm.py
```

The report records36 cases per run, source HEAD/dirty status, executable hash,
host platform and UTC time. Source HEAD is checkout context, not an attestation
that arbitrary supplied binaries were compiled from it. Use the executable hash
to identify the measured binary. Report stays local; no automatic upload.

Wanted900Hz and blocker1700Hz FM, deviation1800Hz NFM/50kHz WFM. Both share
the same deviation within each case. IQ peak<=0.5; wanted-only reference has
identical wanted amplitude. Blocker RF levels0/+20/+40dB relative to wanted.
200ms inputs,8192-sample blocks,48kHz PCM; analyse last4800 audio samples after
startup. Fixed open squelch isolates signal processing from threshold decisions.
Sample rates2.048/2.4/10MS/s. Adjacent offsets25kHz NFM/400kHz WFM. Image offset
fs/round(fs/internalTarget)+1500Hz, using192kHz NFM and198kHz WFM targets at the
selected12500/180000Hz channel widths. This matrix is not a full offset sweep.

Tone amplitude is sine/cosine projection over integer periods; validated against
known mixed tones. Difference is RMS(test-reference)/RMS(reference), including
distortion/noise/phase change, NOT just blocker leakage. Timings exclude fixture
generation and report IO; demod calls include initialization and allocations.
No hard machine-speed assertion is used. Report parser rejects missing/duplicate
cases, nonfinite measurements and missing output.

## Observed results

Three runs on this Windows host, source b88ac00 plus benchmark-only changes;
test executable SHA256:
03216b16d653926230c09014de0af607e0013cc9b642a2699df8d10cba1ffeeb.
Local report: build/fm-benchmark-108.json. Numerical signal metrics repeated;
CPU ratios varied slightly. Ratio1 means processing consumes the input duration.

| Case | Wanted gain dB | Blocker audio vs clean wanted dB | Difference dB | Time/input duration |
|---|---:|---:|---:|---:|
| NFM2.4MS/s, image, +0dB | ~0 | -94.49 | -50.49 | ~0.08 |
| NFM2.4MS/s, image, +20dB | ~0 | -74.99 | -30.42 | ~0.08 |
| NFM2.4MS/s, image, +40dB | -5.55 | -8.63 | +3.23 | ~0.08 |
| NFM10MS/s, image, +40dB | -4.01 | -8.78 | +3.83 | 0.16-0.17 |
| WFM2.4MS/s, image, +40dB | ~0 | -107.57 | -74.03 | ~0.63 |
| WFM10MS/s, image, +40dB | -0.001 | -77.21 | -56.56 | ~2.59 |

At +40dB adjacent offsets the NFM difference was below-108dB in these synthetic
fixtures. That does not generalize to all offsets, modulations or real RF.
At10MS/s WFM channelizer consumed515820us of517184us demod-call time in one
image case (>99%). The RF/DDC/FIR stage therefore dominates this measurement.

## Next isolated work

1. NFM: compare anti-alias first-stage candidates against this image-blocker
   reproduction. Preserve wanted passband, CTCSS/DCS taps, stream counts and
   latency accounting. Measure a broader offset sweep before choosing a default.
2. WFM: profile FIR/DDC separately; optimize computation without changing filter
   response. A decimating FIR alone changes available full-rate power estimates,
   so squelch and meter semantics require explicit treatment, not an unnoticed
   side effect. Compare PCM and RDS sample-for-sample and then measure cost.
3. Keep RF hardware acceptance separate: front-end overload, AGC, antenna noise
   and ADC quantization are not represented here. Current0.2.108 remains the
   listening baseline; no new runtime release is needed for test-only tooling.
