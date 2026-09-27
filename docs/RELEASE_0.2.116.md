# SDR Town 0.2.116 experimental

## Inmarsat message interpretation

Replayed all three submitted five-second 48 kHz modem-input recordings. Their
live counters each show 260 additional CRC-valid signal units, no additional
CRC failures. One cold replay completes four empty ACARS acknowledgements and
one MA single-transfer MIAM acknowledgement. That encoded payload is protocol
data, not readable prose damaged by the radio decoder.

The message pane now interprets ACARS acknowledgements, ADS-C, FANS CPDLC,
media advisory, OHMA and MIAM CORE v1/v2 single-transfer messages using the
pinned libacars implementation, including
bounded raw-DEFLATE decompression and application CRC checks. Application status
is separate from radio-frame validation. Raw text is retained in message JSON;
the new applicationProtocol/applicationStatus/applicationText fields provide the
human-readable rendition. Safe numeric application counters join opted-in
diagnostics; automatic telemetry does not include message content.

ICAO/flight reports update identity even without a position in that message.
Only validated aircraft-originated basic reports create map positions; uplink
contracts and CPDLC clearance waypoints do not. Identity updates enrich existing
map labels without refreshing the original position age. Live and replay use
isolated stores with the same identity merge rules.

Limitations: five-second late-entry recordings may not contain a complete ACARS
message. Segmented MIAM/OHMA application reassembly is not enabled in this pass;
fragments are marked unsupported/incomplete, not complete decoded messages.
Unknown airline-specific formats are explicitly uninterpreted.
There is no speculative PLL/FEC/radio gain change, or new Inmarsat voice claim.

## 8400 voice qualification

Audited shared live/replay channelization, current-frame CRC, 96-bit Aero voice
mapping, persistent codec state, aircraft ownership and speaker queuing. GUI
and CLI, fast and real-time replay produce identical reference WAVs; the public
release executable reproduces that same file. Default-output GUI replay reports
no device errors or queue overflow. Much of this reference is codec silence, so
these checks do not establish clear continuous conversation on a tester's RF.
A matched 8400 speech recording is still needed; submitted clips were 10500 data.
See [the detailed audit](https://github.com/blkph0x/SDR_Town/blob/master/docs/INMARSAT_APPLICATION_VOICE_AUDIT_20260927.md).

## Aircraft Map

- Set **Capture bandwidth** (default 20 MHz), then **Tune 1090**. This selects
  capture sample rate from advertised hardware capabilities, independently of
  the main window's 500 kHz analogue audio-channel filter. It does not force a
  20 MHz rate on unsupported hardware. RSPdx typically offers 10 Msps capture
  with up to 8 MHz IF; RTL-SDR commonly offers 2.4 Msps. Hardware IF and sample
  rate are distinct; usable RF bandwidth is not guaranteed to equal sample rate.
- Setup applies IF/rate before starting and retains the existing tuner-ownership
  protection. An active P25/listen owner is not forcibly retuned. Rescan Devices
  first if the receiver has no advertised rates.
- Local 1090 uses chronological IQ and preserves partial 120 us packets across
  block boundaries. Gaps/epoch changes discard partial packets. Processing and
  gap counts are logged every 30 seconds; maximum-rate live throughput still
  needs hardware acceptance and is not established by synthetic fixtures.
- Uncheck **Internet aircraft** to stop aircraft requests and remove all their
  records/fields immediately. Local ADS-B/ADS-C remain. The choice persists;
  pending replies cannot restore records, even after an off/on cycle. OpenStreetMap
  background tiles are separate and remain available.
- Removed duplicate network refresh requests, corrected OpenSky squawk indexing,
  bounded untrusted responses and rejected malformed rows independently.

## Delivery and testing

Windows portable experimental build, not a signed installer/updater release.
Extract the complete ZIP into a fresh directory. The package includes zlib and
libacars licence notices. P25 and analogue demodulation implementations are
unchanged; publication is gated by the frozen-P25 guard and regression tests.
Hardware acceptance on RSPdx and a 20 Msps-capable receiver remains with testers.

References:
- [libacars v2.2.1 MIAM API](https://github.com/szpajder/libacars/blob/v2.2.1/doc/API_REFERENCE.md)
- [OpenSky state vector fields](https://openskynetwork.github.io/opensky-api/rest.html)
