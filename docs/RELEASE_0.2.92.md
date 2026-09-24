# 0.2.92 Experimental Aero Receive And Map

## Test

1. Tools > Inmarsat Aero > Open IQ replay. Load SigMF, stereo IQ WAV or explicitly
   configured raw IQ. Choose the actual channel and decoder, not a guessed band.
2. For mini-m voice select **Aero C-channel 8400**, enable real-time pacing and
   speaker audio. Optional WAV path must be new; output is 8 kHz mono.
3. For aircraft-originated data select **Aero R/T burst 1200/10500** as appropriate.
   CRC-valid binary ADS-C reports appear on the offline map. Hover for details;
   drag/zoom works without internet. No report means no invented aircraft.
4. Compare the same interval against JAERO. Retain IQ, channel/mode, expected
   speech/location and the session JSONL path shown in the window.

Live receive uses the same decoder: choose a device, a listed or manual channel,
decoder and speaker/record options, then Start. Stop restores the previous tune.
P25 and its audio engine/codec are unchanged. Replay never opens radio hardware.

## Proven Here

- Independent 8400 public sample: 163 valid SUs, 1375 voice words, 220000 PCM
  samples; four actual GUI/CLI paced/fast passes have identical WAV SHA256.
- Default speaker run: no queue drops or device errors. Silence/gap zero-fill is
  separately counted; it is not counted as decoded voice.
- Public burst recording decodes aircraft positions through IQ to the GUI map.
- Independent binary ADS-C coordinates/checksum, corruption/truncation rejection,
  codec thread isolation, sample continuity, WAV layout and GUI controls tested.

## Not Yet Qualified

The public voice sample is mostly silence markers; a nonempty WAV or brief STT
phrase is not proof of clear conversation. Dad's known-good reference is needed
for RF/speech acceptance. Automatic follow, call-end/security handling, dual-SDR,
photos, and EGC/STD-C protocol decoding remain unfinished and are not advertised
as working. Aircraft-originated positions may require C-band reception; voice
and position are not guaranteed to arrive on the same L-band carrier.

Aircraft markers show last decoded positions, not extrapolated navigation data.
Green means matching decoded voice identity, not proof that the pilot is speaking.
WAV concatenates accepted frames; lost frames are reported, never fabricated.

## Diagnostics

HTTPS collector remains https://gearsqueens.online/sdr-town-diag/ingest,
disabled by default in packages. Opt-in replay sharing sends numerical decoder
and audio counters only, through the existing bounded authenticated transport.
No aircraft IDs, positions, text, IQ, audio or paths are automatically sent.
Local logs include positions and source paths; review them before manual sharing.
See [full formats and CLI options](INMARSAT_IQ_TESTING.md).
