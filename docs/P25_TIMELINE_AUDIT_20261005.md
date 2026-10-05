# P25 timing and evidence audit, 2026-10-05

Baseline: dfd4c68b0d49a936f0bc802128376d5a2f79c9cc. T-0108 / DEC-0194.
Private evidence stays under D:/SDRTown-Diagnostics/p25-timeline-20261005.
Do not upload IQ, audio, transcripts or raw call identifiers with this report.

## Confirmed repairs

1. GUI pending control results did not identify their acquisition device,
   epoch, reset generation, center, rate or target. Their worker could update
   the trusted offset after a retune. Results now validate all those fields
   before changing the analyzer, grants or trusted offset. Unit cases include
   out-and-back retunes and a new monitor at the same frequency.
2. The recent-IQ CC view could span a major retune boundary although the epoch
   had changed. A controlled Soapy fixture failed on pre-retune sample markers.
   The new explicit current-tuning view honors the existing retune floor; GUI
   and CLI CC use it. Raw recording and chronological voice consumers retain
   their existing behavior. This does not change the existing >25kHz retune
   boundary policy or claim that fine AFC adjustments are gap-free.
3. Initial hardware opening never set the applied-center metadata used by the
   new view. A live qualification run caught zero-center/no-decode before
   publication. Record successful startup tuning after clearing the synthetic
   ring, before real RX starts, and successful PPM-path tuning. The fixture now
   checks initial applied metadata before its first subsequent tune. Unknown
   center cannot pass the CC context check.
4. Passive validation/deep logging invoked throwaway alternate AMBE synthesis.
   mbelib uses a shared random generator, so this changed real output. Removed
   those calls from the live canonical path; explicit forensic probes remain.
   Both reference replay windows now produce identical WAV hashes in normal,
   validation and deep modes, also matching the pre-change normal baseline.

## New evidence contract

Existing capture `_events.jsonl` contains schema-1 `p25_pipeline` events.
No second IQ format, speech payload logging, network upload or callback I/O.
Producers use a bounded preallocated queue with nonblocking admission; dropped
trace events and file-write errors are counted. Formatting happens in the
existing capture writer. Capture start/stop anchors include monotonic time.

- CC availability, submit, complete, consume and stale-result rejection.
- Grant decision TG/RID/slot/opcode/security and exact compiled decision line.
- Voice submit/start/complete/publish and discarded/deferred result reasons.
- Allocation identity, absolute IQ ranges, queue/decode/publication durations.
- Selected/companion VCWs, accepted/fed/rejected/duplicate/context counters.
- Per-result PCM push/pending counts plus aggregate idle speaker top-ups.

Unknown RID stays unknown. Local call IDs may restart when Receiver is rebuilt;
analysis keys also include generation, flush, carrier and slot. Idle top-ups
are not falsely attributed to the last RID. Partial jobs at capture boundaries,
nonzero trace loss or write errors limit what the evidence can establish.

Run `python scripts/analyze_p25_timeline.py <events.jsonl> --output <report.json>`
after capture stops. It rejects malformed JSON/schema, reports trace sequence
gaps, and separates off-channel time from unsubmitted CC IQ within one tuning
context. PCM counts, push spacing and an STT sentence alone do not establish
audible continuity, correct speakers or completeness.

## Measured remaining gaps

First instrumented GUI run: 540.160 seconds, no IQ overrun, epoch rewind or
file-write failure; no trace drops/order errors. This run predates the final
current-tuning/startup fix and the added idle-top-up/grant instrumentation.

- CC enabled365.466s, out of passband144.270s, suspended for follow30.381s.
- CC decode median71.107ms, p95274.362ms, maximum1862.119ms.
- Same-context submitted windows leave370 gaps: 109117440 samples, or53.28s
  at2.048Msps; median64ms, p95384ms, maximum1776ms. These are unexamined RF,
  not a measured number of lost grants. Saved IQ can establish actual grants.
- Voice worker queue median0.019ms, p950.029ms; decode median76.932ms,
  p9590.624ms; publication median4.481ms, p9512.617ms.
- Voice-job backlog is not established as the current delay source. Do not
  add a guessed half-second buffer or change speaker holds from these numbers.
- The first run lacks idle-drain trace, so decoded minus per-job pushed PCM
  must not be called audio loss. Offline STT recovers connected phrases as
  well as questionable fragments; it is not proof of all-call intelligibility.

## Next repairs require their own gates

1. Recover grant ground truth from the unsubmitted CC intervals, correlate
   expensive offset probes and GUI scheduling, then budget a contiguous CC
   consumer. Do not simply enlarge the window or shorten an acquisition timer.
2. Budget/isolate in-passband CC during voice before lifting its suspension.
   DEC-0064 records why the previous concurrent path was disabled. A single
   tuner cannot recover an out-of-passband CC while tuned elsewhere.
3. Identify each remaining audio gap by RF/VCW acceptance, session filtering,
   pending PCM, actual callback consumption and hardware underrun deltas.
   Selected-slot FEC/security policy and normal vocoder remain unchanged.
4. Capture metadata still needs exact retune-segment annotations. Initial
   SigMF center alone does not describe a multi-frequency follow recording.
5. Control offset-probe state is process-static and nominal-frequency keyed.
   Repeated independent P25 controllers need per-controller state before
   sharing the helper concurrently across different radios.

## Release scope

Publisher confirms permitted Microsoft runtime redistribution. This closes
that confirmation item, not the four remaining ISS-0060 materials/review gates.
No public binary is claimed while those checks fail. Source CI and hardware
acceptance are separate; raw diagnostic recordings must not enter CI artifacts.
