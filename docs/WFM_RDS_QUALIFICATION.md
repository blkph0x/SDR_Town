# Candidate WFM RDS and power qualification - DEC-0153

No production DSP changes. New mandatory test cases in test_fm_benchmark.cpp
qualify the test-support filter; shipping a runtime change remains separate.

## Recorded RDS through RF

The existing mono FLAC MPX reference is linearly interpolated and FM-modulated
at 2.4/10 MS/s with continuous phase, then processed in 8192-sample input blocks.
Both causal filter supports are drained at recording end. This is synthetic
RF derived from a recording, not live antenna reception.

Three simultaneous paths on the identical IQ:

| Path | 2.4 MS/s CRC-valid groups | 10 MS/s CRC-valid groups |
|---|---:|---:|
| Current application demod + multiplex | 2 | 2 |
| Candidate FIR + existing reduced-rate demod multiplex | 1 | 1 |
| Candidate FIR + direct phase discriminator | 3 | 2 |

Direct playback of original MPX gives two groups. Initial candidate test requiring
two groups FAILED at both rates. Draining FIR supports did not fix it. Isolating
the candidate's direct discriminator identified the second filtering path as
the regression; using a 57 kHz tone alone had missed it.

The selected data architecture is now direct retained-FIR discriminator, not
the cascaded speech path. Tests retain the old and double-filter comparison
paths and require direct CRC-valid groups >=2 and >=baseline, final PI 0x6201,
and PTY 14. This changes the selected prototype branch, not acceptance thresholds.
Three groups do not prove superior RDS reception generally; the fixture is short.

## Power

At both rates, average full-FIR and retained-FIR power agree within 0.1 dB for
a stationary 1 kHz carrier plus seeded Gaussian noise after two FIR lengths of
settling. The comparison is relative digital power, not calibrated dBm. It
does not prove arbitrary transient, burst, threshold or hang behavior.

## Gates still open before runtime adoption

- Transient level and squelch attack/hang/release, external RF metric override.
- Bandwidth/rate changes and retunes; prototype qualification so far uses 180 kHz.
- Production sample counts, history ownership and resets for separate data/speech.
- Full runtime regression, CPU tests, CI release and downloaded-asset verification.

Run: `sdr_town_tests.exe "[rds-rf],[power]"`. 3207 assertions / two test cases
PASS on 2026-09-27. RDS cases require SDR_TOWN_TEST_RDS_DSP and shipped backend.
