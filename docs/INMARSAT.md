# Inmarsat (honest scope)

Experimental prototype in SDR Town. Not RF-qualified. Not a replacement for
InmarScope / professional Aero decoders.

## What this release does

- Public band-plan JSON (`data/inmarsat/*.json`) for channel pick lists.
- Tune the current device to a listed L-band channel (tuner lease).
- Chronological IQ cursor into a clean-room slicer (MSK/OQPSK/BPSK energy).
- ASCII ACARS/ADS-C fixture parsers exist but raw live slicer bytes are blocked
  from these parsers and the aircraft map. They are not over-the-air decoders.
- IQ file replay with sample-clock pause/seek/time and shared live/replay probe.
- Bounded local logs and optional authenticated HTTPS numerical diagnostics.
- GUI / CLI / FUBAR: start/stop, band plan, channel, message log.

## What it does **not** do (remaining gaps)

| Gap | Why it is still open |
|-----|----------------------|
| Unique-word / frame sync | No published UW searcher with a named air-interface vector. |
| FEC | Aero OQPSK / STD-C FEC not implemented; slicer bits are not coded frames. |
| C-assign / P-channel | Fixture regex only; must not retune RF. |
| Aero AMBE 8400 | Old incorrect wrapper disabled; mini-m AMBE4800x3600 is a separate codec, not P25 mbelib. |
| Dual-SDR voice radio | Not built. |
| Live RF acceptance | No independent off-air decode compared to a known decoder. |

`locked` in status is always false. `experimental: true`, `rfQualified: false`.

Voice follow and record-voice checkboxes are disabled. Do not invent FEC or
AMBE mapping to close these rows.

See [IQ formats, GUI/CLI testing and diagnostics](INMARSAT_IQ_TESTING.md).

## Next Acceptance Gates

1. Reference JAERO MSK/OQPSK timing, bit rate vs symbol rate, unique-word framing,
   differential decoding, deinterleave, convolutional FEC and CRC on the SAME IQ.
2. Parse binary CRC-valid P-channel assignments with AES/GES identity and derived
   frequencies; never retune from ASCII regexes or guessed frequency tables.
3. Add validated C-channel framing and the Aero mini-m AMBE4800x3600 interleave,
   keeping codec symbols/state isolated from P25. References:
   [JAERO AeroL](https://github.com/jontio/JAERO/blob/master/JAERO/aerol.cpp) and
   [libaeroambe](https://github.com/jontio/libaeroambe/blob/master/libaeroambe/aeroambe.cpp).
4. Prove real clear PCM against JAERO; then wire sample-clocked audio and automatic
   call follow with explicit security, tuner ownership and call-boundary handling.
5. Bind call activity to validated aircraft identity and a fresh verified position.
   Green must mean verified active call, not just local ADS-B reception. Aircraft
   call association is not proof the pilot is speaking: link direction matters.
   Fetch real photo metadata/images with credits, limits and caching; the current
   map's API JSON URL is not an image. These map enhancements are not implemented.
