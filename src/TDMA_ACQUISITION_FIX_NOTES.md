# TDMA acquisition hardening

This pass addresses the case where clear Phase 2 grants are seen but the receiver never reaches `TDMASYNC=yes`, `SF=yes`, `XORMASK=applied`, or `ESS=clear`.

Changes made:

- Phase 2 voice-follow decoder instances are now created with a 6,000 symbols/sec clock. Phase 1/control-channel decode remains 4,800 symbols/sec.
- The P25 voice decoder target no longer uses the normal NFM AFC-corrected frequency during voice follow. TDMA acquisition stays centered on the granted voice frequency so the CQPSK channelizer does not chase adjacent/noise energy during retune.
- Phase 2 rolling IQ windows now retain up to 1.2 seconds and wait for at least 0.45 seconds before decoding, enough to contain a complete TDMA superframe and multiple sync opportunities.
- Auto-follow no longer refuses Phase 2 grants solely because the control-channel grant marked encryption. It can tune for diagnostics, but audio remains muted until MAC/ESS proves clear.
- Added explicit `TDMA ACQ armed` and periodic `TDMA ACQ check` log lines showing voice frequency, actual center frequency, passband status, lock-stage counters, and the 6000 Hz symbol-clock selection.

Expected log progression on a valid clear Phase 2 voice channel:

1. `TDMA ACQ armed ... cfgSymbolRate=6000Hz`
2. `TDMA ACQ check ... inPassband=yes ...`
3. `p2bursts` and `p2vcw` should appear first.
4. `sf` should become non-zero once superframe lock is established.
5. `mask` should become non-zero once XOR mask phase is found.
6. `mac` / `ess=clear` must be proven before audio is allowed through.

The white-noise blip gate remains in place. This package focuses on getting the acquisition attempt onto the correct symbol rate, frequency target, and buffer length while making failures diagnosable.
