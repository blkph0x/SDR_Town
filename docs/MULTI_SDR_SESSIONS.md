# Multi-SDR receive sessions

DEC-0135. User requirement: multiple SDRs, independently assigned to modes,
including simultaneous P25, Inmarsat data and Inmarsat voice. This document is
an audited design and acceptance plan, not a claim of implemented support.

## Confirmed constraints

- DeviceManager.h/.cpp: deviceLeaseOwner_/deviceLeaseIndex_ are global.
  releaseDeviceLease(owner) has no device/session generation. retuneWithLease
  acquires then queues a tune separately. A device-index map alone is insufficient.
- MainWindow.cpp SatcomMainWindowSessionState: one active takeover and restore
  list. endReceiverTakeover has no device/session argument; delayed cleanup can
  affect the wrong future session unless ownership has generation identity.
- SatcomHostServices: end/publish callbacks lack source identity. Per-device
  restore and selected-view publication are necessary before concurrent sessions.
- InmarsatEngine: one singleton configuration, Receiver cursor, worker, pipeline,
  watch scheduler, audio and display. Two roles require separate worker contexts.
- Aircraft GUI/CLI paths and Satcom hub also acquire/control shared devices;
  all call sites must migrate, not just Inmarsat.
- P25/main Listen paths use shared orchestration beyond lease calls. Audit raw
  tune/start/stop paths and pending tune completion before enabling concurrency.

## Ownership and assignment contract

Persist role -> stable hardware key (driver/serial/channel), not enumeration
index. Resolve at start; unavailable or ambiguous assignment fails explicitly,
never falls back to another SDR. Roles include Listen/VFO, P25 control/traffic,
Inmarsat data, Inmarsat voice, Satcom and aircraft reception.

Each radio source has one RF owner with a session ID and generation. Acquire,
tune authorization and release use that token. An old stop/tune callback cannot
control a replacement session. Force is an explicit coordinated handover on the
same device only. Remove priority-based implicit stealing. Group acquisition
must rollback only newly acquired resources on partial failure.

One hardware stream fans out bounded chronological IQ to software channels.
Each channel retains its own demodulator, frame state and identity. Sharing a
source is allowed only under the source owner's tuning contract; no follower
can retune it independently. Device loss faults only its consumers. End-of-IQ,
overload, discontinuity and session generation are logged per source/channel.

## Inmarsat scheduling

One SDR: group selected data channels by actual sampled passband and CPU budget;
decode concurrently. Early voice switch requires minimum dwell, distinct valid
ADS-C positions, and fresh CRC-valid traffic on a majority of current data
channels. A configurable deadline exits incomplete collection transparently.
Visit every data group, then voice groups. At silence or refresh deadline revisit
data; bounded maximum voice dwell prevents permanent stale positions. Do not
interrupt an active conversation merely because another voice worker becomes
active. Keep the map through retunes and mark stale reports honestly.

Two assigned SDRs: data and voice sessions run continuously and independently;
do not run the single-radio alternating scheduler across both. Each source may
still need to rotate its own groups if passband or processing budget is exceeded.
If both bands fit one source, a later mixed-role grouping optimization may avoid
retuning, but must preserve separate channel/identity/audio state.

Aircraft identity: correlate validated Aero AES identity with validated ADS-C
reports. The current C-channel wrapper extracts AES from validated signalling;
map matching is exact AES, not nearest aircraft. Audit call start/end and lost
identity before extending caches. Missing identity must show unknown, not reuse
a previous caller. ACARS registration/callsign and ADS-C position are separate
fields; not every transmission supplies every field. JAERO upstream reference:
https://github.com/jontio/JAERO (aerol.cpp and C-channel signalling).

## Delivery gates

1. Isolated Aero capacity/readiness change: measure 2/4/8/16 workers, preserve
   saved settings, bounds and speaker isolation. Do not call CPU load an RF test.
2. Per-device token registry: concurrent acquire/release, wrong owner, stale
   generation, disconnect/reconnect, failed multi-acquire and queued tune tests.
3. Migrate host/UI/CLI consumers and raw tuning paths. Start/stop one radio while
   another decodes; verify no IQ, audio or settings changes on the unaffected one.
4. Persist role assignments and expose device/owner/sample-rate/passband/overload
   status. Exercise changed enumeration order, missing hardware and same-SDR
   conflicts. No automatic hardware fallback.
5. Split Inmarsat source contexts and shared message/identity store; test data
   and voice concurrently plus a separate P25 replay/live session. One role's
   reconfiguration must not discard another's PCM or restore its tuner.
6. Public CI builds plus multi-device tester captures: per-source generation,
   source cursor/gaps, processing load, valid frames, identity provenance, fresh
   positions and output continuity. Real multi-radio acceptance is still required.

Keep P25 demod/vocoder algorithms frozen. Shared-source ownership changes need
explicit reviewed guard coverage and regression evidence, not removal of guards.
