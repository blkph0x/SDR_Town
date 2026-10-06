# SDR Town 0.2.129 experimental

## P25 locked-lattice empty-hop search

Capture `20261006_093930_588` on v0.2.128 is gapless and listen=CLEAR, but
mid-call dips (example duty 0.439) sit on empty hops that still have the
Phase 2 lattice locked. Those empties were treated as eye-lost, so the next
unique-speech hop ran cand=16 / 120 ms and kept the single-flight worker busy.

v0.2.129 keeps the first locked-lattice empty hop on healthy 80 ms / cand=4.
A second consecutive empty, or a hop with no structure at all, still escalates
to cand=16 / 120 ms so true re-lock is preserved. Hop size, overlap, slot and
encryption gates are unchanged. Missing VCWs are not synthesized.

Local Release `SDR_Town` / `sdr_town_tests` (528 passed, 2 skipped), Catch
planner cases, and 081738 TG10120 `PASS_CONTINUOUS_AUDIO` duty=0.71 / 710/710
AMBE hold; wrong-slot and encrypted replays stay muted.

This remains an experimental tester build. Live re-prove of the 093930-class
mid-call dip is required before closing the continuous-audio product gate.
