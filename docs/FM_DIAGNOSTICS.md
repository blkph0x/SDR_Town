# FM continuity diagnostics (0.2.105)

## 0.2.106 extension

NFM PCM now uses a persistent four-sample cubic clock, delayed by two input
samples (about 42-44 microseconds at typical discriminator rates). No callback
tail is repeated to fill the output hint. maxPcmDelayUs reports this delay;
hintMismatchBlocks counts requested-size differences, not sample loss. The
cumulative clock can legitimately yield one more or fewer samples in a callback.
NFM lookahead-read/phase-repair counters should no longer increase. WFM still
uses the earlier converter; its counters and open issues retain their meaning.
The original milestone description below is historical for 0.2.105.

This milestone repairs NFM's discriminator input continuity, not all analogue
audio issues. Existing FIR coefficients and bandwidth/deviation/de-emphasis
settings are retained. FIR application is causal, adding its true group delay
(reported as maxFirDelayUs); decimation retains phase across callbacks. WFM and
HF processing, P25 decode/follow/security and the speaker engine are unchanged.

## Local and remote reports

GUI and CLI startup launch one low-rate diagnostics worker. It writes changed
FM aggregates every 30 seconds and once on normal shutdown to:

`%APPDATA%/SDR_Town/SDR Town/fm_diagnostics/fm-<process-id>.jsonl`

Qt's application-data root is used, so test/CLI application naming may change
the prefix. One current file plus one rotated file, at most 1 MiB each, are kept
per process. Startup retains at most eight inactive process logs (and their
backups); active writers are protected by lock files. Cleanup/write failures
may leave files behind; localWriteFailed is reported rather than silently
pretending receipt. A crash can lose the last unsampled 30 seconds.

No disk writes, network requests, logger locks or growing diagnostic queues run
in DSP. Fixed-size atomic counts and monotonic-clock timings are recorded once
per block. Snapshots are approximate process-wide aggregates by NFM/WFM, not
per-channel measurements or atomic multi-field transactions. Concurrent streams
of the same mode are combined. They must never drive decoder state.

Reports contain input/discriminator/output/requested counts, DSP resets, empty
audio blocks, resampler unavailable-lookahead reads and phase corrections,
channelizer/discriminator-and-LPF/resampler/post-audio durations, maximum block
cost, input duration, over-budget blocks and maximum NFM FIR delay. Durations are
microseconds. Cumulative totals reset on process restart. Very short input
durations round to microseconds; over-budget counters describe block CPU cost,
not proof of hardware overruns or audible loss.

Unavailable-lookahead reads count interpolation accesses, not missing unique
samples. The existing resampler substitutes its tail in those cases: this release
exposes that behavior but does not claim to repair it. WFM's existing block-local
filter/decimator issues remain separate follow-up work (ISS-0037).

Remote event `fm.pipeline.sample` uses the existing opt-in transport and byte
budget. **Help > Share Diagnostic Reports** controls sharing;
`--no-remote-diagnostics` overrides consent. Local logging still works without
internet or consent. No IQ, audio, frequency, talkgroup, radio/aircraft identity
or filenames are included in the FM payload. Existing transport envelopes still
contain the disclosed app/device/session metadata. Collector deployment limits
in DIAGNOSTICS_SHARING.md remain open; local tests are not remote field receipt.

## Reproductions and regression gates

`sdr_town_tests.exe "[nfm][stream]"` compares real discriminator output from
identical synthetic IQ at 48 kHz, 2.048, 2.4 and 10 MS/s with whole, 8192-sample
and irregular/tiny partitions. Equal sample counts and <1e-5 maximum waveform
error are required. Explicit reset and input-rate transitions must match a fresh
decoder. Before repair seven parameter combinations failed; afterward all pass.

`remote_diagnostics_tests.exe "[fm][diagnostics]"` covers idle suppression,
numeric reports, rotation, active-writer protection, retention, write-failure
reporting and worker lifetime. Full CTest additionally exercises existing P25,
HF, FM, RDS, CTCSS/DCS, SSTV, Inmarsat and device tests.

Next: actual PCM partition characterization, isolated resampler repair, NFM
blocker rejection measurements and separately qualified WFM changes. No claim
that this synthetic pass establishes better speech on every physical receiver.
# WFM continuity extension (0.2.108)

WFM now reports causal maxFirDelayUs and maxPcmDelayUs using the existing bounded
FM counters. hintMismatchBlocks compares clock-derived PCM to caller hints, not
lost samples. resamplerLookaheadReads/resamplerPhaseRepairs should not increase
for WFM after this repair. RDS and NFM processing are unchanged.
