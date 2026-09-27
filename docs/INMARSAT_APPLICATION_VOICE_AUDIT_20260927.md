# Aero application, identity and 8400 audit

T-0092 / DEC-0161 / DEC-0162. This is evidence for v0.2.116, not acceptance
of every satellite application or continuous intelligible speech.

## Data to map

1. Shared IQ channelizer produces 48 kHz real IF at 8 kHz. Native JAERO-derived
   modem performs timing/carrier recovery, soft decisions, framing and FEC.
2. CRC-valid signal units pass through ISU/ACARS reassembly. Raw ACARS is retained.
3. ACARS block ID determines application direction using libacars' convention:
   digits are aircraft-originated, letters ground-originated. The ten-character
   downlink message-number/flight header and H1 sublabel are removed only with
   the corresponding protocol evidence, not by searching arbitrary prose.
4. Pinned libacars dispatches ADS-C, FANS CPDLC, MIAM, media advisory and OHMA.
   Length/encoding guards precede parsing. CRC/parser error status is separate
   from RF CRC. Unknown airline content remains explicitly uninterpreted.
5. Only valid aircraft-originated ADS-C basic reports provide map coordinates.
   Group 17 supplies explicit ICAO identity, group 12 flight identity. These can
   arrive without a position. AES/airframe mismatch is rejected. Neither a
   clearance waypoint nor an uplink reporting contract becomes a plane position.
6. Per-session replay stores and the live store merge identities independently
   of coordinate age. Map tooltip includes ICAO only when received. Identity
   updates never make old coordinates appear fresh. Voice uses matching AES,
   not a guessed registration, to highlight the plane.

Submitted 10500 recording 30409b9e contains a MIAM CORE ACK, not prose. Other
two five-second clips lack complete cold-start ACARS messages. Public JAERO
10500 burst recording still yields eight CRC-valid units, eight messages, five
interpreted applications and two real positions, no application failures.
Neither position report supplies group 17, so ICAO stays blank. Tests exercise
ICAO-only messages, direction, malformed hex, CRC/truncation, CPDLC CONTACT text
and compressed OHMA JSON.

## T-0094 InmarScope Map Comparison

User confirms InmarScope plots aircraft while SDR Town does not. Checked the
clean reference checkout 26ae80af4bcfa4c86ed55f4383d1c95481d3450b and upstream
HEAD ea3602e92ba35a061140456013d60d436a40bd33: the sole intervening change is
README download text. The inspected decoder/map implementation is current.

| Path | InmarScope | SDR Town / consequence |
|---|---|---|
| Native position extraction | `acars_apps.cpp`: libacars ADS-C downlink basic groups 7/9/10/18/19/20 | Same groups; additionally checks ARINC CRC before accepting coordinates |
| Direction | JAERO `parserisu->downlink=burstmode`, forwarded by `fill_acars_msg` | ACARS block-ID direction per libacars; normal P-channel ACKs in submitted clips are ground-to-air |
| Internet map input | `flight_map_webview.h`: `onlinePositions=true`; `/v2/hex/...` at api.adsb.lol, batches of 100, every 20 s | No online enrichment in Inmarsat map; separate Aircraft Map internet control is not connected to it |
| Source identity | `onAcars2` initially copies AES hex to ICAO, then may replace with ADS-C group 17 | Only explicit group 17 populates ICAO; do not silently equate distinct identity namespaces |
| Display | `flight_map_data.cpp`: decoded position first, otherwise fresh online coordinate, `positionSource` label | Native validated ADS-C only; empty map is expected without accepted coordinates |

InmarScope README calls decoded markers blue and online markers orange. Its
normal non-burst 10500 path does not convert an ACK or ground-service message
to a native position. Historical decoded positions can also remain in its table.
Thus a populated InmarScope map alone is not evidence that the same 10500 bytes
carry aircraft coordinates. This is a confirmed feature difference, not proof
of which source the remote tester's individual markers used.

The submitted cfef9adc/cd0e5a9c/570087e1 clips recover eight complete messages
in cold replay: seven ACKs and one A4 ground-service message; all block IDs
ground-to-air, zero ADS-C or application/identity failures. No changes to
modem/FEC, CRC or direction gates are justified by these recovered payloads.
10500 can carry position-bearing applications; do not infer otherwise from
three five-second clips. Raw airline position text, flight plans and clearance
waypoints are not interchangeable with a current aircraft position.

Next feature (T-0095): explicitly opt-in online enrichment of received aircraft,
separate source/age/identity provenance, decoded coordinates taking priority,
bounded asynchronous requests and cancellation on disable. Reuse the existing
internet-aircraft policy; never upload message contents or treat guessed AES as
confirmed ICAO. Validate identity association against explicit ADS-C identity or
independent matching evidence before attaching online positions. Turning off
internet must remove only the online layer and preserve RF records. Offline RF
decoding remains independent. Not included or silently enabled in 0.2.117.

Primary source links:
[InmarScope lookup](https://github.com/SarahRoseLives/InmarScope/blob/26ae80af4bcfa4c86ed55f4383d1c95481d3450b/src/web/flight_map_webview.cpp),
[source selection](https://github.com/SarahRoseLives/InmarScope/blob/26ae80af4bcfa4c86ed55f4383d1c95481d3450b/src/web/flight_map_data.cpp),
[native parser](https://github.com/SarahRoseLives/InmarScope/blob/26ae80af4bcfa4c86ed55f4383d1c95481d3450b/src/decode/acars_apps.cpp).

## 8400 Voice Evidence

| Stage | Implementation / evidence |
|---|---|
| Input | Live/replay share InmarsatPipeline; gaps reset modem/channelizer and discard queued playback |
| Modem | OQPSK 8400 bit/s, 48 kHz input, 8 kHz IF, JAERO-derived framing |
| C frame | DecodeC deinterleaves/FEC/descrambles signalling; 25 96-bit words per 500 ms frame |
| Release | Current C-frame signalling CRC required; 300-byte transaction or reject/reset |
| Codec | Isolated Aero AMBE4800x3600, not P25; persistent stream state, 160 samples/word at 8 kHz |
| Identity | Validated changes reset codec and flush queued audio; unknown speech not assigned a plane |
| Watch | Isolated channel states, drain barrier, only selected focus reaches speaker; switch flushes queue |
| Output | Power-of-two 32768-sample ring; 8 kHz mono miniaudio; first complete 4000-sample frame queued before start |
| Diagnostics | Codec attempts/errors/repeats/mutes, CRC, PCM RMS/peak, queued/dropped/consumed/zero-fill |

`verify_aero_reference.py` on the independent 87.0735-second JAERO reference:
CLI/GUI fast and paced all give 163 valid signalling units, 1375 voice words,
220000 PCM samples, 11 CRC failures, three rejected C frames, 106 corrections,
eight repeats and 1244 muted/tone/erasure words. All four WAV SHA256 values:
`295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f`.
No vocoder/cadence/CRC-release change was made in this pass.

Additional real-time GUI run enabled the default miniaudio output: 220000 PCM
received, 216160 consumed, queue empty at completion, zero overflow drops and
zero device errors. One unknown-to-known source transition occurred; code
flushes queued old-source PCM on that transition. Zero-fill also includes idle
parts of the 87-second RF recording. These counters establish output delivery,
not continuous conversation or an acoustic measurement at physical speakers.

## Remaining gates

- T-0093 now adds a real submitted v0.2.116 8400 clip with five seconds of live
  PCM and coherent local speech recognition. All four shipped replay paths
  agree on its final three seconds after cold acquisition. No new input gaps,
  speaker drops/zero-fill or resets in nearby live telemetry. This establishes
  a useful working field example, not a quality score or universal acceptance.
  Longer speech and matched JAERO/InmarScope reference output remain needed.
- MIAM/OHMA multipart application reassembly remains unsupported. MIAM segments
  never enter the single-transfer parser or become completed messages. This
  is separate from existing ACARS transport reassembly.
- Airline-proprietary/encrypted applications cannot universally become prose.
  This release neither decrypts nor guesses their meaning.
- InmarScope uses a 9 kHz voice DDC/tighter AFC; SDR Town has a shared 6.5 kHz
  half-bandwidth channelizer and native modem settings. Adjacent-carrier
  rejection needs matched tests before altering filters; differences alone
  do not establish a defect.
- Assignment-driven automatic voice follow remains disabled pending call-end
  and ownership qualification. Saved watch-list cycling is separate.

References: libacars v2.2.1 README/API_REFERENCE/PROG_GUIDE example 3 and
acars.c/arinc.c/adsc.c; InmarScope acars_apps.cpp/decoder.cpp/ambe_decoder.cpp;
JAERO DecodeC and libaeroambe deinterleave tables. P25 remains frozen.
