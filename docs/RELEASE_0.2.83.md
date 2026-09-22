# SDR Town 0.2.83

This release hardens the non-P25 receive paths while deliberately leaving the complete P25 receive, control, follow, traffic, Phase 1/2, vocoder and audio chain unchanged.

## Satcom

- Manual Scan and automatic in-range capture can take control from an ordinary active Listen session without requiring the device to be disabled first.
- Satcom refuses to interrupt a P25-owned receiver.
- Satellite IQ is consumed chronologically rather than replaying overlapping recent windows.
- Spectrum and waterfall remain live while scanning, locked and pass-armed.
- Satcom has an independent low-latency monitor-audio path.
- Pass startup verifies real hardware, real IQ and the applied tune before decoder activation.
- Previous receiver streaming and centre frequency are restored after Satcom releases a pass-only session.

## SSTV and HamDRM

- Release builds now require and package the Rust SSTV helper and license notices.
- CI exercises the helper transport and self-test before packaging.

## SDRplay

- Runtime discovery and preflight diagnostics cover official SDRplay, PothosSDR, radioconda, environment and portable layouts.
- Failed probes no longer invent antennas, gain stages or settings.
- Model and connector capability checks enforce antenna-specific frequency and Bias-T limits.
- nRSP-ST is identified as a network/WebSocket receiver rather than a USB Hardware API device.

## Other receive-path hardening

- AX.25/APRS: Bell 202 timing search, HDLC/NRZI handling, bit de-stuffing and FCS-gated output.
- NOAA APT: FM-downlink handling, coherent 2400 Hz subcarrier recovery, Sync A/B acquisition and 2080-word line construction.
- ADS-B/Mode-S: CRC, CPR, subtype-2 velocity, fractional sample phase, preamble confidence and duplicate suppression corrections.
- Inmarsat: unframed physical-layer bits are prevented from masquerading as validated protocol messages.

## Safety and validation

- A repository guard rejected any change to protected P25 or shared P25-critical RF/audio paths.
- Full MSVC Release build, core tests, Qt/live-decoder tests, SSTV tests, release verification and portable packaging passed before merge.
- Rollback branch: `backup/pre-non-p25-hardening-20260922`.

Physical SDR reception, satellite transmission timing and model-specific SDRplay hardware behaviour still require field testing on connected equipment.
