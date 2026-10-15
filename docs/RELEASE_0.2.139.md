# 0.2.139: receive and companion hardening

Paired companion: FUBAR 1.1.52. Experimental field-test release.
No binary assets were published for the initial 1.1.51 companion candidate:
its hosted browser test exposed a refresh-ordering race, repaired and re-tested
in 1.1.52. Its source tag is retained.

## Repairs

- Fence cancelled radio opens, reads and tune acknowledgements by their stream
  generation. Keep native handles alive until an outstanding read returns;
  quarantine unsafe reopen attempts. Reject all negative activation results.
- Report failed native Listen/scan starts instead of announcing reception.
- Exclude emergency audio trimming from concurrent callback reads, without
  adding blocking waits to the audio callback.
- Reject non-finite FM input/configuration and reset only the affected stream
  with an explicit discontinuity. Existing diagnostics include numeric fault
  counters, never samples or decoded content.
- Track aircraft position, altitude, velocity and identity freshness separately.
  Unknown altitude is unknown, not zero; unrelated messages cannot refresh an
  old position. Native popups and HTTP details use the same validity contract.
- Page large aircraft/Inmarsat snapshots without truncating the control DLL's
  JSON response. Snapshot tokens expire and bind to the requested route/session.
- Download updates asynchronously with bounded metadata, streaming SHA-256 and
  exact signed size, transfer deadlines, cancellation and temporary-file cleanup.
  Signature verification and explicit trusted release URLs remain mandatory.
- Honor explicit diagnostic consent for one replay run without enabling future
  automatic uploads. Recording consent remains separate; explicit disable wins.

## Companion use

Update FUBAR and its bundled SdrTownControl.dll together. Take control, choose
the desired radio/session and tune. Draft tuning values remain stable while
polling. Bounded server-issued control tickets and queue limits prevent
unbounded status growth; they do not authenticate a human or prevent one person
using multiple browsers. Existing lease duration/two-extension policy remains.
Aircraft and Inmarsat refreshes carry a shared request/session generation through
status, messages and map rendering, so stale successes or errors cannot replace
the newer view. Changing away from a session and back invalidates its old requests.

## Validation boundaries

See AUDIT_20261015_REPAIR_VALIDATION.md and BUILD_NOTES.md for executed tests.
No P25 demodulator, vocoder, slot/encryption gate or follow timer is changed.
Shared receive/audio repairs require the recorded regression gates; this is
not a claim that synthetic tests qualify every radio, satellite pass or RF
condition. Physical SDRplay reception still requires the official vendor
service and tester hardware. TX and physical rotator movement are not exercised
by the receive-only UI tests. A portable asset alone does not establish a
signed in-app update; publication evidence identifies which assets are verified.
