# NFM Tone Identification

Experimental, receive-only CTCSS identification for the classic 38-tone set.
In NFM, including AUTO resolved to NFM, the status above the spectrum shows
the detected tone after two matching one-second windows. No audio gating,
tone suppression or automatic repeater configuration is implemented.
Normal squelch and audio controls are unchanged. Unconfirmed/ambiguous windows
clear the displayed tone. After two seconds without input the GUI hides it.

The independent FM data tap precedes speech filtering, de-emphasis and squelch.
Nominal frequency identifies a stream; phase-continuous AFC corrections do not
reset it, but RF retunes, source gaps, sample-rate and mode changes do. A
one-second Hann/Goertzel analysis bank compares supported tones and guards.
The engineering thresholds in DEC-0081 are verified against synthetic signals,
not claimed to be a certified CTCSS receiver specification. Persistent voice
components near a tone can still be misleading; independent RF validation is
required before any optional tone-controlled audio squelch is enabled.

## Sources

- GNU Radio's classic frequency table and guard-detector approach:
  https://github.com/gnuradio/gnuradio/blob/main/gr-analog/lib/ctcss_squelch_ff_impl.cc
- TI Goertzel mathematics: https://www.ti.com/lit/pdf/spra096
- SvxLink's production detector documents selectivity/frequency estimation:
  https://github.com/sm0svx/svxlink/blob/master/src/doc/man/svxlink.conf.5

No upstream implementation code is copied into this module.

## Testing

```powershell
build/bin/Release/sdr_town_tests.exe '[ctcss]'
python scripts/test_ctcss_cli.py
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'tones file "recording.wav"'
```

Input: mono WAV/FLAC, 8..96 kHz, maximum 120 seconds, preferably a discriminator
recording retaining subaudible tones. JSON reports current tone, purity, samples,
analysis windows and confirmed windows. A file ending in silence correctly
reports no current tone. Error text, not process exit alone, is the CLI gate.
GUI self-test JSON includes the `ctcss` snapshot and nominal channel frequency.

Next gates: independent known-tone RF capture, expanded tone sets and frequency
offset/voice-falsing evaluation; DCS reference framing, codeword/polarity tests;
then opt-in tone squelch with measured acquisition/release timing. The classic
38-tone subset does not include, for example, 69.3 or 254.1 Hz.

## Experimental DCS

ETSI TS 103 236 section 4.2 specifies 23-bit words, LSB-first transmission,
134.4 bit/s and frequency-deviation polarity:
https://www.etsi.org/deliver/etsi_ts/103200_103299/103236/01.01.01_60/ts_103236v010101p.pdf

DEC-0084 implements informational identification from the same raw NFM tap and
offline mono discriminator files. The catalogue contains 105 payloads matching
the checked SDRTrunk enumeration (its comment says 104). Codewords are generated
algebraically; all cyclic alignments and physical inversions are indexed.
Three equal, parity-valid words at exact 23-bit spacing confirm a candidate;
isolated matches and single-bit corruption do not qualify. No error repair is
attempted. A candidate expires after 46 bits without fresh repeated-word evidence.

Eight staggered nominal-rate timing hypotheses integrate the low-passed/DC-removed
discriminator. At least two must agree with a unique vote winner. This is an
experimental engineering profile, not a certified or adaptive-clock DCS squelch.
Tests include 8/48/96 kHz, +/-0.1% baud error, shaped transitions, DC, voice-band
interference, noise and source discontinuities; this does not establish RF sensitivity.

SDRTrunk's DCSCode comments use bit-reversed/unreversed words for its N/I labels;
this implementation uses true inversion against independent ETSI fixtures.
Because a repeating signal has no unique word boundary, equivalent code/polarity
labels are reported together, e.g. `023N / 047I`. They describe the SAME signal,
not simultaneous radios. Do not choose a user's configured label from frequency
or an arbitrary alignment. There is no automatic DCS audio gating.

The GUI displays codes above the spectrum and publishes `dcs` in its self-test
JSON. CLI diagnostics share the same decoder; CLI hardware monitoring has not
been connected to DCS. Commands (offline, no SDR enumeration):

```powershell
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'tones dcs "discriminator.wav"'
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'tones dcs-bits "recording.bits"'
python scripts/test_dcs_cli.py
build/bin/Release/sdr_town_tests.exe '[dcs]'
```

Audio limits: mono WAV/FLAC 8..96 kHz, 120 seconds. Bit files: chronological
0/1 ASCII plus whitespace, max 64 KiB and 16,128 bits. Results contain all
equivalent labels, sample count and agreeing timing phases. No code means no
confirmed current code, not a failed RF/hardware assertion. Test error text as
well as process exit; the legacy CLI does not return command-specific exit codes.
Independent known-code RF acceptance is still deferred until the user's radio
is available. Longer fading/drift/performance qualification and optional
tone-controlled squelch remain open. P25/speaker processing is unchanged.

## DTMF (opt-in repeater monitor)

See `docs/REPEATER_MONITOR.md`. DTMF uses the same NFM discriminator tap with a
Goertzel keypad bank. It runs only when Repeater Control Monitor is enabled.

```powershell
build/bin/Release/sdr_town_tests.exe '[dtmf]'
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'tones dtmf "discriminator.wav"'
```

An independent protocol oracle and bounded ideal discriminator fixtures are now
available (not live decode or RF evidence):

```powershell
python scripts/test_dcs_reference.py --output build/dcs_reference_qa
```

This checks four literal ETSI codewords, all 512 nine-bit payload syndromes,
single-bit error detection, LSB-first order and true polarity inversion. Eight
five-second mono WAVs plus a manifest test the decoder without requiring
hardware. These square-NRZ fixtures deliberately
do not claim to reproduce transmitter filtering, fading, noise or clock error.
