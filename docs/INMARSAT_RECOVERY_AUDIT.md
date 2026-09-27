# Inmarsat recovery audit - T-0084 / DEC-0156

Scope: executable paths at e1d506b, followed by the 0.2.113 changes. This is
not a claim of live RF acceptance or a replacement modem implementation.

## Confirmed defects and changes

- `InmarsatPipeline::process` threw on invalid geometry/amplitude; the Aero
  passband check occurred after probe reset. All original predicates now run
  before receiver mutation. Rejection returns normally with `inputRejected`,
  `lastInputRejected`, `lastError`, consecutive count and reason counters.
  `lastError` retains the most recent rejection reason after recovery; use
  `lastInputRejected` for the current block. Invalid data never advances the PLL.
  Retrying the same sample index preserves state. Skipping a rejected block
  triggers the existing discontinuity reset on the next accepted block.
- Negative `sdr_aero_decode` previously threw. The complete C-frame is now
  rejected without partial PCM, the codec is reset, and failure counters advance.
  Current codec negative results indicate an API failure, not measured RF BER.
- Voice activity previously lacked the valid 24-bit AES qualification. Unknown
  PCM still flows, with separate `speechActive` and unidentified-speech count.
  A validated identity change clears old speech activity and resets the codec.
- Watch activity used speech-frame increments. It now uses identified current
  speech; the existing sticky focus selection and hang behavior are unchanged.
- Vocoder availability was literal true; it now reflects the codec allocation.
  EGC has no Classic Aero decoder/vocoder and its GUI badge says physical probe.
- Disabled-watch map freshness incorrectly depended on saved watch settings.
  It now uses an explicit 300-second display policy, not a new RF timeout.
- Optional `setProbeEnabled(false)` skips only the physical probe, defaults on,
  and does not reset the native modem when toggled.

## Already correct and preserved

CRC gates, 300-byte / valid-SU C-frame gate, native FEC/modulation, unknown-AES
PCM callbacks, audio flush-on-AES-change, type-17 ADS-C identity mismatch rejection,
probe locked=false/framesOut=0, and disabled automatic C-ASSIGN retuning.
P25, analog DSP, SSTV and RDS code are unchanged.

## Instrumentation and privacy

Existing bounded live/replay JSONL now includes rejection reasons/sample ranges,
codec attempted words/API failures, invalid C-frames, identity changes,
unidentified speech and ADS-C identity mismatches. Existing per-stage timing,
load, queue, underrun and PCM counters remain. The remote allowlist forwards only
numeric failure counters, including per-watch-worker counters, through the existing
opt-in transport. It excludes reason strings, AES, aircraft, raw PCM and scatter.
No per-sample logs, extra network channel or automatic evidence upload was added.
Separately consented manual recording/review/send remains as in 0.2.112.

## Verification boundaries

Tests cover injected codec failure after partial work with no emitted PCM,
pre-SU unknown audio, SU30/SU60 identity changes, speech-window bounds, invalid
IQ/passband recovery, optional probe, sticky identified focus, remote allowlist,
GUI unknown-voice status and freshness policy. Existing silence/CRC/channelizer,
audio identity, replay and GUI suites remain mandatory. See BUILD_NOTES for
actual command outcomes, not assumptions based on compiling.

Remaining: real tester recordings for RF/audio acceptance; fixture-qualified
C-ASSIGN acquisition/follow; full warm-state replay (ISS-0038/ISS-0039). This patch
does not make EGC a decoded protocol or claim every hardware combination tested.
