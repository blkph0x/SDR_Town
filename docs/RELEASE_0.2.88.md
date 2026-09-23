# SDR Town 0.2.88

## Hardened HF receive and clean capture

This release adds a dedicated HF receive path for **AM, USB, LSB and CW** while leaving the protected P25 receive pipeline untouched.

### HF DSP

- USB and LSB now use separate complex sideband filters rather than collapsing to the same `real()` audio result.
- Automated regression tests require more than 30 dB rejection of the unwanted sideband and cover a stronger interferer in the rejected sideband.
- Stateful phase-continuous tuning and anti-aliased conversion support normal SDR rates including 2.4 and 10 MS/s.
- HF AGC reduces gain quickly on overload and recovers more slowly to limit pumping and noise bursts.
- Short impulsive samples are suppressed before channel filtering.
- AM uses carrier-normalised envelope detection with startup settling so FIR fill does not create a false clipping transient.
- CW uses a narrow complex receive channel and 700 Hz beat note.
- Mode changes reject stale WFM-sized bandwidth and audio-filter settings.
- Tune, sample-rate, mode, bandwidth and stream reset changes clear HF state deterministically.
- HF state registry lifetime is safe through application shutdown.

### HF SSTV and data

- USB/LSB sessions expose a continuous pre-squelch decoder-audio block with sample rate, source frequency, absolute sample position, epoch and discontinuity information.
- Satcom/HF SSTV consumes that clean decoder stream rather than rebuilding input from speaker audio after gain/squelch processing.
- ISS/ARISS SSTV on 145.800 MHz remains **NFM**.
- HF SSTV uses the explicitly selected **USB or LSB** sideband; SDR Town does not mix both sidebands.

### Reproducible clean capture

The existing **Start IQ Capture / Stop IQ Capture** path remains the raw-RF source of truth and was deliberately not rewritten as part of the HF work. It records SigMF cf32 IQ with center/tuned frequency, sample rate, absolute ring position, signal/noise/SNR/AFC diagnostics, gap/epoch information and storage safeguards. This allows difficult HF signals to be replayed after DSP changes without risking the working shared capture/P25 path.

For clean HF recordings:

1. Select the correct SDR and physical antenna port.
2. Confirm the stream is real hardware, not a demo stub.
3. Use explicit AM/USB/LSB/CW for weak signals rather than relying on AUTO.
4. Start with moderate RF gain and reduce gain if the waterfall floor rises broadly or mirrored signals appear.
5. Correct PPM from a known reference where possible.
6. Centre the wanted signal before narrowing RF bandwidth.
7. Keep audio bandwidth wide enough for the intended voice/data/SSTV signal.
8. Disable squelch for weak-signal/raw-IQ capture when appropriate.
9. Preserve raw IQ for important receptions so the exact RF can be replayed.

See `docs/HF_RECEIVE.md` for SDRplay antenna/port guidance, RTL direct-sampling notes and the full clean-capture checklist.

## Validation and P25 safety

The release gate covers:

- workflow YAML validation
- frozen-P25 source comparison
- isolated HF smoke test
- USB/LSB sideband and interference tests
- AM normalisation and startup behaviour
- CW filtering
- overload/impulse handling
- multi-MS/s alias rejection
- HF SSTV decoder continuity and stream-reset semantics
- Windows MSVC Release build
- core unit tests
- Qt/live-decoder tests
- SSTV backend tests
- release verification
- clean portable staging
- ZIP/SHA-256 packaging

**No protected P25 control, follow, traffic, Phase 1/2, vocoder or P25 audio implementation/test file was changed.**

Rollback branches:

- `backup/pre-hf-hardening-20260923`
- `backup/pre-v0.2.88-release-20260923`

Real RF quality still depends on antenna, propagation, local noise, front-end overload and installed hardware/driver support. The automated suite establishes deterministic DSP behaviour; continued field reports across SDR models are welcome.
