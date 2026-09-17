# Code notes (tree map)

DEC-0090: scripts/release.ps1 checks native exit status, clean source, actual
branch, CTest and signed packages. sign_update_manifest.ps1 rejects trust-anchor
mismatch rather than rewriting it during ordinary signing. verify_release.py
checks hashes, embedded-key signature, portable runtime/paths and configured RTL.
test_verify_release.py tests happy-path layout and rejection cases with tiny
fixtures; real release verification separately exercises OpenSSL. Windows CI
explicitly builds the native core without the MinGW RDS DSP runtime; local full
release gates include that backend. See RELEASING.md for publication semantics.

DEC-0088/0089: CMake stage_rtl_runtime stages the configured shared RTL target
instead of trusting stale output-folder DLLs; deploy includes package licence.
probe_rtlsdr_lifecycle.py isolates native RX lifecycle from Qt/Soapy/DSP.
test_gui_shutdown.py and test_rds_live_gui.py --debugger collect CDB exception
stacks and reject first-chance AVs. See NATIVE_RUNTIME_QA.md for acceptance scope.

DEC-0087: diagnose_rds_iq.py accepts explicit cf32_le/cu8 with whole-complex
sample validation; test_rds_iq_diagnostic.py proves format parity and rejects
malformed input. test_rds_live_gui.py --rf-gain uses the existing authenticated
loopback API, restores prior gain, and requires real hardware in final results.

DEC-0086: ReceiveDecoder.cpp optional RdsParity wrapper compares native and
adapted live results with bounded counters and teardown-only JSONL output.
test_rds_cli.py verifies it on recorded data; test_rds_live_gui.py --parity
requires agreement AND reception. diagnose_rds_iq.py provides bounded offline
FFT-channelized MPX evidence, with a synthetic normalization/invalid-input test.
Neither changes production DSP settings.

DEC-0085: ReceiveDecoder.h/cpp defines immutable compiled descriptors, borrowed
versioned raw float blocks, typed snapshot variants and native adapters. Internal
sdr_town_decoders links existing RDS/tone libraries and owns the high-level MPX
file reader. Receiver's RDS session and CLI MPX file reader share this adapter;
tone live paths and P25 are not migrated. CLI `decoders` lists requirements
without module/hardware probing. See RECEIVE_DECODERS.md and adapter parity tests.

DEC-0084: DcsBitDecoder generates Golay-valid codewords for 105 reference
payloads, indexes physical inversion/cyclic aliases and requires repeated words.
DcsDecoder owns eight bounded timing hypotheses and thread-safe snapshots.
GUI NFM raw-data branch feeds both CTCSS and DCS; speaker/P25 processing does
not use their results. RdsStatusWidget presents equivalent DCS labels and hides
stale data. CLI `tones dcs` / `tones dcs-bits` and test_dcs_cli.py provide bounded
offline diagnostics. No new third-party dependency or copied decoder algorithm.

DEC-0082: NFM data tap has an independent phase-continuous oscillator; speech
AFC threshold resets do not create data epochs. Same-rate/length bandwidth
updates retain IQ history. Explicit source/retune reset still resets the data
path. Analog NFM bandwidth-only GUI changes no longer discard the input cursor.
First 32 data epochs log reset reason bits; bounded startup diagnostics do not
log per-sample data. scripts/test_dcs_reference.py is an independent protocol
oracle and ideal waveform generator, NOT an implemented DCS receiver.

DEC-0081: CtcssDecoder (sdr_town_tones) is a single-owner, bounded streaming
Goertzel identifier with locked snapshot publication and WAV/FLAC diagnostics.
Demod's isolated FM tap now accepts NFM plus explicit nominal channel identity
for AFC continuity. GUI analog worker routes NFM raw data to CTCSS, WFM to RDS;
P25 branches unchanged. Above-spectrum status and GUI diagnostic JSON expose
CTCSS. CLI `tones file` is offline and does not enumerate/open radio hardware.

DEC-0080: Receiver owns RdsMpxDecoder on existing DSP worker; WFM obtains IQ
provenance from getNewIQWindowForReceiver and resets only data-tap state on gaps.
RdsStatusWidget displays plain-text confirmed metadata, clears on retune/inactive
mode and hides stale identity. GUI self-test JSON contains RDS diagnostics.
visibleBandSections resolves/clips priority overlaps, cached by SpectrumWidget.
Waterfall drags freeze the frequency transform and commit one tune on release.

DEC-0079: RdsDspAbi and src/rds_backend isolate pinned Redsea/liquid-dsp in a
MinGW C ABI DLL. RdsMpxDecoder owns one decoder, recreates on provenance changes,
rejects nonfinite/unbounded input and publishes mutex-protected snapshots.
RdsMpxFile reads bounded mono MPX through existing miniaudio. `rds mpx` is an
offline CLI diagnostic. Demod's optional MPX branch now has separate causal
filter and decimation history; legacy speech/P25 processing is unchanged.
cmake/RdsBackend.cmake builds/copies the DLL beside native app and test binaries.

DEC-0078: RdsDecoder wraps pinned redsea BlockStream/group (sdr_town_rds static
library). API takes differential-decoded bits, emits only complete validated
groups, and bounds station text assembly. Tests use upstream reference bits,
independent polynomial-generated groups and corruption fixtures. CliApp's
read-only `rds bits` command parses bounded files and returns a JSON snapshot.
Demod's optional FmMultiplexBlock branches before speech DSP with explicit
sample-rate/epoch semantics and no queue. No live RDS consumer yet. Vendor
license/source notices are included by deploy and install rules.

DEC-0077: P25AudioResampler now owns the extracted stateful PCM interpolation
function. The two-input-sample causal delay removes block-tail future-sample
clamping; phase, sample count and DC history remain persistent. Regression
test: test_p25_audio_resampler.cpp. No analog DSP or P25 security changes.
compare_pcm_wav.py checks actual normalized WAV samples, not container hashes;
test_compare_pcm_wav.py covers format equivalence and malformed input.

DEC-0076 / REQ-BP.1: `BandPlan` provides immutable country/local profiles,
atomic shared snapshots, priority/specificity lookup and bounded JSON validation.
`BandPlanDialog` handles selection, import/export and explicit persistence.
`SpectrumWidget` overlays profile/service hints and visible service boundaries;
`MainWindow` supplies actual monitor frequency, menu/button and visibility.
Existing AUTO selection and classifier priors query the catalog by value.
CLI `bandplans list/select` and GUI `--gui-bandplan` expose the same catalog.
`test_bandplan.cpp` covers bounds, ambiguity, hostile imports and concurrency;
the Qt test target covers preview/Apply/cancel and temporary settings isolation.
Source/coverage limitations and schema: `BAND_PLANS.md`. No P25 gates changed.

DEC-0075 / REQ-UI.1: `WorkspaceLayout` owns named dock registration, preset
visibility/tab arrangement, versioned QSettings persistence and View actions.
`MainWindow` reparents the existing widgets only; receiver signal connections
and DSP workers are unchanged. `CliApp` parses workspace/size/screenshot flags;
the GUI saves its own widget image for QA. Tests: `test_workspace.cpp` (separate
Qt Widgets/Catch executable) and `scripts/test_workspace_gui.py` (actual GUI,
four presets, startup report + screenshots). No new decoder dependency added.

DEC-0074: `AudioEngine::RingBuffer::ConsumerLease` serializes callback read
cursor commits with exceptional clear/bridge-discard operations. Callback
tries once and emits silence on contention; control threads may yield while
waiting. Producer writes retain SPSC behavior under the existing audio mutex.
Six cumulative callback/producer counters are sampled into GUI ring-health CSV
outside the realtime thread. `p25_capture_audit.py` reports their deltas and
rejects reset/invalid series; empty callbacks are not inferred speech loss.
Regression cases: `[audioengine][cursor]`.

DEC-0071/72/73: validation can opt into all-window offline tracing.
`dsp/P25CqpskStagedScorer.h` validates physical quadrant mappings, used only
for Phase 2 traffic candidate search/reuse in `P25LiveDecoder.cpp`. The decoder
also walks block tails from a current-window anchor and excludes those bursts
from uncovered-sync processing. Tests: `[mapping]`, `[block-tail]`.

2026-09-17 follow-up: `P25LiveDecoder.cpp` resolves I-ISCH ownership before
selecting a mutable slot session (DEC-0069); callers pass the actual burst
position. `P25VoiceDecode.cpp` validation adds absolute stream coordinates.
Its speaker push helper accepts an explicit end-of-stream flag used only by
GUI replay tail drain (DEC-0070). Live defaults remain unchanged. Regression
coverage: `[slot-session]` and `[p25][audio]`.

2026-09-17: `include/P25Gf64.h` provides immutable GF(64) product/inverse
tables for the RS recovery path in `P25LiveDecoder.cpp` (DEC-0067). Exhaustive
byte-domain equivalence tests live in `tests/test_p25live.cpp`. The decoder
also caches its 63 unit-symbol syndrome columns; neither cache carries call,
slot, or mutable decoder state. Playback top-up drains decoded PCM only;
it no longer appends speculative clock silence (055312 forensic report).

If a P25 file's purpose is not in this table, the map is wrong — fix the map
in the same commit. Analog/GUI modules are listed at coarse grain until they
become the active REQ.

| Path | REQ | Spec / cite | Notes |
|---|---|---|---|
| `SOURCE_OF_TRUTH.md` | — | law | Product law L1–L8 |
| `CAUSE_EFFECT_MAP.md` | — | gates | A–E buckets; REQ-P2.* |
| `DEVELOPMENT_RULES.md` | — | method | Athanor process, not sovereignty law |
| `src/main.cpp` | P2.* | DEC-0040 | Bootstrap + `main()` only (~200). |
| `include/P25VoiceSession.h` `src/P25VoiceSession.cpp` | P2.* | DEC-0040 | Session/sustain/cadence/tail-grace/streaming-DDC helpers |
| `include/P25DecodeConfig.h` `src/P25DecodeConfig.cpp` | P2.* | DEC-0040 | Live decoder configs, control offset probe, CLI decode report |
| `include/DemodModeUtils.h` `src/DemodModeUtils.cpp` | — | DEC-0040 | Mode strings, voice diag labels, band plans |
| `include/SavedFrequencies.h` `src/SavedFrequencies.cpp` | GUI | DEC-0040 | Saved-frequency JSON + table populate |
| `include/P25VoiceTiming.h` `src/P25VoiceTiming.cpp` | P2.* | DEC-0040 | Timing constants, LO park, chunk planner |
| `include/P25TalkgroupRegistry.h` `src/P25TalkgroupRegistry.cpp` | follow | DEC-0040 | Talkgroup/CC/channel-ID persistence + grant helpers |
| `include/P25AppGlobals.h` `src/P25AppGlobals.cpp` | P2.* | DEC-0040 | Shared atomics / cadence mirror / diag-stage accessors |
| `include/P25RollingIq.h` `src/P25RollingIq.cpp` | P2.* | DEC-0040 | RollingIqWindow + pull/prepare/backlog cursor |
| `include/P25VoiceDecode.h` `src/P25VoiceDecode.cpp` | P2.* | DEC-0002…0039 / DEC-0040 | Decode/feed/emit/AMBE + shared RF helpers. Block-channelize default; streaming DDC env opt-in (DEC-0014/0038). Do not add gates without a named bucket. |
| `include/P25VoiceTest.h` `src/P25VoiceTest.cpp` | P2.0 | DEC-0040 | SigMF/WAV + replay followtest/voicetest |
| `include/CliApp.h` `src/CliApp.cpp` | — | DEC-0040 | `runCLI` + GUI runtime parse + batch arg helpers |
| `include/AppBootstrap.h` `src/AppBootstrap.cpp` | — | DEC-0040 | Logging, theme, instance guard |
| `include/MainWindow.h` `src/MainWindow.cpp` | GUI | DEC-0040 | Declaration-only header (~520); ctor/UI bodies. Ctor calls `startP25LiveDecodePipeline()` (ISS-0010). |
| `src/MainWindowP25Voice.cpp` | GUI / P2.* | DEC-0040 | Live voice worker, job submit/backpressure, take/purge, decode publish + speaker push. |
| `src/MainWindowP25Orchestration.cpp` | GUI / P2.* | DEC-0040 / ISS-0010 | `startP25LiveDecodePipeline()` — guiDspWorker rolling-IQ / chunk-plan / submit / CADENCE loop (mechanical extract from ctor). |
| `src/tools/p25_orchestration_sources.py` | — | DEC-0040 / ISS-0009 | Concat corpus + `definition_body` / `require_definition` anchors for `verify_p25_phase2_*.py` |
| `src/tools/_extract_mainwindow_out_of_line.py` | — | DEC-0040 | One-shot MainWindow out-of-line extractor (kept for re-runs) |
| `include/P25SdrtrunkTune.h` | follow | DEC-0015/0016 / SDRTrunk CenterFrequencyCalculator | Follow LO is single-channel voice park (voice−11249). Two-channel set calculator is citation only — 115315 997 kHz edge. |
| `include/P25AudioDropClass.h` `src/P25AudioDropClass.cpp` | REQ-P2.0 | DEC-0002 | Pure A–E classifier from CADENCE/voicetest counters |
| `include/P25LiveDecoder.h` `src/P25LiveDecoder.cpp` | P2.1 | SDRTrunk HDQPSK / Voice2/4 / DEC-0033/0034/0038 | IQ → dibits → superframe/ISCH/XOR/MAC/ESS/Voice2/4. Streaming: persistent framer commit when anchor known; sticky Gardner during unlocked search (DEC-0038); companion-only sticky fallthrough **streaming-gated only**. |
| `include/dsp/P25Phase2Framer.h` `src/dsp/P25Phase2Framer.cpp` | P2.1 | OP25/SDRTrunk streaming framer | 180-dibit burst / 720-dibit superframe |
| `src/dsp/P25StreamingChannelDdc.cpp` | P2.1 | DEC-0014/0018 / SDRTrunk HDQPSK | Stateful DDC; overlap must be 0 when enabled. Opt-in via `SDR_TOWN_P25_STREAMING_DDC=1`. DEC-0018: do not jump the dibit lattice to RF-sample time (105622 env=1 duty 0.095). Locked hops 80 ms. |
| `src/dsp/P25CqpskStagedScorer.cpp` | P2.1 | — | Cheap CQPSK pre-filter |
| `src/P25Control.cpp` `include/P25Control.h` | follow | TSBK / Phase 2 MAC grants | TG, Hz, slot, enc. Does not open speaker alone |
| `src/P25FollowStateMachine.cpp` `include/P25FollowStateMachine.h` | P2.5 | DEC-0029 | Stay vs return-to-CC; slot probe. Clear-trusted: 40s speaker grace without live VCW + 15s activity silence (053448 quiet-return thrash). Pure policy |
| `src/P25TrafficChannelProcessor.cpp` `include/P25TrafficChannelProcessor.h` | P2.5 | — | Observational call-active / audioOpen |
| `include/P25ReceiverSession.h` | P2.2 | — | Per-RX key, pending AMBE, abs-dedupe, latch |
| `src/AudioEngine.cpp` `include/AudioEngine.h` | P2.4 | miniaudio | Ring, underrun, digital-voice jitter cap |
| `src/DeviceManager.cpp` `include/DeviceManager.h` | follow | Soapy/RTL | IQ stream + one-RTL retune |
| `src/Demod.cpp` `include/Demod.h` | analog | — | WFM/AM/NFM/SSB; keep stable |
| `src/Receiver.cpp` `include/Receiver.h` | — | — | Logical channel; owns P25 session state |
| `src/SpectrumWidget.cpp` | GUI | — | FFT/waterfall |
| `external/mbelib/` | P2.2 | AMBE 3600×2450 | Vocoder when `SDR_TOWN_ENABLE_MBELIB` |
| `_codex_refs/sdrtrunk/` | cite | DEC-0003 | Reference implementation, not linked |
| `_codex_refs/op25/` | cite | SPEC_INDEX | Reference implementation, not linked |
| `src/P25TxSession.cpp` `src/P25AmbeEncoder.cpp` `src/P25Phase2TxFramer.cpp` | TX later | — | Lab TX shells; not the RX continuity REQ |
| `src/tools/p25_capture_audit.py` | P2.0 | field logs | Forensic counts; not a SoT checkbox |
| `src/tools/verify_p25_phase2_*.py` | ISS-0002 | string locks | Invariant guards only |
| `tests/test_p25follow.cpp` | P2.5 | — | Return/hold policy units |
| `tests/test_p25_audio_drop_class.cpp` | P2.0 | DEC-0002 | Classifier vectors |
| `tests/test_p25_sdrtrunk_tune.cpp` | follow | DEC-0015 | CenterFrequencyCalculator numbers (voice−11249; 095450 pair) |

### Cadence / tail / streaming-DDC ownership (ISS-0008)

**SoT for adaptive cadence/tail/streaming-DDC helpers is `P25VoiceSession`; SoT for named constants is `P25VoiceTiming.h`; SoT for mirror atomics is `P25AppGlobals`.**

| Concern | Owning file | Notes |
|---|---|---|
| Named CADENCE / pending-depth / completed-result **constants** | `include/P25VoiceTiming.h` | e.g. `kP25Phase2VoiceDecode*CadenceMs`, `kP25VoiceDecodeMaxPendingJobs*`, `kP25VoiceDecodeMaxCompletedResults` |
| Adaptive cadence + speaker-sustain pending depth + **audio-tail grace** helpers | `include/P25VoiceSession.h` `src/P25VoiceSession.cpp` | `p25Phase2AdaptiveVoiceDecodeCadenceMs`, `p25VoiceDecodeMaxPendingJobsNow`, `kP25Phase2*AudioTailGraceMs` |
| Cadence **mirror / rollup atomics** (GUI/CLI diag) | `include/P25AppGlobals.h` `src/P25AppGlobals.cpp` | `gP25Phase2Cadence`, `p25Phase2NoteCadenceWindow` |
| Streaming-DDC **env gate** + experiment flag | `P25VoiceSession` (`p25Phase2StreamingDdc*`) | Opt-in `SDR_TOWN_P25_STREAMING_DDC=1`; DDC impl in `P25StreamingChannelDdc.cpp` |
| Decoder configs that **read** streaming-DDC flag | `include/P25DecodeConfig.h` `src/P25DecodeConfig.cpp` | Builds live voice decoder configs; does not own the env parse |
| Live GUI worker that **calls** cadence/backpressure | `src/MainWindowP25Voice.cpp` + `src/MainWindowP25Orchestration.cpp` | Submit/can-accept use `p25VoiceDecodeMaxPendingJobsNow`; rolling loop in orchestration |
| CLI / voicetest replay path | `P25VoiceTest` / `CliApp` | Same constants/helpers; dual path → ownership map below |

### Live GUI vs CLI/voicetest ownership (ISS-0011)

| Concern | Owning TU | Notes |
|---|---|---|
| Policy **constants** (CADENCE ms, eye sizes, pending caps, …) | `include/P25VoiceTiming.h` | Change numbers here only |
| Session helpers (adaptive cadence, sustain depth, tail grace, streaming-DDC gate) | `P25VoiceSession` | Both live and replay call these |
| Decode / feed / emit / AMBE gates | `P25VoiceDecode` | Shared RF/decode path |
| Live GUI voice worker + publish/backpressure | `MainWindowP25Voice.cpp` | Queue / busy / submit |
| Live GUI rolling-IQ orchestration | `MainWindowP25Orchestration.cpp` (`startP25LiveDecodePipeline`) | After ISS-0010 extract |
| Replay / voicetest | `P25VoiceTest` | File IQ path; must call same helpers |
| CLI batch | `CliApp` | Batch entry; must call same helpers |

**Rule:** change policy in the owning TU; both live and replay/CLI paths must call the same helpers (no duplicated constants).

