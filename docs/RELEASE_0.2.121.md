# 0.2.121 - HF integrity and experimental Morse decoding

- New Tools > CW / Morse Decoder window: independent live analog observer,
  recording input, automatic/manual pitch and speed, bounded local text,
  copy/save and gap/reset diagnostics. MIT GGMorse backend.
- HF rejects invalid samples/rates/gain before they poison persistent state;
  explicit same-station NCO correction preserves stream history.
- HF pre-squelch tap now includes CW; unnecessary tap copies removed.
- Evidence-based receive-chain audit separates proven defects from outdated
  claims and records device/TX/WFM follow-up work without changing P25.

Live CW/hand-keying and fading AM/SSB remain field acceptance work. Ordinary CW
carriers need CW/SSB; FM Morse requires keyed audio. Select an explicit demod
before starting the live observer. No P25, FM DSP, driver, audio-engine, network
permission or updater-policy change. This is a portable experimental prerelease,
not a replacement for the last signed installer update.

Guides: [Morse](https://github.com/blkph0x/SDR_Town/blob/v0.2.121-experimental/docs/CW_RECEIVE.md),
[audit](https://github.com/blkph0x/SDR_Town/blob/v0.2.121-experimental/docs/RECEIVE_CHAIN_AUDIT_20261003.md).
