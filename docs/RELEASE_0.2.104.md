# 0.2.104 - Antenna control and opt-in performance diagnostics

Portable experimental testing build, not a signed installer/updater release.

- Tools > Antenna Rotator & SWR: separate pointing, limits/park, meter and
  event tabs; saved settings; actual bearing feedback; explicit motion arming.
- Hamlib rotctld compatibility, with ten documented controller targets and
  access to additional Hamlib backends. Hamlib must be installed/configured
  separately. No direct serial driver or automatic TLE motor tracking yet.
- Configurable soft travel limits, stale-feedback protection, bounded requests
  and prioritized Stop. Physical limit switches/emergency stop remain required.
- Read-only hardware SWR via rigctld, available only with valid transmitting
  hardware feedback. Never keys a transmitter; no SDR-derived pretend SWR.
- Help > Share Diagnostic Reports: saved consent, immediate queued-report
  discard on opt-out, official-build collector defaults and bounded delivery.
- CPU/memory/thread/handle measurements and per-stage Inmarsat timing/worker
  summaries. No change to P25 decoding, audio or device ownership.

Local build and 15/15 test groups pass. Protocol fixtures are not physical
rotator/SWR certification. See [antenna setup](ANTENNA_CONTROL.md).

Important: collector hardening is implemented/tested in source but production
restart was blocked. Its new server-side limits are not claimed live. Shared
distribution credentials do not prove report authenticity; per-install
enrollment/revocation and retention are still open. See
[diagnostics limitations](DIAGNOSTICS_SHARING.md). Client telemetry and this
rotator milestone can be tested independently of that deployment work.

10 MS/s realtime headroom, true independent multi-SDR ownership and satellite
field acceptance remain open. This release does not close those gates.
