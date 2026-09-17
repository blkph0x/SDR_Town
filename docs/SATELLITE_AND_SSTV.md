# SSTV and Public Satellite Reception

Status: approved roadmap, not implemented capability. DEC-0083, 2026-09-17.

## Delivery Order

1. Finish DCS symbol recovery, polarity/alias handling and replay validation.
   The user's independent CTCSS radio check is deferred, not a coding blocker.
2. Implement shared decoder input/output contracts and a capability registry.
   Prove one existing RDS adapter equivalent before migrating any working path.
3. SSTV receive: recorded-audio decoding first, then live HF/FM integration.
4. AX.25/APRS packet decoding, then public amateur satellite telemetry.
5. Satellite catalogue, pass planning and sample-clock Doppler correction;
   validate recorded weather downlinks before scheduling live passes.
6. Weather imagery: Meteor LRPT first; legacy NOAA APT replay alongside it.
7. Expand L/S/X-band and geostationary public downlinks individually as receiver,
   antenna, public specifications, reference recordings and coverage permit.

DMR conventional, NXDN, AIS, ADS-B, ACARS, CW and other previously queued
decoders remain on the main roadmap. Do not silently mark them superseded.
Do not change P25 DSP as a side effect of adding these workspaces.

## SSTV Scope

Initial receive targets: Robot 36, Martin M1/M2, Scottie S1/S2 and PD120.
Subsequent candidates: other Robot/PD modes, Scottie DX and remaining modes
supported by the selected reference backend. Modes become available one by one
after fixture validation, not merely because they appear in a drop-down.

- Automatic VIS acquisition/validation, with explicit manual mode override.
- Line sync, slant/sample-clock correction and recovery after missed lines.
- Progressive image preview, completed-image gallery and PNG export.
- Per-image UTC, receiver frequency, selected mode, source and quality metadata.
- Audio-file replay and live demodulated-audio input using the same decoder.
- Preserve raw decoded image separately from optional visual enhancement.
- ISS SSTV reception when actually scheduled; no promise of always-on service.
- HF weather fax is a separate later decoder, not an SSTV mode.

Data feeds branch before speech noise reduction, EQ, notch and squelch wherever
those can destroy signalling. Speaker volume/mute must not change decoder input.
Evaluate QSSTV source and license before backend selection; do not port timing
constants from memory. Native implementation requires exact mode references and
independent images/audio fixtures, not only self-generated round-trip tests.

## Satellite Coverage Targets

| Family | First deliverable | Required distinctions |
|---|---|---|
| Public amateur/CubeSat telemetry | Validated packet frames, then telemetry fields | Public protocol and per-satellite decoder; not all telemetry is AX.25 |
| Amateur FM/SSB/CW satellites | Pass-assisted receive and Doppler tracking | Voice reception is not telemetry decoding; no uplink control |
| ISS amateur services | SSTV and AX.25/APRS where operational | Check operator mode/schedule before offering an active target |
| Meteor-M LRPT | Image channels and integrity/lock diagnostics | Validate each satellite's current transmitter and exact supported mode |
| Legacy NOAA POES APT | Archived IQ/audio replay, imagery and metadata | NOAA-15/18/19 are retired; do not schedule their old APT downlinks |
| Polar weather HRPT/AHRPT | Per-mission recorded decode, then live receive | Much higher hardware/bandwidth/antenna requirements than VHF LRPT |
| Geostationary weather | Public LRIT/HRIT/EMWIN or other verified products | Footprint, elevation, authorization and dish/downconverter requirements |
| Higher-rate scientific weather links | Selected public direct-broadcast products | Individual L/S/X-band hardware and sample-throughput qualification |

Satellite candidates include Meteor, Metop, Fengyun, GOES, GK-2A, Elektro-L and
JPSS families, plus public amateur missions supported by a reviewed backend.
This is a scope list, not a claim that every spacecraft/link is active, public,
visible from Australia, unencrypted, or supported by SDR Town today. HimawariCast
and other rebroadcast/distribution products must be identified as such rather
than described as direct reception of the imaging spacecraft.

Use SatDump as the first weather/imagery integration candidate and gr-satellites
as a public telemetry candidate. Their larger support lists are not automatically
SDR Town's supported list. An RTL-SDR cannot receive every listed downlink simply
because a decoder exists. Hardware suitability is checked per link.

## Catalogue and Pass Planner

Maintain versioned entries per satellite AND downlink: catalogue ID, operator,
public reference, verification date, operational state, protocol, frequency,
symbol/sample rates, required bandwidth, polarization, backend version and
test status. Unknown fields stay unknown. Show planned, replay-validated,
live-validated, unavailable-backend and retired separately.

Observer latitude/longitude/altitude is explicit and stored locally. Use a proven
SGP4 implementation with tested TLE/OMM ingestion; show orbital-data age, source,
AOS/LOS, maximum elevation, azimuth and predicted Doppler. Validate times in UTC,
coordinate conventions and frequency-correction sign against independent vectors.
Stale orbital data warns; a frequency prediction is not evidence of signal lock.

Use digital frequency correction within available IQ bandwidth where possible.
Hardware retunes create explicit sample epochs and may interrupt other receivers.
The scheduler must reserve device/bandwidth, resolve overlapping passes, and
never silently interrupt an active P25/analog receiver. Manual cancel always wins.
Display footprint/low-elevation and incompatible-hardware reasons before RX.

## GUI and Automation

Add dockable Images and Satellites workspaces using the existing layout system:

- Images: SSTV/weather preview, source selection, decoded gallery and export.
- Satellites: searchable catalogue, upcoming passes, map/sky view and RX queue.
- Details: supported mode, antenna/rate requirements, source age and decoder health.
- Waterfall: selected downlink, predicted Doppler track and occupied bandwidth;
  labels must use the same frequency transform as spectrum and tuning.

One decoder/session implementation serves CLI, GUI, replay and live paths.
Define automation commands only when the corresponding functionality is real.
Machine-readable results include backend version, input hash, sample gaps,
frame/FEC/CRC counts where applicable, output hashes, elapsed time and errors.
Image similarity alone cannot validate frame integrity; do not infer telemetry
correctness from a plausible picture. Avoid stuffing these controls into main.cpp.

## Resource and Security Contracts

Decoders own per-stream state on workers, not the Qt thread. Every input block
has sample rate, absolute sample index, epoch and frequency identity. Use bounded
queues with explicit overflow reporting; never silently drop data then concatenate
the remainder as a continuous stream. UI receives rate-limited snapshots/images.

Optional backend processes use validated executable paths, argument arrays (no
shell interpolation), capped logs, cancellation, output quotas and crash isolation.
Pin tested backend versions and retain required notices/source delivery materials.
Review distribution obligations before packaging; process isolation is not a
licensing exemption. Downloaded catalogue fields cannot select executables.

Captures have explicit duration/byte limits, disk preflight and visible retention
settings. Do not automatically upload IQ or user location. Large imagery is local
by default. Satellite modules are receive-only for documented public services.

## Acceptance Gates

Each decoder/link needs an independently sourced reference recording and expected
frames/image, plus synthetic timing/noise tests. Compare against a pinned reference
decoder. Test arbitrary input partitions, missing samples, retunes, mode changes,
invalid headers, truncated/oversized inputs and cancellation. Corrupt packets must
not become trusted telemetry; missing image lines remain explicitly marked.

Require deterministic CLI/GUI replay equivalence, bounded memory/disk/queues,
throughput above the selected live sample rate, worker restart and multi-receiver
isolation. Then independently verify live reception with suitable hardware.
Backend absence or poor RF is an explicit result, not a fabricated success.

## Reference Sources

- QSSTV upstream: https://github.com/ON4QZ/QSSTV
- SatDump pipeline catalogue: https://docs.satdump.org/md_docs_2pages_2Pipelines.html
- SatDump CLI documentation: https://docs.satdump.org/
- SatDump protocol resources: https://docs.satdump.org/md_docs_2res_2Resources.html
- SatDump license: https://github.com/SatDump/SatDump/blob/master/LICENSE
- gr-satellites: https://gr-satellites.readthedocs.io/en/latest/
- Telemetry coverage: https://gr-satellites.readthedocs.io/en/latest/supported_satellites.html
- NOAA mission status: https://ospo.noaa.gov/operations/poes/status.html

Reviewed 2026-09-17. NOAA status was available through the search index; direct
fetch returned HTTP 403. Recheck operator status when building the live catalogue.
