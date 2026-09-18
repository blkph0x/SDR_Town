# SDR Town 0.2.55

P25 Phase 2 audio and synchronization fixes for continued field testing.

- Correct slot-session ownership when resolving absolute TDMA burst position.
- Reject nonphysical Phase 2 quadrant mappings that could corrupt overlapping
  decodes of the same RF data.
- Decode block tails from a current-window anchor without duplicating bursts.
- Drain short GUI replay audio tails at end of file.
- Fix an audio-buffer cursor race between playback and clear/discard operations.
- Add callback consumption, silence and drop counters to capture diagnostics,
  plus optional complete validation tracing for offline investigation.

Validation before packaging: 235 test cases / 81,154 assertions passed;
90-second live GUI capture completed three follows without IQ overruns or
producer audio drops. A reference replay remained byte-identical after the
downstream buffer repair. One output span ran 7.1 seconds without an underrun
increase. These are measured checks, not a claim of perfect all-call audio.

Experimental update channel. Clear continuous speech across all calls remains
under validation; please include capture logs with any remaining audio issues.

Assets: Windows x64 installer, portable ZIP, reusable SdrTownControl DLL,
SHA-256 checksums, and signed in-app update metadata.
