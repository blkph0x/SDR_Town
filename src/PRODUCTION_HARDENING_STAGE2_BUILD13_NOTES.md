# Production hardening stage 2 / buildfix13

Implemented next-stage audit fixes that fit inside the current .cpp package:

- Rate-limited GUI-thread classifier/AUTO bandwidth work to once every 500 ms instead of every 10 ms UI tick.
- Reduced P25 control-channel GUI decode cadence and IQ window size while leaving capture/voice diagnostics intact.
- Hardened AudioEngine SPSC ring semantics: producer no longer advances readPos; only the realtime callback owns readPos. On overload, new producer samples are dropped instead of corrupting the ring contract.
- Added translation-unit Soapy API serialization around native make/setup/set/read calls in DeviceManager.cpp to reduce driver races between readStream and live setFrequency/setGain/PPM calls.

Still left for a header/full-repo pass:

- Move P25 control decode completely off the GUI timer into a worker-owned queue.
- Promote Soapy serialization/lifecycle ownership into StreamState rather than a file-local global mutex.
- Add formal unit/regression tests for Phase 2 MAC/ESS release gating and AUTO parity.
