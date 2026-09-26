# SDR Town 0.2.101 - Larger Aero watch groups

- Concurrent decoders now supports 1-16 rather than 1-4. Set the budget in
  Inmarsat Watch and use Save timing. Existing saved values and the conservative
  two-worker default are preserved. Up to 32 channels can be saved overall.
- Selected channels that fit the actual SDR passband and chosen worker budget
  run together, with independent decoder state. Data and voice groups still
  alternate on one receiver. Out-of-band channels require retuning.
- Early transition from data to voice now requires the distinct position target
  AND recent CRC-valid data from a strict majority of the current group. The UI
  reports validated data channel counts. Maximum dwell still permits a clearly
  reported partial collection so quiet channels cannot block voice forever.
- A synthetic 2.048 MS/s benchmark on the development PC processed 16 workers at
  about 0.51x RF duration. This is not proof of live multi-call audio performance;
  monitor processing load and IQ gaps and reduce concurrency on slower machines.
- Aircraft map positions now have a separate bounded latest-report cache, so
  high message volume cannot evict positions just by filling the 500-message
  log. Only validated, correctly bounded coordinates and AES identities enter
  it; older updates cannot replace newer reports. Stale positions remain marked.

Important: simultaneous multi-SDR role assignment is NOT implemented in this
release. The audited plan is docs/MULTI_SDR_SESSIONS.md. Global device ownership,
GUI takeover and Inmarsat source contexts must be migrated together before P25,
data and voice can safely own separate radios. No P25/audio implementation change.

Regular portable testing release; extract the full ZIP. Not a signed installer
or replacement for the existing in-app updater release. SDRplay requires the
official API 3.15+ and running service.
