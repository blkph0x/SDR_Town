# Inmarsat Classic Aero

Native experimental receive implementation, not yet RF-qualified on a tester's
antenna/SDR. P25 is unchanged. This is not a decoder for every Inmarsat service.

## Implemented

- Shared live/IQ-replay channelizer and JAERO-derived modem: continuous MSK
  600/1200, OQPSK 10500, C-channel 8400; R/T burst 1200/10500 data.
- Actual unique-word framing, deinterleaving, convolutional FEC, descrambling
  and signalling CRC. No ASCII-regex path to live assignments or map positions.
- Binary P-channel C-assign messages expose AES/GES and receive/transmit frequency.
- C-channel mini-m AMBE4800x3600 codec, isolated from P25 in sdr_aero_codec.dll.
  One persistent codec state per receive stream; 25 words / 500 ms C frame;
  160 samples at 8 kHz per 20 ms word. Requires current-frame valid signalling.
- Default speaker playback and optional bounded, non-overwriting WAV output.
- ACARS reassembly and binary ARINC622 ADS-C basic reports with application CRC,
  signed coordinates, altitude and time. Unknown/truncated groups fail closed.
- Offline Natural Earth map in live and replay windows. Identity-matched voice
  green; other decoded aircraft white; AES, registration, callsign (when sent),
  last report time and altitude on hover. No invented heading or location.
- Bounded local logs and opt-in HTTPS numerical diagnostics. No aircraft IDs,
  positions, text, IQ or audio are automatically uploaded.

Tools > Inmarsat Aero: select receiver, listed or manual channel and explicit
decoder, speaker/record options, then Start. Stop restores the previous receiver.
Replay never tunes hardware. See [IQ testing guide](INMARSAT_IQ_TESTING.md).

## Important Limits

Automatic voice follow remains disabled pending validated end-of-call/security
and tuner-ownership tests. Manual C-channel receive uses the native chain.
Dual-device simultaneous data/voice reception is not implemented.
No encrypted service decryption is supplied. Do not interpret a vocoder frame
or green marker as proof of authorization, clear speech, or who is speaking.

Position reports and voice need not be on the same channel or link direction.
Aircraft-originated ADS-C generally requires appropriate C-band/return-link
reception or relayed data. A good L-band voice recording may contain no positions.
Map reports are last-known decoded positions, not navigation-grade live tracks.
Aircraft photos, extrapolation and external identity lookup are not implemented.
EGC/STD-C still has only physical diagnostics, not a completed protocol decoder.

## Evidence And Qualification

Independent JAERO 8400 reference: 163 valid signalling units, 1375 voice words,
220000 PCM samples. Actual GUI/CLI paced/fast runs produce identical WAV bytes.
Most sample words are codec silence markers; this does NOT qualify continuous
intelligible conversation. Independent JAERO ADS-C report gives -24.073448,
-165.084171 at 32000 ft; every tested corruption/truncation is rejected.
Public 10500 burst reference also produces real decoded positions for two
aircraft through the complete IQ path. GUI map rendering, PCM file format,
state isolation and silence rejection are tested.

Remaining acceptance: dad's IQ plus matching JAERO settings, transcript/audio
and known position events; establish clear speech and link-direction/identity
association on that SAME input before enabling automatic follow. Track ISS-0016.
Protocol/codec provenance: [vendored notes](../external/aero/README.sdr-town.md).
