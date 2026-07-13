# Build 28 - P25 follow crash/hang guard

Purpose: harden the transition from P25 control-channel monitoring to voice-follow after a grant.

Changes:
- Serialized live Soapy read/retune/gain/PPM calls with a file-local mutex in DeviceManager.cpp.
  This targets RTL/USB hangs caused by setFrequency racing readStream during auto-follow retunes.
- Added an auto-follow transition guard so repeated/stale grants cannot re-enter the retune path while a previous follow is still settling.
- Drops queued P25 control-worker results before retuning to voice.
- Drops stale control-worker results that arrive after voice follow has started.

This does not weaken the P25 Phase 2 audio gate; it only makes the follow transition safer.
