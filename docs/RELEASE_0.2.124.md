# SDR Town0.2.124 experimental

## Aircraft and Inmarsat usability

- Inmarsat aircraft icons follow validated ground track. Unknown direction
  uses a neutral marker instead of incorrectly pointing north. Validated
  received voice activity stays green; position source/age remain explicit.
- ADS-B direction validity is separate from position validity. Geometric-
  altitude position reports (TC20-22) now reach the existing CPR decoder.
- ADS-B now labels the request as Sample rate (MS/s), defaults new settings
  to2.4 MS/s and reports the actual device-applied rate after startup. Existing
  saved wide-rate requests and the web API remain compatible.
- Inmarsat Watch has Browse outside capture and an explicit MHz view center.
  Plan1529.000-1530.000 MHz watches without retuning by panning. Unreceived RF
  is blank; click-to-add still uses the selected decoder rate. The existing
  scheduler retunes between distant voice/data groups, or runs both in-band.

## Verification and limits

Regression fixtures cover ground-track validity/rendering, off-capture clicks,
blank RF areas, distant watch-group idle return, and ADS-B CPR dispatch.
ACARS still requires validated direction/application/identity evidence before
marking a position. A10500 channel is not a guarantee of received coordinates.
Physical L-band/SDRplay acceptance remains with testers. This is not a claim
of full dump1090 parity; see AIRCRAFT_WORKFLOW_AUDIT_20261005.md.

P25 decoder, following, RF processing and audio timing are unchanged.
Portable experimental release; does not replace the signed installer updater.
Original source remains MIT; combined RTL-enabled distribution is GPL3-or-later
with original third-party notices and rebuildable source materials retained.
