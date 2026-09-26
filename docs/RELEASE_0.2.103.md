# SDR Town 0.2.103 - Inmarsat monitoring

Portable experimental testing release, not a signed installer/updater package.

- Inmarsat per-channel decoder status, bit rate, decoded message and CRC counts,
  IQ-gap counters and bounded transition history.
- Aircraft identity table with retained AES/ICAO, country, registration,
  callsign, position, altitude, independent ages and message counts.
- Sorting, position filter, copy/clear and independent pop-out windows.
- Combined data/voice monitoring no longer incorrectly displays data-only
  speaker status. Decoder/audio policy is unchanged; P25 is untouched.

See [monitor guide](INMARSAT_MONITOR.md) for field provenance and limitations.
Unknown identity is left blank. Multi-device ownership and 10 MS/s CPU headroom
remain open work. Physical satellite and SDRplay acceptance require testers.

Install the official SDRplay API 3.15+ and service separately, then extract the
whole ZIP into a fresh folder. The matched SDRplay plugin is included. This
portable release does not replace the signed installer used by the updater.
