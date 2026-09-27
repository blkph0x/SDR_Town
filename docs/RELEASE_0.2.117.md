# SDR Town 0.2.117 experimental

## Reviewed diagnostic recordings

- Allow 15 recordings per installation in a rolling 24-hour window, up from four.
- The app states the new limit and distinguishes collector limit rejection from
  other upload failures. Rejected recordings remain available for Save recording.
- Separate consent/review, authenticated HTTPS, maximum 1 MiB uploads, the
  request-rate guard, 128 MiB total storage and 30-day retention are unchanged.
- No automatic recording upload or retry is added. Ordinary telemetry remains
  counters-only. The hosted collector change also works with existing clients;
  older builds may still display the old allowance in the confirmation text.

## Inmarsat Map Evidence

- Existing local/opted-in reports and reviewed recordings now count complete
  aircraft-to-ground / ground-to-air messages, unknown direction, valid ADS-C
  applications and accepted position reports. Multi-channel workers stay separate.
- The map distinguishes received messages without validated ADS-C coordinates
  from a populated map. ACKs, contracts and ground clearances are not positions.
- Recording replay reports the same counters, explicitly marked cold start.
  Automatic telemetry still excludes message bodies, identities and coordinates.

The newly submitted 8400 sample contains five seconds of live decoded audio and
coherent local speech recognition. No clipping or observed playback starvation
in its window; brief zero intervals cannot be labelled speech loss from aggregate
codec flags. This release makes no voice/DSP, P25 or analogue decoder changes.
The three latest submitted 10500 recordings have no live signal-unit CRC failures;
cold replays recover ACKs and ground-service text, no ADS-C position report. They
do not prove a map renderer defect or that every message from that channel lacks
positions. Live application counters close this diagnostic blind spot without
changing RF acquisition, voice, application parsing or map acceptance rules.
InmarScope's separate online-position fallback is not included in this update;
the source comparison and planned source-labelled enrichment are documented in
[the audit](INMARSAT_APPLICATION_VOICE_AUDIT_20260927.md). A populated online map
must not be described as positions decoded from these radio samples.

Windows portable experimental package, not a signed installer/updater release.
