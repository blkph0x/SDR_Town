# Code notes (tree map)

DEC-0209: `resolveListenDevice` matches `stableKey` and returns `size_t(-1)`
on missing or duplicate identity. `MainWindow::stopUnusedListenDevices`
stops other Listen-owned streams. `HfAudioFilter` is analog playback only
(200–2800 Hz, blend in/out over 10 ms) and is skipped for P25 voice/control.
`removeSpectrumDc` edits the spectrum thread's private copy. Display FFT
bins / auto levels / marker live in `SpectrumWidget`. Diagnostics
`qScopeGuard` stops the collector thread before `QApplication` teardown.

DEC-0208: `DriverIoMutexTable` keys FIFO locks by USB `stableKey`.
`Device::make`/`unmake` stay on `gSoapyFactoryMutex`. Live
read/write/tune/gain use `SoapyDeviceIoLock`. `AudioEngine` opens WASAPI
stereo 48 kHz, 20 ms period, duplicates mono into every callback channel,
and starts playback after two periods (min 40 ms) or on a test tone.
`[audioengine]` and `[satcom][log][ownership]` lock it. No P25 DSP.

DEC-0207: `DriverIoMutex::try_lock` does not take a FIFO ticket on failure.
`stopTx` uses `try_to_lock`; busy means leak-and-return, not `lock()`.
Leaked TX handles live in a file-static list and are `unmake`d when a later
stop wins the mutex. `writeStream` re-checks `soapyDev`/`txStream` under the
lock. `test_tx_safety` wedges `writeStream` and requires stop < 2 s.

DEC-0206: `SdrTownControlServer::Config.allowUnauthenticated` defaults false.
`GuiRuntimeConfig.controlAuthRequired` defaults true. `start()` trims the
token and refuses an empty one unless explicitly opted in.
`requestAuthorized` never treats an empty token as success; Bearer and
`x-sdrtown-token` are compared in constant time. `/v1/health` remains open.
`test_control_server` covers default refuse, empty/wrong token 401, header
token, health, and the opt-in path.

DEC-0205: `SatcomScannerEngine` constructor always sets `config_.autoCapture =
false` after load, including the default hub singleton. Saved true no longer
arms RF at process start. `SatcomScannerWidget` checkbox is the session arm.

DEC-0204: `P25FollowSnapshot.holdEncryptedForIqCapture` skips `ReturnEncrypted`
so IQ capture can dwell on encrypted traffic. `MainWindow` Tools/P25 button
arms live IQ + encrypted-grant follow; speaker gates unchanged.

DEC-0203: `p25Phase2PlanLiveHotSearch` in `P25VoiceTiming` classifies the next
live hot search. `MainWindowP25Voice.cpp` applies the plan; first locked-lattice
empty hop stays 80/4, true lost-eye still 16/120 on streak 1.

ISS-0080 / capture 093930: no timing/PLC/security change. Remaining speaker
holes are PTT/retune first-eye, RF/MAC lattice (`gaps=1`), and one vocoder
zero (seq=454). `p25Phase2SpeakerAudioForQueue` ordinal skip explains audit
`audio_output_underpush` when `decoded` > `pushed` at ~72% ring fill.

DEC-0202: no timing/PLC change after live 090937. Residual two 20 ms zeros
are `gaps=1` lattice misses on hops that already emitted `fed=emitPcm`.

DEC-0201: `P25VoiceTiming` keeps the DEC-0200 240 ms maximum / 280 ms
overlap, adds an active-clear-only 200 ms minFresh, and preserves the
non-active 160 ms minimum. `tests/test_p25_voice_timing.cpp` compiles both
branches; `verify_p25_phase2_realtime_catchup_geometry.py` also guards the
spectrum worker and forbids the removed 360 ms realtime profile.

DEC-0200: removed `kP25Phase2VoiceDecodeSpeakerRealtimeCatchUp*`. Active-clear
and non-active speaker backlog catch-up returned to 240/160/280; DEC-0201
subsequently raises only the active minFresh to 200 ms. Spectrum `spectrumThread`
from DEC-0199 remains.

DEC-0199: DeviceManager runs spectrum FFT/publication on a dedicated joined
`spectrumThread` that copies from the IQ ring; `rxThreadFunc` only drains
`readStream` / appends IQ / handles overflow and retune. (Realtime catch-up
constants were added here then removed by DEC-0200 after live AMBE collapse.)
Planner coverage lives in `tests/test_p25_voice_timing.cpp`; hardware-loss tests
also require spectrum publication after start.

DEC-0198: Classic Aero reassembly accepts both ACARS block-ID alphabets. The
8400 evidence wrapper treats zero-valid-unit C-frames and codec failures as
erasures without resetting persistent state; M/E/T words are silenced before
the PCM sink. Inmarsat channelizer conversion uses symmetric signed headroom.
The scheduler and ParserISU acceptance policy remain unchanged pending protocol
fixtures. P25 paths are untouched.

DEC-0197: InmarsatMapWidget renders validated clockwise ground track and neutral
unknown-direction markers; AdsBTrackStore publishes trackValid. AircraftMapWidget
labels IQ rate in MS/s, defaults new settings to2.4 and publishes applied device
readback separately. InmarsatWatchSpectrum's explicit off-capture browse clips
waterfall source/destination spans to received RF; navigation cannot retune.
AdsBTrackStore routes geometric-altitude TC20-22 to existing CPR, TC19 to velocity.
Watch scheduling, ACARS direction/CRC/identity gating and P25 remain unchanged.

DEC-0194: P25PipelineTrace is the bounded passive capture-event queue and CC
acquisition identity contract. MainWindow captures grant decisions, CC worker
identity/timing and writes JSONL; MainWindowP25Voice captures queue/decode/
publication/PCM outcomes; orchestration accounts for aggregate idle top-ups.
DeviceManager's explicit current-tuning recent view honors the retune floor
without changing the default recording view; GUI/CLI CC both use it. Startup
and PPM success publish applied tuning metadata. P25VoiceDecode no longer
invokes alternate synthesis solely because passive logging is enabled.
See P25_TIMELINE_AUDIT_20261005.md and the new trace/timeline/neutrality tests.
No speaker timing, FEC, slot/encryption policy or normal synthesis changed.

DEC-0193: P25TrafficChannelProcessor confirms teardown only after two distinct
CRC-valid selected FACCH END_PTTs, separated by at least one180-dibit burst.
New selected PTT/ACTIVE/masked voice clears the proof. Allocation reuse now
matches session/TG/carrier/slot. GUI and CLI pass confirmation/session to
P25FollowStateMachine; matching current call plus expired existing observer/
speaker holds permits ReturnCallEnded before aggregate-structure silence
heuristics. GUI skips warm standby only on this proof and logs the cause.
No new timeout, decoder/vocoder behavior, security release or PCM path.
Exact protected-file hashes and negative entrypoint tests bound the exception.

DEC-0192: P25TrafficChannelProcessor is an observational follow-health owner,
not a second vocoder. It folds selected-slot/TG bursts in capture order under
an observation mutex, uses absolute positions to reject replayed overlap,
keeps the first end time until a selected restart, and clears pre-boundary
security evidence. p2vcw now means fresh masked selected VCWs as GUI/CLI
already expect; p2AllSlotVcw retains the raw aggregate for diagnostics.
This does not change speaker gates, timeout constants or sample processing.
The main GUI intentionally suspends CC decoding while one-radio traffic is
offset from CC, including some in-passband cases; ISS-0073 records an actual
grant missed there. Do not remove that scheduling gate without concurrent
CC/voice CPU, stale-retune, slot-isolation and continuity acceptance.

DEC-0190: DriverIoMutex is a FIFO BasicLockable ticket queue using standard
mutex/condition_variable. DeviceManager retains every existing live-driver
critical section and timeout but prevents a read loop barging ahead of queued
startup/control operations. Existing rxLiveIoWaitUs still measures that wait.
This does not parallelize devices or recover a blocked native call. Frozen-path
guard tests verify the exact include/global/23 lock-type changes, not a wildcard.

DEC-0189: WorkflowSessionId validates normalized ASCII session keys.
SatcomScannerEngine owns named SatPassPlanner/config/audio/log/decoder state;
QObject-bound old-lease cleanup and stopAll join before host-service teardown.
SatPassPlanner persists named observer/catalogue atomically and invalidates
network completion ownership on destruction. SatcomScannerWidget controls its
engine; hidden named pass timers continue, Stop disarms, saved sessions start
idle. SatCatalogueDialog uses that planner. AdsBTrackStore/AircraftMapWidget
own named tracks/CPR/network settings and worker/selected source; hide affects
rendering only. SatcomHubWidget manages named tabs and shared GUI/API routes.
MainWindow changes only exact route forwarding and global workflow teardown.
SatcomAsyncLog serializes bounded POD copying, counts contention/overflow and
formats/writes/drains off the radio producer. No protocol/DSP math changes.
P25 extraction dependencies and next tests are in WORKFLOW_DEVICES.md.

DEC-0187: InmarsatEngine is an explicitly owned QObject. Empty-ID instance()
preserves the original API; named engines have separate config files, IQ
cursors, decoder/watch workers, stores and lease tokens. stopAll joins the
registry before shared host teardown. InmarsatWidget/WatchUi/MonitorWidget and
TrackingPanel use the owning engine/store. SatcomHub opens/saves/closes named
tabs and handles authenticated session commands; default CLI/API unchanged.
DiagnosticRecording refuses concurrent-engine ambiguity; local open diagnostics
record session and source key. The P25 guard permits only exact shutdown and
session-route forwarding changes in MainWindow.

DEC-0188: vcpkg_tooling.py exports configured Git-committed scripts/triplets/
bootstrap/helper recipes and installed helper files verified against SPDX.
Package inventory policy7 requires the nested kit and its source/hash manifest.
Tool bootstrap downloads, compiler/SDK and independent runtime rebuild remain
explicit prerequisites, not falsely bundled.

DEC-0185: SstvWindow owns its worker, selected radio and named settings namespace.
MainWindow::ensureSstvWindow reuses a case-normalized ID; GUI and authenticated
local API address the same instances. Additional CW/DTMF windows reuse existing
per-window workers and source pickers. Hide is presentation only; explicit
Close/Escape/Stop own teardown. SatcomHub no longer stops engines when hidden.
MainWindow shutdown explicitly joins Inmarsat before clearing shared host
services. SSTV IDs use strict PCRE string anchors, not line-end matching.
P25 and Satcom singleton-engine replication remains unimplemented; Inmarsat is
now independently instantiated by DEC-0187.

DEC-0186: scripts/project_sources.py archives committed Git objects plus three
pinned external submodules, omitting reference-only _codex_refs. Staging and
inventory verification require the bounded nested source ZIP with member hashes
and source revision checks. Untracked captures/keys are not copied. Qt/vcpkg/
compiler kits and combined-distribution review remain separate requirements.

DEC-0183: RepeaterMonitor's identity helper binds hardware-affecting dual-watch
and shared repeater-panel status to the actual primary Receiver. The shared
GUI worker snapshots that identity before filtering inactive receivers; another
VFO, even on the same SDR, is never promoted by active-list position. DTMF and
tone observations remain per receiver. The exact controller-only orchestration
delta is separately guarded; P25 decode/audio code is not changed.

T-0103 / DEC-0182: `DeviceManager::resolveWorkflowDevice` implements explicit
key/no-fallback selection and automatic reservation priority. Capture setup and
confirmed restoration require the exact session token. Inmarsat/Satcom carry
tokens through tune/start/restore and fail on ownership loss; host callbacks
carry the same token. `ReceiverTakeoverSessions` separates GUI pause state by
radio/session and ignores old completions. `WorkflowDeviceCombo` preserves
missing identities. Aircraft GUI/CLI use `WorkflowRadioSession`; only its worker
owns the selected IQ cursor and teardown. Ready and opening are distinct states.
`ReceiverSourcePicker` retains logical receiver identity across vector reorder;
CW/DTMF/SSTV and RDS/tone observers share it. MainWindow's table now reflects
real receivers and selected-row removal. Web source setters go through the same
controls and status exposes workflow reservations/runtime. Existing P25 controller,
DSP/audio, primary repeater and diversity processing are not replaced. Remaining
raw administrative mutations and repeatable workflow engines are still T-0103.

T-0103 / DEC-0181: `DeviceOwnership.h` is the serialized control-plane policy
(stable-key reservations, per-endpoint/client leases, generations, shared domains,
teardown exclusion and bounded persistence). `DeviceManager` owns its mutex,
atomic assignment save/load, scoped commands and legacy per-device adapters.
No driver teardown occurs while holding the ownership mutex. `WorkflowRadioSession`
is a generic worker-only RAII adapter for any RF workflow; it validates the exact
selected key again at claim time, rejects stub startup and uses scoped cleanup.
`WorkflowDevicesWindow` is callback-injected Qt presentation; the manager rechecks
active-stream conflicts at save. `SstvWindow`/`SstvRfLiveSession` provide the first
dedicated UI adapter, with existing main-receiver tap retained. MainWindow P25
source allocation respects reservations; P25 decode/audio kernels are untouched.
Policy/widget/five-mock-radio tests cover ownership, not decoded RF acceptance.
The complete adapter/instance migration is **not** done: see WORKFLOW_DEVICES.md.

T-0104 / DEC-0178: qt_sources.py explicitly fetches only reviewed SHA256-pinned
Qt module archives, preserves complete sources, catalogs license/attribution
files and records six SDK configuration files. Streaming tar inspection has
entry/expanded-byte bounds and never extracts during verification. A nested
stored ZIP is rechecked against pins/catalogs, not just its own hash manifest.
msvc_materials.py reads native VERSIONINFO, validates Authenticode through a
fixed PowerShell helper and reads the signed Burn manifest/license with bounded
7-Zip stdout; it never executes the installer. Version mismatch fails closed.
runtime_signature.ps1 explicitly loads its own PowerShell Security module to
avoid inherited PowerShell7/Windows PowerShell module-path incompatibility.

test_qt_replacement.py uses the configured x64 MSVC environment and Ninja.
DEC-0179 adds --full to build qtbase+qtsvg from the verified kit, replace every
packaged Qt DLL/plugin, exercise widget/SVG/image/icon pixels, loopback HTTP,
native TLS backend, actual CLI and four no-RX GUI profiles. Without --full the
original narrow Svg-only smoke remains available and cannot claim full rebuild.
Bundled dependencies are selected and resolved features checked; a package with
qopensslbackend requires explicit --openssl-root headers, hashed in evidence,
with linked OpenSSL forbidden. No OpenSSL runtime is imported for the test.
--work-root permits short paths on a spacious disk; only validated fresh QA
directories are cleaned. Command deadlines terminate that process tree. Results
record failures instead of leaving stale successes, input/replacement hashes and
SDK feature differences. Source SDK and original package remain unchanged.
test_qt_replacement_harness.py covers missing plugins, extraction containment,
feature checks, failure receipts and command failures without compiling Qt.
DEC-0180 adds --headless-layout for CI: native Windows probe/CLI/listening
startup, then all four exact-size layouts using a source-built QA-only
qoffscreen plugin and installed Windows fonts. The real package never gains
that plugin. Desktop metrics, backend and QA plugin hash are explicit evidence.
test_workspace_gui.py defaults to all profiles; --only-profile supports the
additional native smoke without removing any full-suite assertion.
Policy T-0104-notices-5 requires both new material sets whenever those runtimes
are shipped. Remaining publication blockers are narrowed, not bypassed.

T-0104 / DEC-0177: scripts/embedded_notices.py inspects the explicit libacars
ASN.1 source subtree, preserves leading copyright headers, accounts for generated
files and three hash-reviewed upstream headerless files, and records selected
country/map/bandplan source hashes. package_inventory.stage_notices stages its
bounded, deterministic evidence plus exact data/dedication copies. make_document
reconstructs ASN1-NOTICES.txt and cross-checks packaged data/notice bytes against
that evidence even if the outer inventory is regenerated. It is not a complete
SBOM, linker map, independent source provenance signature or rebuild kit.
resources/icao retains the pinned dataset dedication/provenance; no aircraft,
map, decoder or DSP code changed. test_embedded_notices covers actual source
coverage, malformed/unknown/linked inputs, resource limits, exact copies and
tampering; test_package_inventory checks the same constraints in a real ZIP.

T-0104 / DEC-0175: vcpkg_sources.py collects only installed-receipt-named port
files and SHA512-matching local archives/downloaded Git-format backport patches;
deterministic bounded ZIP, atomic replacement, no network/extraction/execution.
package_inventory.py independently
validates nested membership/hashes and installed binary receipts. Runtime CMake
config carries the explicit vcpkg root. CI disables dependency binary-cache
reuse so exact sources are available. Tests cover tamper, missing/unsafe/linked
inputs, failed replacement and binary/receipt mismatch. This is not full rebuild
or transitive-license clearance.

T-0107 / DEC-0176: main.cpp publishes parsed dryRun as the process-local
sdrtown.guiDryRun Qt property before constructing widgets. SatcomHubWidget
snapshots it, suppresses automatic timer startup/ticks and logs once. Saved
auto-capture settings and normal operation are unchanged. Actual GUI harness
tests both flag spellings, suppression evidence and absence of transient RX.
No MainWindow/P25/DeviceManager/DSP source or guard exception changed.

T-0104 / DEC-0174: cmake/StageRuntime.cmake declares current target/module and
configured dependency/tool paths in runtime-inputs-Release.json. stage_runtime.py
validates all inputs before bounded staging cleanup, copies only declared or
recognized dependencies, and owns common Qt/MSVC deployment plus runtime hashes.
build_rtl_module.ps1 and build_sdrplay_module.ps1 build pinned sources without
overwriting local edits. CI and release.ps1 use the same path; CPack never falls
back to bin/Release. package_inventory.py requires matching runtime evidence.
test_stage_runtime.py covers failed-input preservation, destination bounds,
stale-file isolation, linked paths and configured tools; GUI smoke opts out of
remote diagnostics. No receive, decoder, control, vocoder or audio-source edits.

T-0106 / DEC-0173: SdrTownControlServer dispatches readyRead and prebuffered
requests through guarded queued callbacks on its own Qt thread. Application
handlers can stop/delete clients without destroying a socket inside its native
read notification. Existing handled flag retires queued work across restart;
QPointer also covers already-deleted clients. test_control_server covers native
notification ordering, reentrant cancellation/throw and both retirement cases.
No new thread, deadline, decoder or audio-path change.

T-0104 / DEC-0172: scripts/package_inventory.py stages notices/input identity,
builds the exact-file record and verifies bounded ZIP contents without extraction.
Publication blockers are recomputed, not trusted from generated metadata.
CMake stage_rtl_runtime copies configured RTL plus libusb. test_rtl_runtime_package.py
uses fresh restricted Windows loader children and a missing-USB negative fixture,
never hardware enumeration. Windows CI, release.ps1, VerifyPackageGate.cmake and
verify_release.py enforce publication; blocked CI publishes the inventory only.
This is not a complete source kit/transitive SBOM. No src/include DSP edits.

T-0102 / DEC-0171: DiagnosticsHealthMonitor owns GUI monotonic/session-aware
health scheduling; RemoteDiagnostics owns bounded status transport and atomic
callback epochs. SdrTownControlServer owns accepted-client lifetimes, strict
single-request framing, error redaction and numeric counters. DeviceManager
publishes hardware-overflow epoch/floor before new samples and owns cumulative
driver wait/read/error counters; no per-sample logging. startToneTx validates
explicit authorization and real driver configuration before activation; CLI
defaults to file-only. HfDemod::tryDiagnostics never creates state or blocks DSP;
MainWindow/AppBootstrap publish bounded numeric snapshots under existing consent.
Tests: test_control_server, test_remote_diagnostics, test_hardware_loss,
test_tx_safety and test_hf_demod. Shared-file P25 guard is exact-digest only.
Global device leasing, driver-hang recovery and binary inventory are still open.

T-0099 / DEC-0168: DtmfDecoder owns per-stream analysis and sample-time debounce;
queued options, bounded events and published history share a mutex. DtmfReport
is the bounded JSON contract used by CLI and local GUI export. DtmfWindow owns
only observer settings/snapshots and a cancellable file worker. The MainWindow
menu hook aliases the existing receiver-owned decoders without controlling RF.
The exact orchestration edit removes skipped input-watch blocks only inside
opt-in NFM dual-watch. RepeaterControlHooks preserves each event's original RF
identity when reset completes a sequence. No changes to Demod/P25/AudioEngine.

T-0098 / DEC-0167: HfDemod owns invalid-input rejection, identity-aware NCO
continuity and per-owner numeric diagnostics; existing filter/AGC constants
remain. CwDecoder validates bounded audio, filtered-resamples to4 kHz and owns
one pinned GGMorse recognizer. CwSession streams recorded audio; CwRfSession
observes chronological IQ with independent demod/cursor and no control writes.
CwWindow owns cancellable worker/snapshot mailbox, local transcript/settings
and UI; exact MainWindow menu hook only. External provenance and license ship.

T-0097 / DEC-0166: WorkspaceLayout owns the visible preset selector and panel
menu. MainWindow only reparents the existing repeater widget; the P25 guard
allows this exact insertion and rejects any additional protected edit.
InmarsatTrackingPanel::webReport owns the web snapshot success flag required
by SdrTownControl.dll. Native satellite widgets inherit the application palette.

T-0095 / DEC-0164: InmarsatIdentity.h provides validated-Classic-Aero address
formatting and C-identity publication. Native Aero supplies ICAO on ACARS and
assignments; explicit ADS-C mismatches still reject coordinates. MessageStore
records monotonic identity/position receipt times and typed earth-reference
motion paired to its position. No decoder or speaker eligibility change.
InmarsatTracking owns source selection, age/TTL, bounded estimates and activity
association. InmarsatOnlineLookup owns one cancellable Qt HTTPS reply, strict
provider validation/body/depth/deadline/backoff and fair received-ID batches.
InmarsatTrackingPanel owns consent, live snapshots, lifecycle and bounded local
evidence; numeric counters join the existing opt-in diagnostic allowlist.
InmarsatMapWidget keeps the shared offline/replay renderer, adds source/estimate
presentation and independently labels selected playback vs received activity.
Recording before/after snapshots and collector validation retain reviewed-only
voice identity context. No aircraft data in ordinary automatic telemetry.

T-0094 / DEC-0163: InmarsatAero counts ACARS block-ID direction and valid ADS-C
applications; existing accepted position count is preserved through pipeline
resets into `positionReports`. InmarsatDiagnostics allows only numeric counters
through the existing opt-in route. DiagnosticRecording stores live before/after
counts, with backward-compatible collector allowlisting. ReplayCommand exposes
the same cold-start counters; the map reports messages without coordinates.
No new inferred coordinates, raw-message telemetry, framing or voice decisions.

T-0094 / DEC-0163: remote_diag_server.RECORDINGS_PER_DAY controls the durable
per-installation rolling allowance, advertised by /health for deployment checks.
InmarsatRecordingDialog describes 15 and handles HTTP 429 distinctly, retaining
the recording for manual save/retry. Authentication and other quotas unchanged.

T-0092 / DEC-0162: InmarsatAcarsApplication dispatches bounded libacars v2.2.1
applications with explicit direction, statuses and structured ADS-C fields.
InmarsatAero supplies ACARS block-ID direction and downlink-header evidence.
InmarsatMessageStore also supports isolated per-pipeline replay stores;
positions() merges identity without refreshing coordinates. Map includes
explicit ICAO. See INMARSAT_APPLICATION_VOICE_AUDIT_20260927.md for full path,
reference results and remaining application/voice acceptance gates.

T-0092 / DEC-0161: InmarsatAcarsApplication is the bounded, serialized adapter to
external/acars (pinned libacars MIAM CORE). InmarsatAero adds application metadata
without changing raw ACARS/ADS-C input; Pipeline accumulates interpretation
counters and Diagnostics shares numeric counts only. The reference probe accepts
submitted recording bundles, writes only new PCM outputs and reports actual
messages. Private field recordings stay outside Git.

AircraftReceivePlan qualifies capture rates/IF independently of analog filter
bandwidth. AircraftMapWidget applies controls under the existing lease and reads
chronological IQ with AircraftMagnitudeStream's bounded packet tail. The new
internet checkbox persists through AdsBTrackStore; RF/network maps are separate,
and generation checks reject stale network replies. Tests cover 20 Msps packets,
block/epoch boundaries, source preservation, off/on races, malformed JSON, MIAM
ack/data/CRC/DEFLATE and actual Qt controls/compact layout.

T-0091 / DEC-0160: DiagnosticsObserver owns GUI-thread read-only actions and
periodic snapshots. DiagnosticsMenu invokes the existing MainWindow diagnostic
slot through Qt metadata, never opens a radio/audio engine. AppBootstrap cached
device inventory adds SDRplay/RTL settings, tune sequencing and a non-consuming
single-sample cursor read (sample discarded). Existing MainWindow snapshot adds
audio health; old health timers now check opt-out. No P25 algorithms changed.
RemoteDiagnostics has bounded pre-Qt ingress, coalescing and priority; all data
obeys the same byte ceiling. ProcessPerformance provides identifier-free PC specs.
Collector indexes events separately from issue grouping, authenticates history,
backfills bounded legacy data and applies retention. Tests cover consent, floods,
privacy, migration, authorization, pagination and HTML escaping.

T-0086 / DEC-0158: SstvWindow remembers the save root and generates unique session
paths. SstvStreamWorker's optional archive directory selects continuous helper
stdin mode and completion callbacks in SstvProgress. RGB pixels/revision are
verified before atomic PNG/JSON saves; temporary RGB is removed, metadata capped
at 64. SstvLiveSession/SstvRfLiveSession no longer impose a session duration.
Explicit-path automation and bounded file decoding remain compatible. Tests
cover parser bounds, save-root persistence, repeat sessions, rolling filenames,
and optional [.sstv-archive-recording] repeated real recordings beyond old limits.

T-0085: tests/test_sstv_virtual_sdr.cpp registers a paced Soapy fixture and calls
production decodeSstvRfLive. scripts/test_sstv_virtual_sdr.py prepares arbitrary
short WAV fixtures and retains logs/images locally. See SSTV_VIRTUAL_SDR.md.

T-0084: InmarsatVoiceEvidence holds the native validated-SU identity update and
C-frame transaction, shared with injected codec-failure tests. Pipeline rejection
counters are cumulative across receiver resets; lastInputRejected identifies the
last block while lastError retains the last reason. InmarsatWatchFocus requires
identified current speech. InmarsatDiagnostics allowlists numeric failure metrics
for both single pipelines and watch workers. See INMARSAT_RECOVERY_AUDIT.md.

T-0083: InmarsatDiagnosticRecording holds one explicitly armed, bounded modem IF
and PCM capture. InmarsatPipeline feeds it at the existing modem boundary;
InmarsatRecordingDialog owns consent/review/save/HTTPS submission. No recorder
network or disk I/O runs on the decoder worker. InmarsatReplayCommand supports
diagnostic_if cold-start reproduction. remote_diag_server.py validates bundles
and maintains durable upload quotas, storage bounds and expiry.

## Inmarsat source identity (DEC-0154)

InmarsatAudio::push accepts AES and preserves unknown as zero. Source changes
discard queued speaker data and update constant-size local diagnostics. Engine
and Replay forward callback AES, including watch sessions. Remote allowlist
exports transition/unknown counts, not identities/offsets. InmarsatMapWidget
shows unlocated/unknown voice and validates integer AES range before matching.

## Candidate WFM RF-to-RDS qualification (DEC-0153)

test_fm_benchmark.cpp [rds-rf] remodulates recorded MPX, compares current,
double-filter and direct-discriminator data branches through shipped RDS DSP.
[power] compares stationary full/retained FIR power. Both are mandatory when
their backend is enabled; no production route is selected by these tests.

## Retained WFM FIR prototype (DEC-0152)

tests/WfmRetainedFirPrototype.h is test support only: persistent causal FIR
evaluated on retained positions, resettable history and phase. Mandatory
[wfm][retained] fixtures compare exact convolution and multiplex/PCM partitions;
optional [.wfm-retained-benchmark] measures the cascade through Demodulator.

## Independent WFM filter oracle (DEC-0151)

scripts/analyze_wfm_filter.py: NumPy-only offline coefficient, causal convolution,
pre-decimation IQ leakage and delayed-ideal discriminator comparison. No app
mutation. scripts/test_wfm_filter_oracle.py verifies convolution and scaling.
Report embeds source/dirty state, script hash and NumPy version.

## WFM image characterization (DEC-0150)

tests/test_fm_benchmark.cpp adds opt-in [.wfm-image-sweep] through the real
Demodulator. scripts/benchmark_fm.py --wfm-sweep validates a 72-case JSON matrix;
scripts/test_benchmark_fm.py covers missing/duplicate/nonfinite/invalid reports.
No production filter, default or P25 path changes.

## NFM input anti-aliasing (DEC-0148)

NfmInputDecimator designs16*M+1 normalized Kaiser taps at cutoff0.25/M using
the existing windowed-sinc design pattern. Persistent ring input history and
phase emit at M-1,2M-1,...; dot products are computed only for retained samples.
Reset clears history/phase; factor changes rebuild coefficients. Applied only
above300kS/s, preserving the existing rate plan and downstream NFM FIR/tone tap.
maxFirDelayUs now includes both first-stage and speech-filter causal delay.
Reference convolution/partition and response/blocker fixtures are release gates.

## WFM FIR computation (DEC-0147)

WfmSpeechFir retains raw causal history and contiguous scratch input. SSE2
computes four complex outputs per tap loop using two float vectors, preserving
ordered multiply/add without FMA. Scalar fallback handles tails and non-SSE2
builds. Every full-rate sample remains available to the existing power estimate.
Reset and tap-length changes zero history; same-length coefficient changes
preserve it as before. No change to NFM's ring FIR or independent RDS branch.

## FM benchmark (DEC-0146)

tests/test_fm_benchmark.cpp adds a default measurement-math test and opt-in
[.fm-benchmark] matrix using actual Demodulator calls. scripts/benchmark_fm.py
validates36 cases/run, records provenance and writes a local JSON report.
scripts/test_benchmark_fm.py covers parser negative cases. No device, application
setting or production DSP changes; timing is a measurement, not a CI threshold.

## WFM speech timing (DEC-0145)

Separate WFM FIR ring/decimator phase and cubic clock now preserve stream time.
Uses existing causal FIR helper and NfmPcmClock polynomial with independent state;
the helper name reflects its original owner, not a shared audio history. RDS
retains its independent branch. Source rate/center/reset clears speech history;
deferred PCM reset clears de-emphasis/notch/final filters even after empty output.
Bounded FM maxFirDelayUs/maxPcmDelayUs/hintMismatchBlocks expose causal latency
and count differences; LookaheadReads/PhaseRepairs must stay zero on this path.

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

## Station front-end (DEC-0210 / T-0123)

`include/frontend` owns LNB IF math, claimed noise figure, Bias-T state,
rotator tick planning, pass arm/teardown order, JSONL metrics, station
profile JSON, and a D1 FFT survey. `StationPassSession` sends those ticks
through one `RotatorController` and records commanded, reported, and
error angles. Abort stops, then moves to the saved park angles, then
commands Bias-T off. `inventoryClearTransportStream` lists PIDs from
caller-supplied TS bytes and counts scrambled packets without reading
their payload. `EquipmentWizard` is the Tools panel.
`installAntennaControlMenu` also installs that panel so `MainWindow.cpp`
stays untouched. Lease and IF boxes are attestations, not device leases.
Read/Follow armed pass uses `skyFeedFromPass`. Azimuth comes from the
planner position list because the armed record stores elevation and
Doppler only. `PlannerCapture.cpp` is linked into the application and
not into the station tests, so those tests stay on the fail-closed
default. Save pass log writes the profile JSON and metric JSONL. GEO park commands
one look. GEO box scan uses `planBoxScan` and stops at 49 dwells with no
peak claim. Manual does not call `moveTo`. `leadSky` aims one observed step ahead
on the short azimuth path. The wizard mission combo is stored on the
profile. Horizontal and high-band checkboxes select 18 V and 22 kHz.
`sharedRotatorController` is the one process client for the Tools window
and the station panel. A second `connectTo` is refused while it is open. `StationRadioBind`
queues the IF through `retuneWithLease` as Satcom, without force, and
requests RTL or SDRplay Bias-T for the internal backend. Tests install
their own sink. `detectDvbs2PlHeader` correlates SOF phase steps, estimates
one radians-per-symbol offset plus one phase, removes both across the
90-symbol header, and decodes the (64,7) PLS code to MODCOD, frame length,
and pilots. The same offset is not tracked through the payload.
`modulateDvbs2PlHeader` builds that header.
`writeClearTsForPlayback`
writes only an unscrambled transport stream; the wizard Play clear TS button
opens that file with the OS player. LDPC payload demod is not linked.
`StationSupplyBind` writes the external
supply line at 9600 8N1 and treats `OK` as acknowledgement only.
D2 demod and commercial decrypt return false.

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
| `src/AudioEngine.cpp` `include/AudioEngine.h` | P2.4 | miniaudio | Ring, underrun, digital-voice jitter cap. DEC-0208: stereo WASAPI, start after two periods |
| `src/DeviceManager.cpp` `include/DeviceManager.h` | follow | Soapy/RTL | IQ stream + one-RTL retune. DEC-0208: per-USB live IO, factory make/unmake |
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
# DEC-0184 / T-0103 - Hardware command ownership

`DeviceOwnership` now owns short-lived control permits as well as long-lived
workflow leases. Permits pin an endpoint and its shared hardware domain through
driver I/O, preventing claim/rebind while allowing the lease mutex to be released.
`DeviceManager::ControlScope` releases the permit and wakes stop/restore waiters;
stop invalidates the lease before draining. Public settings accept an optional
exact token; legacy operator commands cannot borrow a named workflow's token.
Private implementations are used only within an admitted command or the exact
startup generation. `correctWorkflowFrequency` is the transitional legacy-P25
adapter, not a change to its correction math or decoder/audio pipeline.
MainWindow/CLI propagate refused controls; source selection and DSP are separate.
`test_workflow_radio_session` includes a blocked driver call, old-token release,
competing replacement, independent-radio claim and deferred teardown fixture.
# DEC-0196 distribution completion

scripts/distribution_materials.py reviews embedded/Qt/versioned dependency
licences against exact source kits, exports supplemental source/runtime
materials and verifies nested hashes. scripts/rds_toolchain.py installs the
identified GCC14.2/MinGW12 archive without sweeping an ambient compiler.
scripts/test_usb_replacement.py independently rebuilds the USB stack from
exported tooling/recipes and exercises isolated replacement loading.
scripts/qualify_distribution.py binds compact passing Qt/USB results to the
package executable, source archives and runtime set. package_inventory.py
now clears finite blockers only after these checks, retaining original
source/notice/signature/ZIP safety gates. No P25/receive/audio code changes.
