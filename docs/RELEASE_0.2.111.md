# 0.2.111 - Inmarsat identity hardening (experimental)

- Live single-channel, watch and IQ replay now preserve AES on the audio path.
- Identity changes, including known-to-unknown, clear queued speaker audio.
  Empty PCM does not change identity; out-of-range AES is treated as unknown.
- Local audio diagnostics include current AES, source-change count, unknown
  sample count and the current source's first sample offset. Remote opt-in
  summaries include counts only, not aircraft identity or sample offset.
- Map status explicitly identifies active voice without an AES or without a
  matching ADS-C position. Invalid AES values cannot alias another map identity.

Existing watch focus already retains its speaking channel; that policy is not
changed. ADS-C mismatch rejection, FEC and encryption/validation gates remain.
Automatic C-ASSIGN retuning is NOT enabled: validated assignment fixtures and
channel acquisition tests remain required. Per-aircraft WAV indexing remains
future work; these counters are not a complete recording attribution timeline.

No P25, NFM or WFM runtime changes. WFM prototypes remain test-only. This portable
experimental build does not replace the signed-installer update channel. Real
Inmarsat reception and physical speaker handoff still need tester acceptance.
