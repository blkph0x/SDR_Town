# Build 24A — Waterfall stub wave kill

Problem observed: waterfall showed a large sweeping/wave-shaped carrier and strong WFM/P25 signals were not visible. Review of `DeviceManager.cpp` showed the internal stub/no-hardware path still injected a strong synthetic moving complex tone:

- `toneOffsetHz = clamp(0.04 * fs, ...) * sin(t * 0.3)`
- per-block carrier samples at ~0.7–0.8 amplitude
- stub spectrum published via the real FFT path

That exactly matches the moving waterfall wave. It also hid real hardware-open failures because the display still looked active.

Fix:

- Removed synthetic carrier generation from the stub/no-hardware path.
- Stub now publishes only very low-level random IQ and a flat -120 dB spectrum.
- Added explicit log warnings when the device is in STUB/no-hardware mode.
- Preserved the RF restore path from build24 otherwise.

Expected behavior:

- If hardware opens: real WFM/P25 signals should appear.
- If hardware does not open: waterfall should be flat/blank-ish and logs should clearly show STUB/no-hardware mode; no fake sweeping spike.
