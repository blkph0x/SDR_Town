# SDR Town 0.2.128 experimental

## P25 residual clear-audio cadence repair

Capture `20261006_081738_771` confirms the v0.2.127 rollback restored clear
Phase 2 audio and retained gapless IQ. It also isolates the smaller remaining
speaker holes to active-clear decode cadence: 160 ms fresh windows underproduce
real PCM even though worker queues, result publication, device IQ, output
producers, slot isolation and security gates are healthy.

v0.2.128 keeps the safe 240 ms maximum and 280 ms context, but waits for a
measured 200 ms of fresh IQ on the active-clear backlog path. Non-active
acquisition remains at 160 ms.

Same-IQ qualification across six clear calls:

- all six preserve or increase speaker PCM versus 160 ms
- aggregate speaker PCM increases from 35.38 s to 37.00 s
- speaker timeline drops decrease from three to zero
- wrong-slot and encrypted replays remain muted
- no software-generated repeated non-silent frame was found

Local Release build and all 17 CTest suites pass. This remains an experimental
tester build. A new live multi-call capture is required before declaring the
continuous-audio product gate closed.
