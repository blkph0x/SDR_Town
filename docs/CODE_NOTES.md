# Code notes (tree map)

## Auto bandwidth policy (DEC-0144)

AutoBandwidthCheck owns persisted monitor/autoBandwidth (default true) and
resolves automatic GUI monitor width proposals: mode, band-plan, classifier and
remote mode defaults. Explicit widths and P25 setup bypass it intentionally.
Snapshot field autoBandwidth and transition log monitor.auto_bandwidth expose
state without per-sample overhead. Qt tests exercise click, defaults and reload.

## NFM PCM clock (DEC-0143 / T-0071)

NfmPcmClock.h retains the existing cubic polynomial with four real/history
samples and two-input-sample delay. Absolute input/output counters make output
independent of callback partitions. Only NFM uses it; Demod.cpp resets deferred
audio state safely and completes NFM startup fade per sample. FmDiagnostics
reports delay and hint mismatches. No WFM/HF/P25 algorithm changes.

## NFM continuity / FM telemetry (DEC-0142 / T-0070)

Demod.cpp NFM branch uses persistent causal FIR history and decimation phase;
coefficients and WFM/HF/P25 algorithms are unchanged. FmDiagnostics.h collects
bounded lock-free numerical process/mode counters and stage durations, never
controls DSP. FmDiagnosticsLog.cpp samples on a dedicated Qt thread, rotates
local JSONL, retains inactive sessions and forwards opt-in summaries through
RemoteDiagnostics. See FM_DIAGNOSTICS.md for fields, privacy and known gaps.

## Antenna control (DEC-0140 / T-0068)

AntennaControl.h/.cpp isolates bounded asynchronous Hamlib ERP transport,
RotatorController finite limits/freshness/arming/stop ordering and SwrMonitor's
read-only PTT/SWR queries. AntennaControlWindow provides pointing, limits/park,
meter and bounded event tabs. Main only installs the standalone menu; no SDR
ownership or receive-chain changes. Settings persist, armed/connected states
do not. Local antenna/control.log rotates to control.previous.log at 1 MiB.
No new linked dependency: external rotctld/rigctld own the hardware drivers.
Controller catalog facts and setup limitations are in ANTENNA_CONTROL.md.

## Diagnostics delivery/performance (DEC-0139 / T-0067)

DiagnosticsMenu installs a standalone Help sharing action, saved via QSettings;
explicit launch-off overrides it. Opt-out discards queued reports and aborts
in-flight requests (already transmitted bytes cannot be recalled). No P25
receive/audio/orchestration files changed. ProcessPerformance samples CPU,
working/private memory, handles and thread count on the diagnostics thread
every 30 seconds. InmarsatPipeline adds measured stage timing; remotePayload
allowlists bounded anonymous worker summaries. No RF math or queue enlargement.

StageDiagnostics.cmake generates consent-off defaults beside the executable
and in deploy staging. Official release builds fail without the restricted
collector credential. CI credential is separate from local admin credentials.
RemoteDiagnostics checks JSON acknowledgement, bounds POST lifetime/response,
and reports delivery counters. The collector rejects invalid envelopes,
nonfinite/over-deep data, excess per-client/global bytes and requests; caps
concurrent request threads at 32 and socket inactivity at 10 seconds. Admin
authority never falls back to the client key. Shared distribution credentials
are extractable: per-install enrollment/revocation is NOT implemented here.

## Inmarsat monitoring (DEC-0138 / T-0066)

InmarsatMonitorWidget provides passive decoder/aircraft tabs and pop-outs.
It polls snapshots only while visible at 500 ms, bounds history to 500 lines,
preserves table sort/selection, and never owns a DSP callback. MessageStore
adds a mutex-protected bounded aircraft registry independent of rolling logs;
position age is distinct from identity receipt age. InmarsatPipeline counts
actual emitted messages separately from CRC units. InmarsatAero forwards the
explicit ADS-C airframe ID after the existing identity/CRC checks. Country
range facts are pinned CC0 data; see INMARSAT_MONITOR.md. P25 unchanged.

## Contributor regression coverage (DEC-0137 / T-0065)

PR #33 from fubarzi supplies the busy-traffic map regression; its historical
aa8c4d5 supplies the active-IQ SDRplay restart idea. Adapted tests prove actual
map painting survives chronological eviction and clears with the store, plus
five Inmarsat worker restarts consume synthetic IQ and restore live receiver
ownership without producing frames/PCM from zeros. RAII cleanup covers failures.
No production source, P25 pipeline, profile search paths or release version
is changed. Third-party DLL discovery and obsolete cache/release edits are not
adopted; the shipped matched driver/current validated cache remain authoritative.

## Continuous in-band Aero / RF viewport (DEC-0136, 0.2.102)

InmarsatWatchConfig.simultaneousInBand persists with default true. Planner
combines mixed roles only when all enabled channels fit bandwidth and budget.
Group.simultaneous suppresses scheduler transitions; validatedData excludes
voice channels, speaker activity explicitly requires a voice worker. Existing
channel worker barrier and separate modem/vocoder ownership stay unchanged.
InmarsatWatchSpectrum stores normalized viewStart/viewSpan, reused for every
visual/click transform; pan/zoom never requests RF tuning. Drag threshold uses
Qt's platform setting. Minimum span is 16 FFT bins (UI policy, not RF resolution).
Wheel scales by 0.8 per notch, with bounded exponent; RF/rate changes reset view.
InmarsatFirHistory mirrors the 65-sample FIR ring so dot products need no modulo
per tap. Original tap order and double accumulation are preserved and compared
exactly against the old reference for 20,000 inputs. Chunk parity now covers
2.048 and 10 MS/s as well. This is not a new PFB, SIMD or oscillator algorithm.

## Aero channel budget/readiness (DEC-0135, 0.2.101)

InmarsatWatchConfig defines a shared UI/validation bound of 16 concurrent
decoders. Planner still groups by role and actual sample-rate/filter margin.
InmarsatWatchSchedule accepts current-group CRC progress and expires it after
dataDwellSeconds. Early transition requires strict majority plus positionTarget;
deadline fallback remains explicit partial/no-position collection. Worker results
feed evidence only after the job barrier. Per-source multi-SDR migration remains
T-0062, documented in MULTI_SDR_SESSIONS.md, not implemented in this change.
InmarsatMessageStore separately retains the latest validated position per AES,
bounded to 256 entries and evicting oldest receipt when full. The live map uses
positions(), not the 500-message presentation log. Replay remains independent.

## Constellation identity (DEC-0134, 0.2.100)

InmarsatWidget successful manual tuning releases pinned watch UUID selection.
InmarsatWatchSpectrum::setChannels resolves only the requested active decoder
(or first active for automatic selection); setChannel clears old points and
labels actual RF/rate. Missing peers remain absent. GUI tests cover manual and
preset retunes, exact identity, lock/absence states. Native tests exercise actual
MSK/OQPSK scatter callbacks for all four continuous rates and reconstruction.
No modem DSP, scheduler, speaker selection or P25 changes.

## CI publication verification (DEC-0130)

windows-ci.yml release/* builds publish immutable portable versions with
version-specific notes. Post-publication anonymous download verifies the ZIP
against its public checksum and built bytes, then checks source/run/executable
provenance and smoke-runs the shipped CLI. Experimental tags remain prereleases,
preserving the prior signed installer updater. Permanent delivery rule lives
in DEVELOPMENT_RULES section 11 and is linked from SOURCE_OF_TRUTH.

## RTL-SDR Bias-T control chain (DEC-0129)

RtlBiasT.h/cpp isolates the Soapy `biastee` boolean capability, strict cached
readback and best-effort OFF. RtlBiasTWidget handles capability visibility,
explicit DC-safety confirmation and rejected-write rollback. DeviceInfo keeps
driver observations separate from saved intent. DeviceManager probes the actual
handle, applies intent at start, serializes checked live writes with existing
native I/O locking, and powers down at clean/fault cleanup. Opening/stub sessions
reject live requests. CLI biastee and selected-RTL Device Manager use the same
setter. P25 algorithms, IQ rings, tuning and sample-loop cadence are unchanged;
RX-function edit is fault-cleanup-only. Tests cover adapter, widget and isolated
Soapy factory lifecycle. No fake-driver operation can reach a real receiver.

## SDRplay controls and capability lifecycle (DEC-0128)

SdrplayControl is the isolated driver adapter: probe actual antennas/settings/
gain ranges; prepare validates requests without I/O; applyChange writes and
checks driver readback; apply restores a complete profile after activation.
RFGR is an LNA state and its optional rfgain_sel alias cannot overwrite it.
RSPdx Bias-T is B-only. Automatic bandwidth calls setBandwidth(0), not a no-op.
Readback confirms driver state, not physical voltage or RF sensitivity.

DeviceManager publishes capabilities from the already-open handle. Control
commands serialize, keep the device alive during I/O and commit only after
success; saved/stopped and driver-confirmed states are distinct. Startup catch-
up restores the whole SDRplay profile without disabling IF AGC. Non-SDRplay
sample delivery, tune loop and P25 decoder/audio policy are unchanged.
SdrplayControlsWidget is shared-testable physical-control UI, including antenna,
error feedback and capability-aware enablement. Device Manager refreshes model
snapshots at 250 ms without reprobe/stop. CLI/API propagate failure. Contract
fixtures test open/restart/readback and every RSPdx widget; no field claim.

## Aero parallel watch and local visuals (DEC-0127)

InmarsatWatch::Channel owns one thread and constructs/destroys its complete
pipeline there. submit/wait admits one immutable borrowed block; wait-all drains
exceptions before input reuse. Only the session owner publishes messages and
arbitrates speaker PCM. InmarsatEngine applies watch revisions at that barrier,
not on the GUI thread. Existing tune confirmation/cursor reset guards remain.

InmarsatAero subscribes to bundled JAERO ScatterPoints after recovered symbols,
copying at most 300 finite points. Pipeline/Watch expose typed local displays,
separate from JSON diagnostics. Engine's lightweight displaySnapshot takes latest
FFT directly without map/message/report copies. Widget has a 50 ms visual timer
and a separate 500 ms status timer, both stopped when hidden. Spectrum keeps a
circular image and rejects repeated FFT values; selected-channel constellation
has fixed axes and does not infer lock from dot shape. Click-to-add saves stable
channel IDs; hot edits are worker-applied, one tuner still schedules groups.

## Inmarsat P25 handover (DEC-0126)

- SatcomHostServices adds a typed, read-only-by-default preflight; exceptions
  fail closed. The existing begin/end receiver ownership checks remain intact.
- InmarsatEngine resolves the selected stable device and validates empty watch
  lists before preflight. GUI Start asks, rechecks on Yes, then starts normally.
- MainWindow's additive Satcom host callback checks selected-device P25 state,
  including configured/stopped CCs. Confirmation quiesces P25 receivers and
  pending audio before the existing force-leave-P25 control-tune operation.
  A changed monitor target rejects delayed old CC retune callbacks. Busy locks
  return a retry message before mutation; other-device probes do not stop P25.
- No P25 algorithm or existing MainWindow handler was edited. The original
  additive-only host guard accepts this patch without an allowlist expansion.

## Saved Aero watch and SSTV lifecycle (DEC-0124 / DEC-0125)

- `InmarsatWatch`: validated saved channels/policy, rate-aware grouping, steady
  clock scheduler, current-visit distinct positions, per-channel native pipelines,
  stable single speaker focus, timing/gap/load reports. Worker-thread owned.
- `InmarsatEngine`: confirmed group tune without lease churn; atomic config save,
  IQ cursor/PCM reset after tune, lifecycle-owned session.
- `InmarsatWatchUi` / `InmarsatWatchSpectrum`: real-only spectrum/waterfall,
  absolute-frequency click mapping, saved list and bounded policy UI. Manual live
  mode allows editing; running automatic session freezes its configuration.
- `InmarsatMapWidget`: received position age/staleness, no extrapolation.
- `InmarsatAero` / `InmarsatPipeline`: additive speech counter only, no DSP/FEC/
  codec/PCM changes. `InmarsatDiagnostics` extends only the scalar allowlist.
- `SstvReceiverFeed`: control admission barrier; matching-session ownership and
  contention discontinuities preserved. Unchanged producer stress test retained.
- Tests: scheduler/grouping/focus/privacy, actual GUI persistence/lifecycle,
  release/reference replay and full CTest gates.

## Inmarsat live tone routing (DEC-0123)

- InmarsatWidget owns frequency/decoder controls, initializes them from saved
  config without selecting a plan, and shares applyTuningControls for Tune and
  Start. Explicit plan/channel actions resync; timer refresh preserves edits.
- InmarsatEngine pairs SatcomHostServices begin/end with its tuner lease. No RF
  start/retune occurs when the host refuses parking. Error cleanup is dispatched
  to the GUI without blocking a worker join, and Start drains a prior failed
  session before acquisition; a delayed cleanup cannot unpark an active session.
- InmarsatAudio exposes producer-side PCM statistics and callback consumption,
  keeping RF/IF distinct from decoded audio. InmarsatDiagnostics allowlists only
  bounded numeric/boolean fields remotely. No DSP/codec or P25 changes.
- tests/test_inmarsat_live_gui.cpp links the real widget/engine with a stub-only
  DeviceManager build and no network adapter. GUI cases start with no devices;
  the separate host lifecycle case enumerates only that stub backend. Generated
  test settings are isolated and removed. CMake keeps the executable outside
  portable/deploy assets. CI/release run both test selections.

2026-09-25 / DEC-0122 SDRplay runtime:
- SdrplayRuntime owns serialized API dependency/module registration. Windows
  wide API load checks required exports; read-only SCM query reports service
  state. Soapy loader-result plus find/make registry checks distinguish loaded
  DLLs from accepted drivers. Failure is retryable; success lives until exit.
- SdrplayProfile generates explicit/vendor/portable/conda/plugin-path candidates,
  including per-user registry and Unicode API roots. Removed global constructor
  and broad PATH mutation; existing model/capability/gain math unchanged.
- DeviceManager setup and SDRplay-only pre-open module load use the same loader.
  Discovery of physical devices remains in the existing enumeration flow.
  No changes to stream samples, tuning, P25, audio or analog demodulation.
- sdrplay_runtime_probe and isolated fake API/modules exercise loader errors,
  retry, concurrency and path layouts. Test DLLs/executable live under
  build/test-fixtures, not deployment. Windows CI runs the five CTest scenarios.
- verify_no_p25_changes accepts only the exact reviewed whole-file digest pair;
  extra RF changes remain rejected. Release verifier blocks fixture leakage.

2026-09-24 / DEC-0121 native Aero:
- external/aero pins MIT JAERO/JFFT, BSD libcorrect, ISC/MIT mini-m codec.
  Qt6 adaptation, per-instance modem scratch, strict CRC and bounded unpacking.
- AeroCodec C ABI isolates AMBE4800x3600 in its own DLL. Exact upstream 6x24
  word mapping; per-stream parameters/PRNG; ECC scratch per call. P25 untouched.
- InmarsatAero owns modem/framer/vocoder on one worker, paced by samples. It
  emits CRC-validated assignments/messages and framed 8 kHz PCM.
- InmarsatAdsc checks complete ARINC .ADS CRC, bounded tags and signed position
  fields; rejects unknown/truncated groups and never promotes route waypoints.
- InmarsatPipeline shares channelizer/native chain between live and replay.
- InmarsatAudio has isolated default miniaudio playback, power-of-two SPSC queue,
  bounded WAV writer and explicit queue/drop/zero-fill reporting.
- InmarsatMapWidget uses bundled public-domain Natural Earth geometry, no network;
  separate replay state, identity-linked green activity and white last positions.
- InmarsatReplayDialog/Command adds native mode, speaker, WAV and map controls.
  Runtime owns decoder worker; EOF drain bounded, pause/seek discard queued audio.
- prepare_aero_reference/verify_aero_reference tools compare independent input
  across actual GUI/CLI and pacing variants. Native tests cover independent
  ADS-C, noise rejection, chunk parity, concurrent codec state and PCM layout.

The following DEC-0120 entries describe the earlier physical-only release:

2026-09-24 / DEC-0120 Inmarsat replay:
- InmarsatIqFile: bounded QFile reader; 14 complex formats, strict SigMF retune
  boundaries and PCM16/float32 RIFF validation; explicit raw/WAV RF metadata.
- InmarsatPipeline: common worker-owned live/file physical probe and sample-clock
  continuity metrics. No unframed bytes or false voice output. InmarsatEngine
  calls it for live IQ and writes matching local/remote session diagnostics.
- InmarsatReplay: file/DSP owner thread with cancellation, pause and seek mailbox;
  no hardware interaction. Real-time pacing never discards samples to catch up.
- InmarsatReplayDialog/Command: GUI transport and CLI/GUI automation; bounded
  snapshots, plain-text errors, scoped diagnostic consent. Early startup branches
  avoid MainWindow/device/P25 initialization for isolated replay.
- InmarsatDiagnostics: 8 MiB local session cap plus summary, UUID, allowlisted
  remote counters at <=1 progress/5s. InmarsatRemoteDiagnostics is app-only adapter
  to existing HTTPS/auth/budget transport; files/IQ/audio/aircraft data excluded.
- InmarsatVoice: false HAVE_MBELIB capability and incorrect Aero decode disabled.
  Actual mini-m codec/frame/FEC integration remains ISS-0016, not a placeholder win.
- verify_inmarsat_replay.py: actual GUI/CLI fast/paced parity and error exits.
- release.ps1: public HTTPS endpoint validation; packaged diagnostics default off.

2026-09-24 / DEC-0118 CI fixture repair:
- test_verify_release.py builds an independent valid portable fixture, including
  source/executable provenance and SGP4 notices. Repacking helpers preserve ZIP
  uniqueness; negative cases prove missing/tampered metadata cannot reach signing.
- windows-ci.yml runs these cheap checks immediately after Python setup and
  includes sourceCommit/executableSha256 in CI artifact build-info.json.
- release.ps1 uses the identical negative-test gate before configure/build.
  Production verify_release.py, signing keys, updater and DSP remain unchanged.

2026-09-24 / DEC-0117 SSTV RF acquisition:
- SstvRfRouter: independent Demodulator states, pre-speaker data taps, 48 kHz
  conversion, strict known classic VIS selection, bounded header pre-roll and
  pinned route. Rejects ambiguous routes and IQ continuity failures.
- SstvRfLiveSession: worker-only chronological DeviceManager cursor; observes
  main receiver frequency but never changes its settings. Checks device identity,
  tune sequences/rate/center around every read; saves RF identity in the report.
- SstvWindow: separate image format and RF route selectors plus detected-route
  status. MainWindow control API exposes the same route choices.
- SatcomScannerEngine retains catalogue/Doppler routing; SSB SSTV bandwidth now
  matches HfDemod's two-sided BW argument. HfDemod exposes AM pre-squelch tap too.
- test_sstv_rf.cpp and scripts/test_sstv_rf.py exercise modulation, header
  preservation, ambiguous/parity/gap failures and recorded-image RF round trips.

2026-09-24 / DEC-0114..0115 release follow-up:
- SatcomDoppler is a worker-owned, phase-continuous IQ translator. Pass tracking
  retains the arm-time RF center and nominal decoder identity; the planner only
  updates the desired digital offset. Out-of-capture channels are rejected.
- SatcomScannerEngine consumes short IQ and processes decoder data even without
  speaker output. IQ discontinuities/reset requests also reset its oscillator.
- DeviceManager appendIQBlock uses the RX-owned StreamState; no devicesMutex
  lookup under the driver lock. It only publishes to the ring and queue.
- Talkgroup buttons no longer undo identity-based selection with a row index
  after refreshing. Add/edit selects the saved identity. P25TalkgroupPresentation
  holds the unchanged table/label helpers, linked by the app and workspace tests
  without pulling radio runtime globals into GUI tests (DEC-0116).
- release.ps1 adds version/source/executable provenance and versioned release
  notes; verify_release.py checks that provenance and the SGP4 license payload.

2026-09-24 / DEC-0111..0113 receive-chain repairs:
- HfDemod: cached/interpolated sinc resampling, continuous oscillator/output
  clock, recovering blanker and sample-clock AM priming. GUI observes HF gaps.
- Sgp4 wraps pinned external/sgp4; independent fixtures/tests include deep space.
- DeviceManager separates failed/applied tune sequences; DS validates driver
  settings, preserves native tuner bounds, targets selected device and invalidates
  IQ atomically with read publication. Remaining ownership contracts: audit A17.
- P25TalkgroupRegistry scopes metadata by control source/system; stable table
  identities and atomic writes. P25Aliases parsed cache is mutex protected.
  These do not change P25 decoder, security gate, scheduling or vocoder math.
- InmarsatEngine separates UI notification cadence from IQ draining.
- SatcomSignalSelection restricts acquisition to configured RF bounds;
  SatcomScannerEngine uses discriminator blocks for data instead of speaker PCM.
- SstvModes supplies file/live capabilities. MainWindow publishes USB/LSB taps.
  File/stream helper inputs now agree; digital row transport, CRC and bounds are
  explicit. STWN remains an experimental non-interoperable file format.

DEC-0104: FUBAR/website never gets home lat/lon. `publicStatusJson()` and
aircraft HTTP status omit coordinates. Observer set stays GUI/CLI.

DEC-0103 (0.2.74): DeviceManager lease + `retuneWithLease`; Dual Tuner Soapy
calls under `gSoapyLiveIoMutex`; diversity sets `preferredListenDeviceIndex`.
SdrplayProfile discovers API/module paths, does not ship vendor DLLs.
ObserverMapWidget + SatPassPlanner observer JSON; Sgp4 `lookAnglesTeme` /
`dopplerShiftHz`; TleStore WinINet `httpGetUrl`. Satcom/Inmarsat/Aircraft in
SatcomHubWidget (lazy tabs). CLI: observer/tle/satcom/inmarsat/aircraft.
ModeS CPR positive modulo. InmarsatEngine chronological IQ cursor; experimental
status JSON. Tests: test_sdrplay_*, test_satcom*, test_adsb, test_inmarsat.

DEC-0100/0101/0102: P25Aliases is a bounded Qt JSON model/merge/resolve and
atomic storage module with RadioReference CSV talkgroup and site imports.
P25AliasDialog stages imports/manual edits and only commits on Save.
P25TalkgroupRegistry's table renderer loads names independently; alphaTag wins,
otherwise known WACN/System ID+TGID must match. Site aliases annotate control
log `site=` lines and Alpha Tag tooltips. Never modifies registry rows or audio
decisions. MainWindow adds the manager beside Add TG. Alias tests live in the
Qt workspace target and use temporary databases, not the user's files.

DEC-0099: SstvReceiverFeed owns the optional live queue. Attach/detach/finish
serialize against publication; RX only try-locks, and contention becomes an
explicit gap. SstvLiveSession runs receiver validation, bounded stream decode
and atomic image/report saves on the worker. Finish quiesces RX then drains
queued data; cancel or a source discontinuity discards provisional output.
MainWindow captures the main receiver on the GUI thread; its worker never
dereferences MainWindow. Only active manual NFM outside P25 is accepted.
SstvWindow provides Recording/Live NFM, Receive, Finish and save, and Cancel.
tests/test_sstv_live_gui.cpp covers lifecycle and real-recording image parity;
scripts/test_sstv_worker.py can also target extracted-package test executables.
Historical notes below describe earlier isolated stages, now integrated.

DEC-0098: SstvStreamWorker consumes explicit data/idle/EOF callbacks on a
non-GUI thread, validates chronological stream identity, converts, feeds a
128 KiB-capped QProcess stdin queue and drains bounded progress/stderr.
In-stream gaps invalidate provisional output instead of splicing epochs.
Normal EOF returns owned images/metadata only after zero exit and bytewise
preview/RGB-file parity. Temporary RGB filenames are removed from returned
metadata; no disk publication in this layer. Guard kills/reaps on failure.
Independent test uses actual SstvLiveInput plus converter/helper and compares
against converted-file decoding. It is not attached to a live receiver yet.

DEC-0097: SstvRateConverter owns a persistent bundled miniaudio resampler on
one non-real-time worker. Scaled integer rates preserve fractional timing to
0.0001 Hz; output is 48 kHz mono float. Exact 48 kHz bypass is unchanged.
Input <=8192 samples, output bounded at sixfold plus one carried interval.
No gain/speaker filtering, block flushing or artificial EOF tail. Invalid input
invalidates the session until start(rate); owner must reset on every queue gap.
Not wired to RX. Tests include independent fixture export consumed by
scripts/test_sstv_rate.py, plus sample equality across caller chunk sizes.

DEC-0096: src/sstv_backend/src/pcm.rs supplies the pinned decoder's i16 iterator
from buffered file or stdin input. It retains I/O/odd-length/limit errors until
finish(); main.rs rejects the whole session on failure. Output is provisional
until exit zero. Reader storage is fixed, not proportional to capture duration.
Helper --stdin preserves sample order across byte-fragmented writes and flushes
both progressive rows and image metadata. Owner drains both pipes and controls
deadline/cancellation. Rust reader units join CTest when SSTV images are enabled;
scripts/test_sstv_stream.py covers independent recordings and pipe lifecycle.
No C++ RX/GUI caller uses stdin yet; existing file interface remains compatible.

DEC-0095: SstvLiveInput is an isolated, preallocated NFM ingress queue in
sdr_town_decoders. It preserves fractional sample rate and absolute position,
and emits generation-tagged gap events before replacement data after faults.
Producer try_lock never waits on a consumer; capacity is limited by both slots
and sample duration. Rejected blocks are included in discardedSamples; gaps
counts reset operations, which can coalesce into one event. Lifecycle control
requires a quiescent producer. test_sstv_live_input.cpp checks ordering, bad
input, retunes/epochs, both overflow limits, restart and concurrent accounting.
No receiver, P25, speaker, helper or UI path calls this component yet.

DEC-0094: SstvProgress parses bounded progressive helper JSONL, validates row
geometry/uniqueness and completion, retains assembled frames for final RGB parity.
Rust helper --progress exports native scanlines without modifying DSP. Optional
SstvPreview callback is consumed on the file worker. SstvWindow exchanges one
mutex-protected latest image, observed by a 50ms UI timer, no per-row GUI queue.
Failure/cancellation clears provisional pixels. CLI without callback retains its
prior metadata-only helper path. test_sstv_progress.cpp exercises transport edges;
real Qt tests cover full/partial independent recordings and preview/final equality.

DEC-0093: SstvWindow is a nonmodal recorded-audio window using one QThread job
and the same SstvImageFile function as CLI. QProcess stays on that worker;
only queued completion touches widgets. Optional cancellation is checked during
input conversion, helper waits and before publication. Close waits asynchronously;
parent destruction cancels/joins. MainWindow Tools reuses one window instance.
Tests cover busy/close/cancellation/error states and actual-recording pixel parity
using the same window class; scripts/test_sstv_gui.py supplies independent fixtures.

DEC-0092: SstvImageFile.h/.cpp normalize bounded mono audio to LE16 temporary
PCM, launch an app-adjacent helper via argument-array QProcess with deadline/log
limits, validate schema/mode/row/RGB outputs, then save new-directory PNGs/report.
src/sstv_backend is a pinned Cargo helper consuming upstream ImageStart/Row/End
events; duplicate rows, unsupported modes and excess images fail closed.
cmake/SstvBackend.cmake opts Rust image builds in, stages helper and dependency
notices; core VIS builds remain Rust-free. release.ps1 enables image backend and
verify_release.py now requires matching helper/licences. Tests exercise independent
off-air Robot36 and M1 recordings, exact wrapper pixel parity and failure cases.
No P25, demodulator, speaker, GUI live tap or radio timing code changed.

DEC-0091: SstvVis.h/.cpp implement bounded native classic VIS inspection with
sample-clock positions, parity/framing gates and explicit reset. SstvAudioFile.cpp
uses existing miniaudio file decode, mono/rate/duration/size limits. CLI sstv inspect
is offline-only and reports imageDecoded=false. test_sstv_vis.cpp covers protocol
vectors/partitions/rejection; test_sstv_cli.py exercises the executable and optional
independent upstream audio. No receive registry image capability or live tap added.

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

# 0.2.98 packaging/preset repair (DEC-0132)

scripts/build_sdrplay_module.ps1 builds pinned MIT SoapySDRPlay3 against local
Soapy and hash-verified API development files; CI requires it in the ZIP.
SdrplayProfile adds the full-DLL override used by InmarScope. InmarsatBandPlan
no longer invents fallback frequencies. Five survey JSONs retain exact Hz/rates;
test_inmarsat_bandplans.py gates source and staged copies. The GUI regression
checks 1546.0625 MHz display, 10500 bit/s and the activated engine settings.
# 0.2.99 rate preview (DEC-0133)

InmarsatWidget connects only QComboBox::activated to survey-frequency preview.
No engine call occurs until existing Tune/Start actions; restoration remains
side-effect-free. No-match modes preserve frequency and expose a status label.
test_inmarsat_live_gui iterates all five plans and seven mode/rate choices,
checks preview/commit separation and preservation of an existing matching center.
