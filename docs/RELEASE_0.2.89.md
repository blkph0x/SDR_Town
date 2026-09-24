# SDR Town 0.2.89 - experimental receive-chain repairs

## Repairs

- HF: fixed a noise-blanker latch that could silence a stronger signal. Preserved
  the existing anti-alias response while reducing processing cost. Continuous
  resampling and sample-based AM startup remove callback-size dependence.
- Satellite orbit prediction: replaced the incomplete propagator with a pinned,
  complete SGP4 implementation verified against independent near/deep-space vectors.
- Satellite Doppler: keep the RF center fixed and translate digitally with a
  continuous oscillator and stable decoder identity. No more hardware retunes
  for each Doppler update. Do not discard short IQ blocks.
- P25 metadata: prevent cross-system/site enrichment, preserve the selected
  talkgroup through table sorting/refresh, protect the alias cache and atomically
  save talkgroup files. P25 audio/security/vocoder algorithms are unchanged.
- Devices: report failed tune/direct-sampling operations, preserve native tuner
  limits, use the selected device for direct sampling, and invalidate IQ when
  sampling mode changes. Removed an IQ-publication lock-order inversion.
- Inmarsat prototype: drain chronological input without the previous 1.6384 MS/s
  scheduling ceiling. This is not a completed RF protocol decoder.
- SSTV: align file/live PCM input; expose analog live modes including USB/LSB;
  validate digital FAC CRC and size limits. Digital STWN remains a file-only
  prototype, not an EasyPal-compatible implementation.

## Testing Focus

Pre-release gates passed: all four CTest groups, 367 core and 31 workspace/GUI
tests (six optional fixture/import skips), eight Rust transport tests, actual
GUI dry-run startup/shutdown, and sixteen recorded SSTV worker/live-GUI parity
runs. These do not replace live RF, full hardware-matrix or long-soak acceptance.

Please report the version and source revision from the packaged build-info.json.
Test HF weak-to-strong recovery and long listening sessions, alias import and
selection on multiple P25 systems, RTL direct sampling/native-HF devices,
SDRplay startup, and satellite/SSTV live passes. Preserve existing captures for
before/after comparisons. Do not use this build as proof of hardware certification.

The detailed repair status and outstanding gates are in
[the audit report](https://github.com/Blkph0x/SDR_Town/blob/v0.2.89/docs/AUDIT_20260924.md).
Remaining work includes per-device ownership/lifecycle, driver readback and
composite-tuner acknowledgement, scanner carrier-loss release, a dedicated APT
channel profile, and independent Inmarsat/digital-SSTV interoperability fixtures.
Until the ownership redesign is qualified, do not run competing Listen/Satcom/
Inmarsat controllers against the same radio.

## Downloads

Windows x64 installer, portable ZIP, matching SdrTownControl DLL, signed update
manifest and SHA-256 checksums. SDRplay's vendor API and SoapySDRPlay3 remain
host-installed dependencies. FUBAR itself is unchanged and not re-qualified here.
