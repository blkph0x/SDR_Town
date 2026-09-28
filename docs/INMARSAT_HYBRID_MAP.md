# Inmarsat Hybrid Aircraft Map

T-0095 / DEC-0164. Experimental receive visualization, not navigation equipment.
The modem, FEC, voice codec and P25 are unchanged.

## Use

Open Tools > Inmarsat Aero > Aircraft map after starting the desired saved watch
channels or a single channel. RF positions work offline. To supplement them,
enable **Online positions (ADSB.lol)** and accept the separate privacy prompt.
That preference is remembered. Disabling immediately cancels the request and
removes online positions; RF observations are preserved. A hidden map suspends
requests. Replay never makes lookups or changes the live aircraft store.

- White: decoded RF ADS-C position. Blue: ADSB.lol position, not satellite GPS.
- Green: aircraft-associated received voice activity on an observed channel.
  Hover identifies the selected speaker source separately. This is not proof
  that the pilot is speaking: a ground-to-air call also has an aircraft address.
- Yellow outline: optional short estimate, never a new measurement.
- Gray: stale position. The hover text gives the source, age and measured anchor.
- Unlocated aircraft stay in the aircraft registry; the map/status explains why
  no coordinate is available. No address-only marker is placed at (0,0).

The trash icon clears aircraft/position history. New RF messages may populate
it again. A decoded call with no address is explicitly unidentified, never
joined to the nearest plane or assigned another call's identity.

## Identity And Position Evidence

Classic Aero AES ID is the 24-bit ICAO aircraft address (ICAO Doc 9925 Part III
6.3.1; AMCP signal-unit Appendix C section 3; Cobham 98-124743-G table 5-65).
This corrects the earlier implementation requiring ADS-C group 17. The native
CRC-validated ACARS/assignment/8400 call-identity paths supply this association.
Zero, all-ones and out-of-range addresses are not queried. Explicit ADS-C
airframe conflicts still reject the position; CRC-valid ACKs supply identity,
not coordinates. Internet lookup is allowed only for received Classic Aero IDs.

Live RF snapshots use the existing mutex-owned store and monotonic receipt ages.
The recent identity and independent position registries remain separate; one
locked union (at most 512 aircraft) preserves retained positions under heavy
identity-only traffic without renewing their ages.
The GUI-owned tracking model and Qt network callbacks share one thread; neither
holds a DSP lock during network access. RF and internet coordinates are never
merged into one mutable observation. Identifications do not renew coordinate age.

Identity and RF-coordinate retention are 20 minutes. This is display policy,
not a promise of L-band ADS-C every 10-15 minutes. An online fix is preferred
while <=60 seconds old and at least as recent as RF, otherwise RF is used if retained. An online-only last
fix may remain visibly stale for up to five minutes. Provider `now` is Unix
milliseconds, while `seen_pos` is seconds; their combined observation time owns
freshness. Repeated/older replies cannot renew an old fix.

Optional projection uses ground speed in knots and true ground track, not true
heading/Mach or a clearance waypoint. RF motion must be from the same accepted
ADS-C report (earth-reference tag 14); air-reference tag 15 is excluded. Estimates
advance in ten-second steps for at most 120 seconds from the observation, then
show the measured anchor again. The measured anchor/source/age remain visible.
This bounded spherical great-circle approximation makes no accuracy guarantee
through aircraft turns, acceleration or missing data. It does not change altitude.

## Network And Privacy

Only the received ICAO hex list and ordinary HTTPS metadata are disclosed to
ADSB.lol. No raw RF messages, IQ, speech or receiver coordinates are sent there.
ADSB.lol supplies the online position data and has dynamic service limits; a
position is not guaranteed over oceans or for absent/filtered aircraft.

One asynchronous request, max 100 IDs per 20 seconds, fair bounded batches,
10-second total deadline and 512 KiB response cap. No cookies, credentials,
redirect following or unbounded JSON nesting. Unexpected IDs, bad coordinates,
nonfinite values, old/future envelopes and stale fixes are rejected. Empty/404
responses retain only still-valid cached observations. Errors back off with
Retry-After respected; disable/window hide aborts callbacks and late replies
cannot repopulate the removed layer. API shape verified against public
https://api.adsb.lol/api/openapi.json and a no-aircraft response, not private IDs.

## Diagnostics

Existing `inmarsat_diagnostics/*.jsonl` logs now include a separate
`inmarsat-map` session. Local entries record the bounded current aircraft list,
position source, observation/receipt ages, estimate anchor and reasons such as
`no_position`, `identity_unverified`, `online_disabled`, `awaiting_position`,
`position_expired` and `stale_position`. No PCM/IQ is written by map diagnostics.

Opted-in remote diagnostics get numeric counters only: RF/online/estimated/
unlocated/stale aircraft, identified and unidentified activity, unavailable voice
positions, accepted/rejected/older API rows, request/failure/cancellation counts,
HTTP and Qt transport status, bytes, latency and retry delay. `lookupErrorCode`:
0 success, 1 oversized, 2 deadline, 3 rate-limited, 4 malformed/stale envelope,
5 transport/HTTP failure. Ordinary telemetry does NOT upload aircraft IDs,
coordinates, decoded text or provider URLs. Existing opt-in remains mandatory.
Reviewed five-second diagnostic recordings additionally preserve before/after
voice identity and activity counters; this is not automatic upload or permission
to initialize a live decoder from unvalidated metadata.

## Qualification And Remaining Limits

Gates include malformed/unsolicited/repeated/late replies, time boundaries,
source disable/fallback, cancellation/backoff/body/depth bounds, consent UI,
multi-aircraft colour rendering, reference IQ-to-ICAO/map and byte-identical
8400 PCM. Tests use synthetic identities and mocked API responses, not live
tracking of a tester's aircraft. A same-session field capture tying the decoded
call to the mapped aircraft remains necessary for physical installation acceptance.
Green indicates decode activity, not sample-exact sound-card playback. Automatic
watch still requires known identity before acquiring speaker focus; this pass
does not relax that existing audio policy. No new assignment-follow algorithm,
photograph service or navigation-grade prediction is claimed.
