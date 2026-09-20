# Inmarsat (honest scope)

Experimental prototype in SDR Town. Not RF-qualified. Not a replacement for
InmarScope / professional Aero decoders.

## What this release does

- Public band-plan JSON (`data/inmarsat/*.json`) for channel pick lists.
- Tune the current device to a listed L-band channel (tuner lease).
- Chronological IQ cursor into a clean-room slicer (MSK/OQPSK/BPSK energy).
- Parse **ASCII** ACARS-like text and ADS-C lat/lon **if those strings appear**
  in the byte stream (unit tests on fixtures, not live air).
- ADS-C positions can mark the aircraft map (`fromAdsc`).
- GUI / CLI / FUBAR: start/stop, band plan, channel, message log.

## What it does **not** do (remaining gaps)

| Gap | Why it is still open |
|-----|----------------------|
| Unique-word / frame sync | No published UW searcher with a named air-interface vector. |
| FEC | Aero OQPSK / STD-C FEC not implemented; slicer bits are not coded frames. |
| C-assign / P-channel | Fixture regex only; must not retune RF. |
| Aero AMBE 8400 | mbelib pack is not a verified Aero mapping; voice follow is **disabled**. |
| Dual-SDR voice radio | Not built. |
| Live RF acceptance | No independent off-air decode compared to a known decoder. |

`locked` in status is always false. `experimental: true`, `rfQualified: false`.

Voice follow and record-voice checkboxes are disabled. Do not invent FEC or
AMBE mapping to close these rows.
