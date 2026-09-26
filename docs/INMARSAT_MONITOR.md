# Inmarsat decoder and aircraft monitor

Tools > Inmarsat Aero now includes **Decoders** and **Aircraft** tabs. The
expand icon opens either view in its own window; closing it does not stop RX.

## Decoders

Each saved watch channel has a row, including channels outside the current
watch group. Manual tuning has one row. Columns show channel, MHz, bit/s,
protocol status, emitted decoded messages, CRC-valid units, CRC failures and
IQ discontinuities. These are different counters: CRC OK does not mean an
ACARS message or audible voice. Counts describe the current worker lifetime;
rotation/restart can replace the worker and reset counts.

Locked means the native modem's protocol DCD, not merely RF energy. Searching,
Acquiring, Not active, Disabled and Stopped are distinct. Stopped rows cannot
retain a displayed live lock. The timestamped state/rate/frequency history is
bounded to 500 lines; it does not flood on each counter increment. Clear clears
history only. Copy exports visible table rows as tab-separated text.

## Aircraft

AES-keyed rows show ICAO, country allocation, registration, flight/callsign,
latitude, longitude, altitude in feet, last-message age, message count,
position age and last observed message frequency. Only validated decoded
ACARS, supported signal-unit and assignment messages enter the registry.
Message count counts observations, not deduplicated RF retransmissions.

Identity and position survive rolling message-log eviction. Empty newer
fields do not erase known identity; older messages do not overwrite newer
identity. A newer message does not refresh an old position's age. The registry
retains at most 256 aircraft, evicting the oldest last-message timestamp.
It is session-local, not a permanent aircraft database. Clear removes tracked
aircraft and map positions, but not message history or decoder counters.

Use **With position only** to filter and column headers to sort. Copy copies
only visible rows. Radio reception continues while either window is closed.
No aircraft identities or positions are added to automatic remote diagnostics.

ICAO is currently populated only from an explicit CRC-validated ADS-C airframe
ID that agrees with the decoded AES. Other rows can legitimately have blank
ICAO/country. Registration and callsign must be received; no online lookup or
guess from a nearby aircraft is performed. Country is an address allocation,
not current location or operator nationality. This table is not a navigation
or safety-of-flight product.

## Country data provenance

`include/InmarsatIcaoCountry.h` uses range facts from the CC0-1.0 dataset
[ibosoftnet/icao-aircraft-addresses](https://github.com/ibosoftnet/icao-aircraft-addresses/tree/2ac0f294274beddb57212eb7531ff87dfd869de5),
`Hexadecimal Addresses (Amendment 92).csv`, a transcription of ICAO Annex 10
Volume III, Appendix to Chapter 9, Table 9-1. Upstream's
[CC0 dedication](https://github.com/ibosoftnet/icao-aircraft-addresses/blob/2ac0f294274beddb57212eb7531ff87dfd869de5/LICENSE)
applies to that dataset. Full allocation names are retained. Special ICAO
temporary/safety blocks and unknown/unassigned ranges have no country label.
The screenshot's exact country abbreviations are not copied from InmarScope.

## Remaining gaps

- No general aircraft-registration database or separate aircraft-ID-only
  ADS-C report parsing; unknown fields remain blank.
- This is live monitoring. IQ replay retains its own separate report/map;
  replay observations do not contaminate live aircraft ages or counts.
- Multi-SDR independent mode ownership remains ISS-0031 / T-0062.
- 10 MS/s realtime headroom remains ISS-0032; do not mask overload with buffers.
- Native satellite reception, SDRplay RF/bias-T and RTL electrical behavior
  still need tester hardware acceptance. UI/unit tests are not RF proof.
