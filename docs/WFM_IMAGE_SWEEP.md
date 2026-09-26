# WFM wider image sweep - DEC-0150

## Reproduce

Build Release sdr_town_tests, then run:

```powershell
python scripts/benchmark_fm.py --exe build/bin/Release/sdr_town_tests.exe --output build/wfm-sweep-110.json --repeat 2 --wfm-sweep
python scripts/test_benchmark_fm.py
```

72 combinations: 2.048/2.4/10 MS/s; 150/180/220 kHz channel bandwidth;
50/75 kHz FM deviation; positive/negative first decimation image with folded
offset +/-30 kHz. Actual rate follows current bandwidth-dependent rounding.
Wanted tone 900 Hz, blocker 1700 Hz, blocker +40 dB; IQ peak bounded by 0.5.
Compare final 100 ms of a 200 ms actual Demodulator run against identical clean
IQ. No RF hardware or ADC overload is modeled.

## Measured 2026-09-27

Two runs on unchanged 0.2.110 production DSP, test changes on ecfe239.
Wanted gain, difference and sample counts repeat exactly between runs.

| Input MS/s | Worst PCM difference dB | Largest wanted loss dB | Cases difference > -40 dB (of 24) |
|---|---:|---:|---:|
| 2.048 | -4.36 | 2.84 | 4 |
| 2.4 | -4.10 | 2.99 | 4 |
| 10 | -1.38 | 4.75 | 10 |

Worst-case columns need not describe the same combination. At 10 MS/s,
180 kHz BW, 50 kHz deviation, blocker -166078.43 Hz, wanted loss is 3.38 dB
and PCM difference -1.38 dB; at +166078.43 Hz they are 3.41/-1.39 dB.
Peak processing/input ratio in first run approximately 0.14/0.16/0.66.

## Interpretation and next gate

This exposes corruption missed by the previous image+1500 Hz matrix. It does
not distinguish pre-discriminator blocker leakage, aliasing, or FM spectral
overlap. Wide deviation with narrow bandwidth removes wanted sidebands too;
comparison with an identically filtered reference does not measure that loss.
The -40 dB column is a screening threshold, not a reception standard.

Next isolate 10 MS/s / 180 kHz / 50 kHz / +/-166078.43 Hz: measure filter
response and IQ blocker residue before downsampling; compare rate plans at
equal passband and clean audio against an independent FM reference. Include
broad-offset blockers, stereo/RDS composites, chunk invariance, meter/squelch
behavior and CPU before production adoption.

This opt-in characterization fails for invalid outputs, not for a chosen
quality threshold. Passing it is NOT clear-audio proof. The parser rejects
incomplete/duplicate matrices, nonfinite metrics, missing audio and incorrect
blocker power. Production and published assets remain unchanged.
