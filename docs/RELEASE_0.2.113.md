# 0.2.113 - Inmarsat recovery and identity diagnostics (experimental)

Malformed IQ or unusable channel geometry now produces a diagnostic rejection
instead of stopping the worker. No bad block enters the modem. Codec API failures
discard the complete C-frame without partial audio and reset the codec.

Aircraft highlighting/watch focus requires identified speech. Unidentified decoded
audio is preserved and shown honestly; identity changes cannot inherit old speech
activity. EGC is labelled physical-probe-only. Disabled-watch position freshness
uses 300 seconds independently of saved scan settings.

Local JSONL and opt-in remote counters expose input rejections, codec failures,
unknown speech and position identity mismatches, alongside existing stage timing
and audio queue metrics. Ordinary telemetry contains no aircraft IDs or recording
data. Manual five-second recordings still require separate consent and review.

See [audit and test scope](INMARSAT_RECOVERY_AUDIT.md). No P25, NFM, WFM, RDS or SSTV
signal-processing changes; no automatic assignment retuning or weakened CRC gates.
Real RF/voice acceptance still needs tester recordings. Portable experimental
release, not a replacement for the signed-installer updater channel.
