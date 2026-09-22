# Non-P25 hardening — 2026-09-22

Branch: `hardening/non-p25-20260922`

Base: `148fd227e3dc6da74dd5ba99dbf1939c721f8f2a`

## Non-negotiable boundary

The P25 receive, control, follow, traffic, Phase 1/2, vocoder and audio paths are frozen for this work. A repository guard fails when the branch changes a protected P25 file or a shared RF/audio file that feeds P25.

Protected shared files include `DeviceManager`, `Receiver`, `Demod`, `AudioEngine` and the main-window P25 orchestration files. Changes that require those files must wait for a separate, replay-gated P25-safe migration.

## Implemented hardening

### Satcom receive path

- Converts overlapping recent-IQ snapshots into a chronological exactly-once stream using absolute sample coordinates.
- Detects stream epochs, ring overruns and discontinuities, and resets all satellite decoder state after a gap.
- Keeps spectrum and waterfall data live while scanning, locked and pass-armed.
- Uses an isolated Satcom audio output and keeps its queue close to real time.
- Requires `live hardware` state and real IQ before reporting startup success.
- Confirms a requested pass tune before decoder activation.
- Refuses to take a receiver owned by P25.
- Saves and restores the previous device stream/frequency after a pass-only session.
- Records IQ discontinuity counts in capture metadata.
- Displays stream, speaker, discontinuity, log-drop and decoder-output health in the UI.

### SSTV / HamDRM packaging

- Release builds enable the Rust SSTV helper.
- CI runs the Rust transport test and helper self-test.
- Packaging fails instead of silently publishing without the helper or required license notices.

### SDRplay capability safety

- Separates physical model capabilities from controls confirmed by the live Soapy probe.
- An empty or failed probe no longer invents gains, antennas or settings.
- Models connector-specific frequency limits and Bias-T safety for the USB RSP family.
- Distinguishes nRSP-ST as a network/WebSocket receiver rather than a USB Hardware API device.
- Expands official, PothosSDR, radioconda, environment and portable Windows runtime discovery paths.
- Adds regression tests for model normalization, runtime layouts, connector limits, Bias-T and probe failure.

### AX.25 / APRS

- Uses quadrature Bell 202 energy detection.
- Searches timing phase rather than assuming audio begins on a symbol boundary.
- Detects HDLC flags bitwise, performs NRZI conversion and removes stuffed bits.
- Accepts output only after AX.25 FCS validation.
- Tests require a real decoded synthetic frame under arbitrary sample and chunk alignment.

### NOAA APT

- Treats APT correctly as a 2400 Hz AM subcarrier carried by an FM RF downlink.
- Recovers the subcarrier envelope coherently, resamples to 4160 words/s and acquires Sync A.
- Builds 2080-word lines and measures Sync B at the half-line boundary.
- Refuses to invent image lines without valid synchronization.
- Tests use a standards-shaped two-channel synthetic line and verify PGM output.

### ADS-B / Mode-S

- Corrects the 24-bit Mode-S CRC implementation.
- Corrects global CPR longitude selection for an odd-newer pair and local CPR modulo handling.
- Corrects subtype-2 velocity scaling.
- Uses fractional sample-phase PPM acquisition with the complete preamble shape and adaptive confidence checks.
- Gates output on DF17/18 plus valid CRC and suppresses duplicate frames.
- Tests include known valid messages, identity, velocity, CPR, multiple sample rates and structured-noise rejection.

### Inmarsat

- The existing unframed physical-layer bytes are no longer allowed to reach ACARS, message or voice-follow paths.
- Carrier detection uses sustained PSK/MSK moment coherence and survives arbitrary input chunking.
- `locked` and `framesOut` are reserved for validated protocol output and remain false/zero until unique-word acquisition, deinterleaving and FEC are implemented.
- Raw diagnostic blocks are counted but not delivered to higher protocol layers.
- Tests prove random phase noise cannot acquire and coherent BPSK cannot masquerade as a validated frame.

## Acceptance gates

A non-P25 change is acceptable only when:

1. The frozen-P25 guard passes against the base commit.
2. Native Windows build and unit tests pass.
3. SSTV helper build, self-test and packaging checks pass.
4. Satellite chronological-IQ, APRS, APT, ADS-B, SDRplay-profile and Inmarsat fail-closed tests pass.
5. A release package contains the required helper binaries and notices.

## Deliberate residual constraints

### SDRplay live readback

Post-activation antenna reapply/readback, out-of-process device-open timeouts and end-to-end hardware-state readback belong in `DeviceManager`. That file is frozen because it directly supplies P25. These changes require a separate migration protected by a known-clear P25 IQ/audio replay gate and a physical hardware matrix.

### RDS recorded fixture

The recorded MPX fixture exists, but the isolated MinGW RDS backend must be enabled in the standard Windows validation environment before the fixture can become a normal release gate.

### APT field qualification

The decoder now has correct subcarrier and line synchronization fundamentals. Real NOAA captures are still required for telemetry calibration, channel geometry and image-quality qualification.

### Inmarsat protocol decode

No protocol message is claimed from raw physical-layer bits. Production status requires independently verified unique words, framing, deinterleaving, FEC and legal known-good captures.

### DMR

No DMR decoder is present in this branch. It remains outside the supported feature set rather than being represented by classifier labels or UI text.
