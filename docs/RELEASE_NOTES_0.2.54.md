# SDR Town 0.2.54 (experimental)

This is a remote-testing build. P25 Phase 2 audio continuity is still under
investigation; this release does not claim complete clear-voice reception.

## Changes

- Speed up Phase 2 Reed-Solomon recovery using exhaustively verified GF64
  tables and cached syndrome columns. No FEC, CRC, slot or encryption criteria
  were weakened.
- Keep speaker ordinals continuous across talkspurt resets within one call;
  apply MAC boundaries in chronological order.
- Preserve and drain approved pending PCM without repeatedly priming an
  already-playing stream. Stop inserting speculative silence behind speech.
- Reject implausible recovered channel identifiers before updating live tables.
- Add capture/speaker-frame diagnostics and regression coverage.
- Package runtime files only, excluding local captures, test logs and old
  baseline executables. Includes the SDR Town control DLL.

## Verification and limits

- P25 tests: 68,957 assertions in 122 cases passed.
- Full suite: 77,061 assertions in 229 cases passed.
- An eight-second IQ replay improved from 11.27 to 7.56 seconds processing time.
- An earlier reference replay improved from 19.69 to 11.63 seconds, with a
  byte-identical output WAV.
- GUI replay matches CLI sample counts. Live RTL-SDR capture followed grants
  and emitted PCM, but underruns and feed gaps remain.
- Intermittent garble, background buzz and continuous-audio quality are not
  certified fixed. A recoverable native Soapy shutdown warning was observed.

Please include the capture, talkgroup/slot, radio ID if known, and whether the
problem occurred live or during replay when reporting results.
