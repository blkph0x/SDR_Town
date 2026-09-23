# HF receive and clean-capture path

This document describes the production HF receive path added in SDR Town 0.2.88.
It is deliberately isolated from the P25 receive chain.

## Supported HF analogue modes

| Mode | RF channel default | Audio passband | Notes |
|---|---:|---:|---|
| AM broadcast/utility | 10 kHz | 30 Hz–5 kHz | Carrier-normalised envelope detector |
| USB voice/data audio | 6 kHz | 80 Hz–3 kHz | True positive-frequency complex sideband filter |
| LSB voice/data audio | 6 kHz | 80 Hz–3 kHz | True negative-frequency complex sideband filter |
| CW | 700 Hz | 250 Hz–1.1 kHz | Narrow complex RF filter and 700 Hz beat note |

The USB and LSB paths are independent. SDR Town does not mix both
sidebands. ISS/ARISS 145.800 MHz SSTV remains NFM. HF SSTV is received
using the one sideband selected by the operator, normally USB or LSB
according to the transmitting station.

## Signal path

```text
chronological IQ
  -> phase-continuous digital down-conversion
  -> impulsive-noise blanking
  -> band-limited streaming rate conversion
  -> mode-specific complex RF filter
  -> AM envelope / USB / LSB / CW detector
  -> fast-overload, slow-recovery HF AGC
  -> DC and audio low-pass filtering
  -> smooth RF-level squelch
  -> click-free fade and peak protection
  -> speaker audio and optional SSTV decoder tap
```

Important properties:

- USB and LSB reject the unwanted sideband rather than relying on a
  symmetric low-pass followed by `real()`.
- Strong signals reduce AGC gain quickly. Gain rises slowly after a signal
  fades, reducing pumping and noise blasts.
- Very short impulsive samples are replaced before the channel filter.
- The streaming rate converter expands its anti-alias kernel for common
  multi-megasample SDR rates, preventing signals near 48 kHz multiples
  from folding into the selected HF audio channel.
- Tune, sample-rate, mode, bandwidth and explicit reset changes clear all
  streaming state.
- Stale WFM bandwidth/LPF settings are rejected when entering HF. Safe
  mode defaults are used instead.
- The Satcom/HF receive session publishes clean pre-squelch SSB decoder
  audio with sample provenance and discontinuity information for live HF SSTV.
- The existing NFM, WFM and P25 paths remain in the legacy demodulator and
  are not routed through the HF module.

## Hardware setup

### SDRplay

Use the antenna selector in Device settings. SDR Town validates the
selected model/port frequency range and only exposes controls reported by
the installed SoapySDRPlay3 driver.

For HF:

- RSP1/RSP1A/RSP1B use their normal wide-range SMA input.
- RSP2/RSP2pro and RSPduo can use the Hi-Z input for suitable high-
  impedance HF antennas, but that input is limited to its model-specific
  HF range.
- RSPdx/RSPdx-R2 can use the appropriate A/B/C input; the UI describes
  connector type, frequency range and Bias-T availability.
- Start with hardware AGC off, moderate RF gain reduction, and IF gain
  reduction high enough to avoid overload. Increase gain only until the
  noise floor rises clearly.
- Enable MW/FM/DAB notch functions only when local broadcast overload is
  visible or audible. A notch should not be enabled merely because it is
  available.
- Bias-T must only be enabled for an antenna system designed to accept DC.

### RTL-SDR

Most R820T/R828D RTL sticks require direct sampling for HF below the tuner
range. Select **Q-ADC (HF)** or **I-ADC (HF)** in Receiver Controls as
appropriate for the particular board. Leave direct sampling off when an
external upconverter is in use.

Direct sampling bypasses the normal tuner. RF filtering and overload
performance therefore depend heavily on the antenna and any external
preselector/notch filter.

## Clean-capture checklist

1. Select the correct physical SDR and antenna port.
2. Confirm the hardware stream is real and producing IQ, not a stub.
3. Set frequency correction from a known reference where practical.
4. Use AM, USB, LSB or CW explicitly; do not rely on AUTO for a weak HF
   signal.
5. Begin with the mode defaults above.
6. Reduce RF gain if several stations appear mirrored or the waterfall
   floor rises across a wide span.
7. Narrow channel bandwidth only after the desired signal is centred.
8. Keep the audio low-pass wide enough for SSTV/data tones.
9. Disable squelch for weak-signal or unattended image/data capture unless
   false opening is a problem.
10. Save enough pre-roll or raw IQ for difficult signals so later DSP
    changes can be replayed.

## Validation

The automated HF suite verifies:

- USB and LSB wanted-sideband recovery
- more than 30 dB opposite-sideband rejection
- rejection of a stronger signal in the unwanted sideband
- overload recovery without clipping
- impulsive-noise tolerance
- AM level normalisation
- CW beat-note generation and adjacent-carrier rejection
- safe fallback from stale wideband settings
- multi-MS/s rate conversion and alias rejection at 2.4 and 10 MS/s
- continuous USB/LSB decoder audio for SSTV
- strict dispatch: NFM, WFM and AUTO never enter the HF module

Real-world acceptance should still cover multiple SDR models, strong-signal
urban antennas, weak amateur voice, shortwave AM, CW, and a known HF SSTV
transmission.
