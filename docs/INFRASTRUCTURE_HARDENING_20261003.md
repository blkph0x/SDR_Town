# Infrastructure hardening ledger

T-0102 / DEC-0171; baseline `d47f000`, application version 0.2.122.
This is an unreleased source repair, not a claim of a new public binary or
complete RF qualification. The canonical queue remains [TASKS.md](TASKS.md).

## Reproduced and repaired

| Area | Defect/evidence | Repair and verification |
| --- | --- | --- |
| Hardware RX loss | An injected Soapy overflow left the epoch unchanged and returned 2048 samples spanning the missing interval; five pre-fix assertions failed. | Advance epoch and publish a received-sample floor under the ring/queue locks before post-loss IQ. Drop stale queued IQ. Tests cover two independent cursors, repeated overflow, unchanged received-sample counts, ordinary timeouts and clean recovery. |
| Local control | Accepted partial requests survived stop; malformed/duplicate lengths and transfer encoding were accepted; exception text escaped. Eight pre-fix assertions failed. | Close accepted clients, strict framing, authenticate before JSON, generic errors, one command/connection, cap active clients and absolute request lifetime. Positive commands, stop/restart, limits and reentrant cancellation tests pass. |
| Diagnostic status | Oversized status JSON, trickled replies and consent cancellation violated the bounded request contract. Three pre-fix assertions failed. | One in-flight GET, 128 KiB decompressed reply budget, absolute deadline, and atomic session generation invalidating old callbacks. Tests cover valid replies, limits, cancellation, reconfiguration, busy status and destruction. |
| Late diagnostics opt-in | GUI health monitors were created only when consent was already enabled at startup. Wall-clock/offline time could become false stall evidence. | Always install a lightweight scheduler, collect only for an enabled session, use monotonic elapsed time and reset on session transition. Deterministic late-opt-in/opt-out/re-enable and reporting-rate tests pass. |
| Hardware tone TX | Driver-name capability promotion and swallowed setup errors could activate unconfirmed RF settings. | File-only default; explicit per-command authorization, actual TX-channel/range checks and readback before activation; fail closed on any driver failure. Fake driver tests prove no activation/writes on rejection and cleanup on standard/nonstandard exceptions. No physical RF transmission performed. |

## Diagnostic contract

- Device inventory adds `rxHealth`: reads, timeouts, errors, overflows, cumulative
  driver-lock wait/read microseconds and last-loss received-sample index. They
  are cumulative per stream-state lifetime, not a latency histogram or an
  estimate of missing RF samples. Individually atomic counters are approximate
  snapshots, not one transactional view of the radio.
- GUI runtime reports include local-control accepted/rejected/timed-out,
  dispatched/error and active-client counters. No tokens, request bodies or
  exception details are included.
- Up to eight main receiver HF diagnostic snapshots report block/sample/reset/
  rejection/correction counts, effective filters and reset reasons. Both the
  receiver list and HF state are acquired nonblocking; a busy snapshot is
  omitted, never allowed to stall DSP. This does not cover every specialty
  decoder or establish a process-wide correlation ID.
- Status transport reports in-flight state and failures. Old-session replies
  cannot change a new session's upload state or deliver stale GUI callbacks.
- No automatic IQ/audio/message-body upload was added. Existing diagnostics
  consent and separate recording consent/review remain. Health scheduling
  performs no health collection/upload while disabled. The GUI stall report
  still occurs after the GUI resumes; this is not an independent hang watchdog.

## Limits and operator changes

Control defaults: 16 active connections, 64 KiB total request and a 10-second
absolute connection lifetime. A local client that does not finish or does not
read its response is closed. Normal authenticated FUBAR JSON commands retain
their semantics. The server still binds loopback; public reverse proxies and
authentication policy are not changed by this patch.

The CLI command is now:

```text
tx tone <dev> <mhz> [hz=1000] [sec=2] [gain=20] [dump=path.cf32] [rf=on]
```

Without `rf=on`, it generates a local IQ file only. Duration must be finite,
positive and no greater than 60 seconds. `rf=on` is explicit hardware
authorization for that command, not the GUI's P25 stub arming state. Driver
readback must match requested rate/frequency within 1 Hz and gain within
0.01 dB before activation. This is deliberately fail-closed and may reject
quantized driver settings; it is not proof of radiated frequency/power or
regulatory authorization. There is no silent file fallback after hardware
was explicitly requested and rejected.

Overflow publication changes shared RX behavior only when the driver reports
actual overflow. Ordinary timeout does not prove missing samples and does not
advance the epoch. Existing epoch-aware consumers can reset; old queue-only
consumers still lack a full typed discontinuity contract. No P25 symbol,
slot/ESS, vocoder, speaker gate, FM filter or AudioEngine changes were made.

## Evidence and remaining work

Local Release build: app plus all six native test targets passed on MSVC 17.14,
Qt 6.11.1. All 16 CTest suites pass (71.21 seconds after the follow-up); 240879 core assertions and
184 remote-diagnostics/control assertions pass. Optional external recordings
and private alias fixtures skipped in the generic test invocation are not
claimed as tested. CI uses Qt 6.7.3 and must pass independently.

Final source `65e3da3` passed [Windows CI](https://github.com/blkph0x/SDR_Town/actions/runs/37111782581)
and [workflow validation](https://github.com/blkph0x/SDR_Town/actions/runs/37111782612).
The downloaded CI package matched its checksum, embedded source/run, EXE and
SDRplay plugin hashes and passed DTMF/RDS smoke. That package still omits the
root license/scope/credits; it is not a public release and does not clear
ISS-0060. Full artifact evidence is recorded in BUILD_NOTES.

Follow-up test at `90edc4a` exposed premature consumer epoch acknowledgement
on an empty post-overflow poll (one failed assertion). Acknowledgement now
waits for a nonempty delivery, preserving the HF consumer's reset comparison.
Producer epoch remains visible immediately; retune anchoring and P25 DSP do
not change. The 34-assertion hardware-loss fixture and full suite pass again.

The P25 guard accepts only exact reviewed before/after digests for the four
shared files. Mutation, wrong-path and reverse-patch tests preserve rejection
of unrelated changes. Passing this guard is not proof of live voice continuity.

Remaining gates, in order:

1. **ISS-0060:** complete exact-artifact dependency/source/notice inventory,
   including Qt/runtime/plugins and actual RTL/libusb versions, before a new
   binary release. Root MIT applies to original contributions, not dependencies.
2. **ISS-0055:** migrate the single lease into generation-bound physical-device
   ownership and retune/release tokens. Test two independent SDRs, shared RSPduo
   channels, stale token rejection and device removal before changing P25 callers.
3. **ISS-0055:** fault-inject slow/stuck opens, handoff, stop/reopen and destruction.
   Do not replace timeout/detach recovery with an unbounded GUI-thread join.
4. Add independent hang detection and bounded per-stream duration histograms /
   operation IDs. Preserve consent and never log per-sample payloads.
5. Field-qualify overflow recovery on P25/Inmarsat/RDS/SSTV; retained-rate WFM
   blocker sweeps, fading AM/weak SSB and Morse/DTMF field corpora remain separate.

Publication results and subsequent evidence belong in [BUILD_NOTES.md](BUILD_NOTES.md)
and [LOG.md](LOG.md). No new binary has been published as part of the local gates.
