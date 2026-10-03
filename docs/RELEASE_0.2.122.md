# SDR Town 0.2.122 experimental

## DTMF burst analysis

- New **Tools > DTMF Analysis** window with conservative and 20 ms fast-burst
  profiles, live tuned/input snapshots, cancellable recording analysis and JSON
  evidence export. File analysis shares the live detector.
- Explicit known frequency inversion, pitch multiplier and frequency shift.
  Audio polarity reversal needs no switch. This is not decryption or automatic
  recovery of unknown scrambling.
- Correctly normalized tone energy, signed twist and competing-tone rejection;
  source-sample timing is independent of UI/block/replay speed.
- Repeater dual-watch processes every input block instead of silently skipping
  three in four. This costs more CPU; genuine IQ gaps still reset correctly.
- Thread-safe bounded event delivery, original-frequency sequence logging,
  explicit EOF completion, Unicode recording paths and measurable rejection/
  overflow/processing diagnostics. Decoded content is not automatically uploaded.
- CLI: `tones dtmf "capture.wav" [--fast] [--invert-hz 3300] [--scale 1.0]
  [--shift-hz 0]`.

## Testing and boundaries

Independent waveform and synthetic dual-channel NFM tests cover all 16 keys,
fast/repeated bursts, several rates, polarity, configured inversion and shifts,
noise/DC/clipping, timing, EOF, resets and GUI/file parity. New failures were
reproduced before repairs. P25, FM/HF demodulation, vocoder and speaker buffering
are unchanged; only opt-in NFM input-watch scheduling changes in orchestration.

Fast detection is experimental and more susceptible to speech false detections.
No telephone certification, universal speech talk-off or live handheld acceptance
is claimed. Use **Conservative** for ordinary keys. See
[DTMF guide](DTMF_ANALYSIS.md) for settings and known limitations.

Portable experimental tester asset, not a signed installer update. Existing
updater channels and diagnostics opt-in remain unchanged.
