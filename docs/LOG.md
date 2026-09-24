# Development log

## 2026-09-24 - Inmarsat diagnostic release 0.2.91 published

DEC-0120 replay/diagnostics subtask complete: source 698dcd8, release metadata
dac8778, public Latest v0.2.91 with installer, portable, control DLL, hashes and
signed update manifest. All eight remote asset digests/sizes verified. Full local
CTest, extracted-package GUI/CLI paced/fast/failure tests and independent Windows
CI 35993275475 pass. Four replay summaries reached the actual HTTPS collector.
Documented the unsupported formats, opt-in behavior and exact privacy boundary.
P25 and analog demodulators unchanged; FUBAR and other proxy routes unchanged.
ISS-0016 Aero framing/FEC/CRC/codec/assignment/map work is still open, as is
ISS-0017 off-LAN diagnostics qualification. Dad's JAERO-verified IQ/reference
settings remain necessary for RF and clear-voice acceptance. No synthetic test
has been represented as a working Aero voice decoder.

## 2026-09-24 - Inmarsat replay and diagnostics (DEC-0120)

Implemented bounded 14-format IQ reader + SigMF/WAV metadata, shared live/replay
physical pipeline, pause/seek/clock GUI, isolated CLI/GUI automation, diagnostic
logs and allowlisted remote summaries. Incorrect inactive Aero codec removed;
no fabricated voice, assignments or aircraft activity. Protocol/codec/map work
remains open and documented. P25 and analog DSP unchanged.

Configured only an isolated HTTPS proxy include on the authorized VM after backup
and configtest; no FUBAR/other app route edits. Confirmed server receipt for four
actual replay sessions. Fixed remote-counter sanitizer collision and omitted
redundant startup messages that delayed final summaries. Reporting remains opt-in.
Native focused tests, full CTest and actual GUI/CLI parity passed. Preparing
0.2.91 tester assets with an explicit diagnostic-only scope; no RF acceptance claim.

## 2026-09-24 - SSTV 0.2.90 published for GitHub testers

All DEC-0117 source/tests/docs pushed as 401e2e3; signed metadata/tag b8a27dd.
Local and independent Windows CI build/test/package gates passed. Actual portable
GUI/helper/API checks passed without developer runtime paths. Eight assets are
public in Latest v0.2.90; hashes, sizes and downloaded updater manifest verified.
Release notes explain RF Auto/manual controls and live-radio qualification limits.
No SSTV work remains only local. No P25 DSP or FUBAR changes; no captures deleted.
Remote run's pre-existing empty-diff guard limitation is recorded as ISS-0015,
not presented as evidence of P25 invariance. Original dev folder remains canonical.

## 2026-09-24 - SSTV 0.2.90 publication requested

User requires the completed SSTV fixes in GitHub source and downloadable tester
builds. DEC-0119 prepares the version, release notes and pairing documentation;
all DEC-0117 changes are included, not held back behind the CI-only repair.
Recorded RF and full/partial GUI image regressions were repeated successfully.
Release build/package/remote verification is the remaining publication gate.
No P25 DSP changes; on-air SSTV qualification remains open for testers.

Newest at the top.

## 2026-09-24 - GitHub Windows CI failure isolated and repaired

Run 35979813492 failed on stale disposable release-test data, after successful
app/core/Qt/SSTV build gates. Added the missing provenance and SGP4 notices to
the fixture, targeted negative cases and early CI/local-release preflight.
Production verification was not weakened. Local tests pass (BUILD_NOTES).
Repair isolated from uncommitted SSTV RF work; no release version/tag/asset is
changed. Remote run 35984373766 passed all build/test/package/upload stages;
workflow validation is green. Fix 2b615ba is pushed to master. The original
development folder has the fix; all 16 SSTV application/test/doc file blob
hashes checked before/after integration are unchanged. Unpublished SSTV work
is preserved separately, not silently included in this CI repair.

## 2026-09-24 - SSTV RF Auto separated from image-format Auto

Traced file, GUI, API and satellite routes. Existing Auto identified image format,
not USB/LSB/FM. Added independent worker-owned RF auto acquisition and manual
USB/LSB/NFM/AM, validated classic VIS, bounded header retention, explicit ambiguity
and continuity failures, GUI route status and API/report RF identity. Fixed the
SSTV SSB passband mismatch on both main and satellite routes. Main speaker and
P25 processing unchanged; satellite catalogue selection/Doppler remains explicit.
DEC-0117 / ISS-0013 / BUILD_NOTES record builds, tests and remaining limitations.
Off-air RF qualification is still needed. Extended/headerless signals require
manual RF selection; image-format Auto remains available. Changes are local,
built in the original development folder; no new release was requested here.

## 2026-09-24 - 0.2.89 published for remote testing

Fast-forwarded original-folder master to repaired source 560cf85; published
v0.2.89 at asset-metadata commit 3820791. Installer, portable ZIP, versioned
control DLL, individual hashes, SHA256SUMS and signed updater manifest verified
locally and against GitHub's uploaded asset digests. Latest release is 0.2.89,
experimental channel (existing Latest/updater distribution convention).
Packaged GUI and SSTV helper smoke tests pass without development Qt paths.
No Desktop/TEST overwrite, capture deletion, P25 audio-algorithm change or
FUBAR release. Remaining audit gates stay open; work continues in the original
Desktop/maulaudio_pro folder on master.

## 2026-09-24 - 0.2.89 tester release candidate

User authorized code and asset publication. Continued the repair report with
fixed-RF digital Doppler, no short satellite-IQ discard, identity-safe talkgroup
button selection and the DEC-0115 lock-order repair. Presentation helpers moved
unchanged into a testable module. Full CTest 4/4, actual GUI dry-run and sixteen
recorded SSTV parity cases pass (BUILD_NOTES). Release pipeline now records the
reviewed source commit and executable hash in build-info.json and verifies the
SGP4 license payload. Version 0.2.89; signed experimental packaging/publishing
is the next gate. T-0047/ISS-0012 remain open, with limitations in release notes.

## 2026-09-24 - First receive-chain repair batch, not a release

Implemented DEC-0111..0113 in original Desktop folder on
codex/receive-chain-repairs-20260924. HF overload recovery, throughput, AM and
output-clock partition defects have regression tests; SGP4 passes independent
near/deep-space vectors. Device tune failure and DS reporting are hardened.
Talkgroup metadata/selection cannot use TGID or row position alone. Satellite
data taps and bounds, SSTV mode transport, USB/LSB source and digital integrity
checks repaired. Recorded SSTV tests additionally found/fixed file-only filtering.
Build/test evidence in BUILD_NOTES; exact closed/partial/open audit status in
AUDIT_20260924. P25/WFM/NFM audio algorithms unchanged. No user capture deletion,
no tag/push/release. T-0047 and ISS-0012 remain open for the substantive remaining
ownership, Doppler, driver matrix and protocol-qualification work.

## 2026-09-24 - Reconciled GitHub work and audited 0.2.88

Original Desktop/maulaudio_pro source now matches TEST portable a2ac437. Preserved
prior local source and HttpGet change. Removed 15 exact-tree duplicate remote
work branches after local archival/expected-SHA validation; retained unique work,
all backups/releases and PR #32. No executable replacement, hardware retuning,
product algorithm edits or release. Added AUDIT_20260924.md with A01-A17 and
ordered regression-gated next pass; ISS-0012/T-0047 remain open.
Focused smoke/import/SSTV checks passed; new HF and SGP4 diagnostics reproduced
defects, including a merge-blocking PR #32 alias regression. Full build, live
device matrix, audible acceptance and protocol interoperability remain untested.

## 2026-09-20 - SSTV extra modes VIS+sync auto (DEC-0107)

Vendored unexcellent/sstv; added Robot B&W 8/12, Robot 24 luma, SC2-30/60/120,
AVT 24/90/94/188. Auto: VIS then line-sync period. Helper --selftest still
Robot36 complete. [sstv] 9369 assertions PASS.

## 2026-09-20 - SSTV Dayton modes unlocked (DEC-0106)

Helper/CLI/GUI now accept the 18 modes in the pinned crate. VIS names expanded.
Per-line 1200 Hz sync is the slant correction. Helper timeout 540 s. Preview
line limit 16 KiB for 800 px PD290. Unsupported: AVT, Robot 8/12/24, SC2-30/60/120.

## 2026-09-20 - Release 0.2.76 Inmarsat honesty + FUBAR 1.1.39 pair

DEC-0105: Inmarsat voice follow/record disabled; locked always false; no slicer
bytes into AMBE. Remaining gaps listed in docs/INMARSAT.md. FUBAR website matches
Town radio controls without home lat/lon.

## 2026-09-20 - Release 0.2.75

Experimental patch: TLE WinHTTP + UI busy, DEC-0104 home coords omitted from
FUBAR HTTP, T-0041 inmarsat JSON in staging / no leftover 0.2.71 control DLL.

## 2026-09-20 - TLE download hang

Refresh TLE looked idle because refreshUi() (500 ms) overwrote "downloading…".
WinINet InternetOpenUrl also ignored timeouts. Switched httpGetUrl to WinHTTP
with 15 s timeouts; UI keeps busy state until the worker finishes. Live
CelesTrak stations/weather/amateur groups parse ISS 25544.

## 2026-09-20 - DEC-0104 FUBAR must not see/set home lat/lon

Public website no longer proxies observer GET/POST or shows lat/lon. Town
satcom/aircraft HTTP status omits coordinates. Home map stays in SDR Town.
FUBAR satcom Start uses force=true under Take-control.

## 2026-09-20 - FUBAR pairing recorded from both READMEs

`SdrTownControl.dll` is the companion bridge (FUBAR LoadLibrary beside FUBAR.exe).
Published pair on GitHub is FUBAR **1.1.33** + Town **0.2.66** DLL; Town Latest is
**0.2.74**. FUBAR working tree is **1.1.38** unreleased (satcom/aircraft/Inmarsat/
SDRplay website tabs). Documented in docs/FUBAR_PAIRING.md. T-0041 leftover is
the extra 0.2.71 named DLL in the Town ZIP, not the pairing asset itself.

## 2026-09-20 - SoT/tracker reconciliation after v0.2.74

Reconciled SOURCE_OF_TRUTH, TASKS, BACKLOG, ISSUES, CODE_NOTES, CAUSE_EFFECT_MAP
to published v0.2.74 (4f26f2e). Did not flip REQ-P2.2…P2.6. Closed T-0038/T-0040
from release evidence. Opened T-0041 from inspecting the published portable ZIP:
missing `data/inmarsat`, leftover `SdrTownControl-0.2.71-win64.dll`. SDRplay
vendor files remain host-installed by design.

## 2026-09-20 - Tuner lease, home map, Doppler/TLE, CLI coverage

Fixed Dual Tuner Soapy init serialization, diversity listen retarget, Mode-S CPR
southern-hemisphere modulo, blocking TLE QEventLoop, OpenSky/query-string API
gaps, and satcom/Inmarsat stealing a live listen tuner. Observer map click (and
Shift-click on aircraft map) sets home lat/lon for ISS SSTV, SGP4 passes, and
Doppler (TEME→ECEF + Earth rotation). TLE fetch is WinINet off the GUI thread;
`tle load` reads cache files. CLI: observer/tle/satcom/inmarsat/aircraft.
Inmarsat labeled experimental prototype (no unique-word/FEC).

## 2026-09-20 - Full SDRplay multi-model Soapy support

SoapySDRPlay3 profile layer: discovery/PATH for SDRplay API + module, IFGR/RFGR,
AGC, bandwidth, bias-T/notches/extref/HDR settings, RSPduo Single/Dual/Master
(Dual Tuner as two channel rows with shared Soapy device). Device Manager panel,
CLI `sdrplay` commands, docs/SDRPLAY.md, profile unit tests. RTL/HackRF unchanged.

## 2026-09-19 - Alias cache hotfix for P25 audio

Control-log site labels stopped doing a full `p25_aliases.json` reparse per
event (~515 KiB after NSW CSV import). Cached resolve + mtime/size gate.
`[aliases]` 119 assertions PASS. Preparing 0.2.62 release.

## 2026-09-18 - RadioReference CSV talkgroup and site aliases

DEC-0101/0102: Import CSV for RR talkgroup and site exports, destination system
picker, Sites tab, control-log/tooltip site labels. Manual overrides preserved.
P25 RF path untouched. Workspace `[aliases]` 111 assertions PASS. T-0035 done.

## 2026-09-18 - Alias release published

v0.2.60 code and eight assets published after source/package tests and download
hash verification. Public updater manifest verified. T-0034 done. No real alias
directory installed or private data uploaded. JSON/manual lists are available;
external directory integrations and radio-ID aliases remain future scope.

## 2026-09-18 - P25 alias-list manager

DEC-0100 implements system-isolated labels with create/edit/search/import/export,
staged review, manual-name precedence and atomic conflict-aware persistence.
Structured JSON avoids a new parser dependency. Unit and GUI workflow tests
pass; application layouts remain valid. RF/audio logic unchanged. README and
P25_ALIASES explain format and deferred integrations. Preparing0.2.60 assets.

## 2026-09-18 - 0.2.59 published

Source, tag and eight release assets published for experimental live NFM SSTV
testing. Full CTest, extracted CLI/GUI/worker reference and lifecycle checks
pass; downloaded asset hashes and public updater manifest verified. T-0033
complete. Known-transmission RF acceptance, HF live input, additional modes
and system-scoped talkgroup alias importing remain future work. P25 unchanged.

## 2026-09-18 - Live NFM SSTV integration

DEC-0099 adds bounded main-receiver attachment, live GUI session ownership,
Finish/save and cancellation. Independent recording-driven live GUI images
match the reference decode; lifecycle, concurrency and regression tests pass.
P25 DSP/security/speaker paths are unchanged. README/SSTV help document the
experimental NFM-only scope and open RF acceptance. Existing Alpha Tag support
answers talkgroup names; system-scoped alias import is planned as T-0032.
Preparing0.2.59 source and assets; publication not yet verified.

## 2026-09-18 - Combined SSTV streaming path verified

DEC-0098 links the isolated queue, converter and helper into a bounded
synchronous worker with explicit EOF, fault propagation and owned cleanup.
Independent recording pixels match the converted recorded path exactly; error,
cancel and regression gates pass. No changes to P25 or receiver routing.
Readme/developer docs updated. T-0031 remains open for receiver lifecycle/UI
and live acceptance. Source checkpoint only; no new release asset yet.

## 2026-09-18 - SSTV fractional-rate conversion qualified in isolation

DEC-0097 adds worker-owned continuous miniaudio conversion with bounded blocks,
scaled rate precision, exact 48 kHz bypass and reset/error isolation. Native
timing/partition tests and independent image quality checks pass; recorded GUI
and pipe regressions still pass. Documentation updated. No RF or P25 wiring
changed. T-0031 remains open for bounded worker, receiver lifecycle/UI and live
acceptance; no version bump or feature-release asset for this internal stage.

## 2026-09-18 - Streaming SSTV helper transport verified

DEC-0096 adds bounded buffered stdin/file input to the existing pinned decoder,
without DSP changes. Handles split samples, Interrupted reads, EOF/error and
sample-budget enforcement; emits rows and completion metadata incrementally.
Five reader units, eight file/pipe recording parity cases, existing CLI/GUI
regressions and full CTest pass. Documentation and development command updated.
T-0031 remains open for fractional-rate conversion, C++ pipe worker, RX/UI
integration and live acceptance. Source checkpoint only; no new release asset
or live SSTV claim. P25 and speaker processing are untouched.

## 2026-09-18 - Live SSTV input foundation

Added DEC-0095 bounded preallocated NFM ingress with explicit discontinuities,
generation and sample-position metadata, nonwaiting producer and fault stats.
Six native cases and repeated concurrent stress pass alongside full CTest and
four independent recorded-image GUI cases. No receive/audio/P25 wiring changed.
T-0031 remains in progress for streaming conversion/helper/UI/live acceptance.
No version bump or release asset: 0.2.58 remains the completed feature release.

## 2026-09-18 - Progressive recorded SSTV feature/release preparation

DEC-0094 adds scanline transport and latest-only GUI previews on the same pinned
backend. Full/partial Robot36 and Martin1 match direct decoding exactly; malformed
transport and cancellation/teardown gates pass. Readme/help/build scope updated
for 0.2.58. T-0030 packages the completed recorded-GUI feature; live RF remains
next T-0022 stage, not a release claim. P25 implementation unchanged.
Release cadence requested by user: completed feature updates include current
README/docs, pushed source and fresh versioned release assets after acceptance;
do not silently leave full features source-only or overwrite a published version.
Completed: v0.2.58 /4006b94 published with eight verified assets and working
latest manifest URL. T-0030 closed; next T-0022 gate is bounded live RF input,
not another recorded-GUI placeholder. P25 remains isolated from this work.

## 2026-09-18 - Recorded SSTV GUI checkpoint

DEC-0093 adds Tools > SSTV Recorded Images on the tested CLI file decoder.
Off-thread single-job processing, cancellation/close ownership, full-resolution
PNG results and scaled preview are implemented. Actual independent Robot36/M1
recordings match direct output through the Qt window; responsive timer and both
window-size screenshots checked. Core/Qt and CLI regression tests pass.
Documentation distinguishes unreleased GUI source from published 0.2.57 assets.
P25/DSP unchanged. Next: bounded live SSTV input and progressive image events,
then qualify more modes and advance AX.25/public satellite work.

## 2026-09-17 - Recorded SSTV images and 0.2.57 preparation

Continued T-0022 with native VIS diagnostics and an isolated pinned MIT image
helper. Robot36 and Martin1 decode independent recordings through the app;
PNG export, partial status, reproducibility hashes and bounded failures tested.
Readme/commands/build and scope docs updated. P25/live receive left untouched.
Local core/CLI gates pass; release publication remains T-0028 until signed
assets, extracted portable tests and GitHub download comparisons finish.
Completion: tag v0.2.57 at 9347f82 pushed, eight assets published as Latest
experimental after identical download hashes and extracted CLI/GUI passes.
Public latest/update.json matches signed local manifest. T-0028 closed.
Existing hosted/local P25 string failures are recorded as T-0029, not hidden
by this new decoder release; P25 source/DSP/security paths remain unchanged.

## 2026-09-17 - Native shutdown fault reproduced and corrected

CDB caught libusb/RTL teardown AV; a minimal native-only lifecycle test then
reproduced the old deployed RTL DLL failure on cycle 2. The manifest's configured
RTL runtime passes 10 cycles, as does its newly deployed copy. CMake now stages
that imported target explicitly and includes its licence in release staging.
Five actual GUI shutdowns under CDB pass; 30-second RDS reception/parity passes;
279 core/Qt tests pass. T-0026 closed for the reproduced local failure. P25 and
application shutdown logic untouched. Added reusable native and CDB test tools.
Next decoder remains SSTV (T-0022). No push, installer, or release publication.

## 2026-09-17 - RDS live gate passes; high gain isolated

Independent RTL gain comparison isolated overload: 40.2 dB clips ~32% of raw
components and decodes no groups; 19.7 dB clips none and decodes 53 clean groups.
Actual GUI at requested 20 dB passes twice with 396/261 groups and correct
station metadata. Adapter/native agreement holds; T-0021 complete. Prior gain
restored after experiments, no all-mode gain or DSP changes. Added temporary
gain automation, cu8 analysis/tests and explicit real-hardware acceptance.
One earlier successful-reception run crashed at shutdown. CDB probes did not
reproduce unhandled fault; T-0026 remains open for stack-based diagnosis.
Full core/Qt suite passes. No release/push. SSTV remains next decoder milestone.

## 2026-09-17 - Live RDS adapter ruled out on identical input

Added opt-in, bounded same-input parity probe, tested it with recorded fixtures
and actual GUI reception. Native/adapted outputs match on all 1,446 live blocks;
both fail station acquisition. Captured five seconds of gapless IQ and performed
independent offline FM demod: still no groups. Known-good reference decodes at
the measured live MPX rate. Exact results in BUILD_NOTES; no guessed RF cause,
DSP tweaks or station-success claim. Added reusable inspection/validation tools.
Release build and full core/Qt suite pass. Antenna/cabling question outstanding.
P25 unchanged, no release or push. RDS live acceptance remains open.

## 2026-09-17 - Shared receive decoder contract, live gate open

DEC-0085 adds a bounded versioned receive interface and immutable registry for
the existing RDS, CTCSS and DCS backends. RDS GUI and MPX file replay now use
the same adapter; native versus adapted RDS matches at every recorded block.
Source/domain/version errors reset state instead of inheriting another stream.
Added actual CLI registry diagnostics, contract documentation and parity tests.
Release build, 279 core/Qt cases, CLI checks and four GUI layout checks pass.
Two live RDS checks failed to identify the expected station, so T-0021 remains
open. Evidence and the next same-input comparison are in BUILD_NOTES/ISSUES.
No speculative DSP correction, P25 processing change, release or push.

## 2026-09-17 - Experimental DCS integrated

Implemented native Golay-valid repeated-word DCS recognition, cyclic/polarity
aliases and bounded nominal-rate discriminator recovery. Uses the existing
NFM raw tap independently of speaker processing, with above-spectrum status and
GUI diagnostic snapshots. Added CLI WAV/FLAC and bitstream diagnostics, all-code
tests and independent Python fixture integration. Eight DCS cases pass, full
274-case core/Qt suite passes, four GUI layouts pass, live CTCSS/DCS sample
counts match exactly. No known-code RF claim; user's radio check remains deferred.
Next coding milestone is T-0021 shared decoder contracts/registry, then SSTV.
P25 behavior unchanged. No release, push or dependency install performed.

## 2026-09-17 - SSTV and satellite scope recorded

Added user-requested SSTV and public weather/amateur satellite roadmap with
explicit tasks, delivery order, backend evaluation, Images/Satellites workspaces,
pass/Doppler/device scheduling and per-link validation gates. Separated archived
NOAA APT formats from live targets; no all-satellite coverage claim. Reviewed
upstream SatDump, gr-satellites and QSSTV sources. Planning-only pass: no RX/DSP
code changed, no dependencies installed, no build or hardware test claimed.

## 2026-09-17 - NFM tone identification and continuity

Implemented experimental informational CTCSS, CLI replay diagnostics and GUI
status. Real hardware testing caught defects absent from original fixed-parameter
fixtures: bandwidth data resets, inherited speech AFC resets and bandwidth-only
GUI cursor resets. Added varying-BW/AFC regressions, isolated the NFM data mixer,
and preserved analog NFM cursor continuity. Final live test: 35 completed windows,
three startup resets, no subsequent resets. 265 core/Qt cases pass. Speech output
parity tested; P25 paths not changed. User deferred independent known-tone test.
Started DCS with an independent standard-codeword/bit-order/polarity oracle and
eight ideal WAV fixtures. Live DCS is not implemented; clock/alias/noise validation
is the next gate. No release or GitHub push requested/performed this pass.

## 2026-09-17 - Automatic RDS and waterfall controls

Implemented user-requested automatic WFM RDS status, visible frequency-aligned
band sections and release-committed waterfall drag tuning. RX worker uses source
epoch/cursor evidence for data-only resets; UI never reads mutable codec state.
Fixed observed stale frequency input on waterfall/automated tuning. Added
repeatable bounded live hardware GUI test and RDS self-test diagnostics.
257 native/Qt cases, four actual GUI layouts and CLI fixtures pass; live GUI
decoded 402 RDS groups with verified expected i98FM PI/PS and song radiotext.
P25/audio processing left unchanged. Wider RF/Unicode/registry work remains.

## 2026-09-17 - Recorded RDS DSP milestone

Continued T-0018 with pinned Redsea carrier/timing recovery and liquid-dsp behind
a versioned C ABI DLL; native decoder resets all DSP state on sample provenance
changes and bounds file/input sizes. Added MPX CLI and recorded upstream fixture.
Reproduced/fixed optional MPX tap chunk discontinuity using its own causal filter
and persistent decimation grid; legacy speech/P25 paths left unchanged this pass.
Release build, 254 core/Qt cases and actual CLI smoke/error tests pass. Vendor
notices, submodule metadata and deploy/install licenses updated. No push/release.
Next: live source-loss propagation, receiver capability routing and RDS GUI dock,
then independent RF identity verification; T-0018 remains in progress.

## 2026-09-17 - User-approved move to RDS

Deferred P25 optimisation as requested, without closing its measured gaps.
Completed first RDS protocol milestone: pinned redsea sync/CRC/FEC, complete
group metadata, native offline CLI and optional pre-audio WFM multiplex tap.
Tests exposed and fixed Windows quoted-path parsing and a text-segment reset
ordering defect before completion. Release and all tests pass. Deployment
rules include upstream notices. Live carrier/timing demodulation, extended
text and GUI display remain explicitly unimplemented, tracked by T-0018.

## 2026-09-17 - Jitter repair verification, remaining parity gap

Verified DEC-0077 resampler partition fix, spectrum dB label mapping and Qt
settings isolation. Core/workspace suites and four GUI launches pass. Real
TG30003 replay retains frame count and has no GUI tail loss, but the actual
PCM comparison uncovered a GUI/CLI mismatch despite equal sample counts.
Recorded this separately from live producer gaps rather than asserting P25
completion. No decoder/security threshold changes and no release this pass.
Next decoder remains RDS under WORKSPACE_AND_DECODERS.md; do not advertise
it as implemented or close P25 acceptance based on STT/duty alone.

## 2026-09-17 - Location-based receive profiles and waterfall context

Completed infrastructure REQ-BP.1 / DEC-0076 with AU/GB/US partial profiles,
region/country/location selector, bounded local JSON import/export, persistent
explicit selection, AUTO priors and waterfall service/decoder hints. Digital
hints do not pretend to be implemented decoders. Fixed broad airband AM prior
including navigation and added AU CB data exception. Old unverified generic
HF priors are not silently asserted to apply nationally; coverage is tracked
under open T-0015 / REQ-BP.2. No IP geolocation or decoder trust changes.
Release build, 244 tests, four GUI cases and CLI/GUI P25 byte-equivalence pass.
No commit, GitHub push or release asset was requested/published in this pass.

## 2026-09-17 - Dockable workspace foundation

Completed REQ-UI.1 / DEC-0075: existing receiver controls remain central;
saved frequencies, P25, receiver management and capture/display moved into
named detachable/tabbed docks. Added four layout presets, visibility/lock,
save/restore/reset and normal-close persistence. Automation does not overwrite
saved layout. Added workspace/size/screenshot CLI arguments and GUI QA script.
238 cases pass; real GUI matrix reviewed; CLI/GUI reference P25 WAVs identical.
No P25 decoder changes or publication. Decoder registry/RDS deliberately remain
next, rather than advertising an unimplemented module. Roadmap and usage:
`WORKSPACE_AND_DECODERS.md`.

## 2026-09-17 - v0.2.55 packaging

User requested source and release assets. Version bumped to 0.2.55; full tests
and clean package checks passed, including signature/hash verification and
staged CLI smoke test. Prepared installer, portable ZIP, standalone control
DLL, checksums and signed updater metadata. Release notes describe measured
progress without closing the all-call continuous-audio acceptance issue.

## 2026-09-17 - Downstream race repair and real GUI capture

Repaired callback cursor overwrite during clear/discard (DEC-0074), with a
nonblocking callback lease and regression tests. Added callback counters and
capture audit deltas. Release/full suite pass: 235 cases / 81,154 assertions.
90-second GUI auto-follow at 420.350 MHz completed three follows; no producer
drops, IQ overruns, or control-lease collisions. Reference replay is byte-identical
after the downstream patch. Live/replay STT still contains errors: no claim of
fully repaired speech and no release published. Detailed evidence and remaining
acceptance criteria are in `P25_DOWNSTREAM_AUDIT_20260917.md`.

## 2026-09-17 - Mapping defect proven from identical RF

Full trace found an exact 0/2-only dibit permutation on overlapping identical
RF. Rejecting nonphysical Phase 2 mappings sharply reduces codec concealment
and false voice. Block-tail unit test then reproduced lost A/B bursts beyond
one lock; fixed current-window walking and duplicate uncovered-sync processing.
233 tests pass; final GUI replay completes; live mapping-only capture provides
recognizable exchanges via local STT but still underruns. No claim of complete
continuous audio; no publish this pass. Full audit:
`P25_MAPPING_AUDIT_20260917.md`.

## 2026-09-17 - Verified slot-state and GUI tail defects

DEC-0069 makes mutable session selection agree with I-ISCH-rebased ownership,
including final-fragment C/D inversion used by SDRTrunk. Regression tests cover
clear/encrypted companion calls. Same-IQ reference retains four additional
frames previously rejected for stale talkgroup state. DEC-0070 fixes GUI EOF
tail starvation without altering live startup thresholds. Added absolute burst
and codeword stream positions to detailed validation logs. Release rebuilt;
231 tests pass. New capture still has six gaps and 66 concealment frames:
continuous clear speech remains unproven. No release or asset published in
this follow-up; do not replace the existing 0.2.54 assets with these changes.

## 2026-09-17 - Capture 060515 processing-cost correction

Exhaustively verified GF64 lookup arithmetic and cached RS syndrome columns.
The new capture replays faster than its eight-second RF duration; the earlier
12-second reference retains exactly the same PCM bytes and runs substantially
faster. No slot, encryption, gain, hop, or timeout policy was changed in this
optimization. Continuous Phase 2 remains open. Preparing experimental 0.2.54
for remote testers with honest remaining-gap notes.

---

## 2026-09-15 — DEC-0066 dead unknown-grant ~45s hang (131458)

- **Evidence:** 0 emits; TG12068/10326 unknown parks ~45s no-VCW each.
- **Fix:** unknown/cold no-VCW timeouts → ~8s/6s; clear cold → ~10s/7s;
  hard-timeout covers unknown dead.

## 2026-09-15 — DEC-0065 return without RF retune while LO on voice (125341)

- **Evidence:** TG10120; return `without RF retune` at cf=421.33875 claiming
  CC 420.350; P25 log stopped. retunesPrimary=false despite LO move.
- **Fix:** return forces RF-home when |cf−cc|>75 kHz; start latches
  RetunedPrimary when primary LO leaves CC.

## 2026-09-15 — DEC-0064 warm-standby return kills CC watch

- **Evidence:** Bridge Monitor-CC → follow → return; P25 log stops after warm-standby
  while RF still on voice; validation disable zeros CC.
- **Fix:** pause CC decode/validation in warm-standby; reset validation on real CC
  RF return; harden idle arm; persist bridge autoFollow; status warmStandbyActive.

## 2026-09-15 — DEC-0063 FUBAR follow snap-back (420.350)

- **Evidence:** DLL Monitor-CC / Tune re-armed same CC mid-grant → RF back to
  420.350; voice follow cleared; P25 receive stopped.
- **Fix:** idempotent `armGuiRuntimeP25Control` on same-CC+follow; analog
  `/v1/tune` 409 while follow live; FUBAR Tune refuses on follow status;
  control-tune request logging + `voiceFrequencyHz` in status.

## 2026-09-13 — DEC-0062 talkspurt vocoder reset (225923)

- **Evidence:** TG30304; one RID clear, later talk unintelligible; src=unknown;
  BAD uniqueFreshR≈1.0 (overlap tax) while mbelib never reset mid-grant.
- **Fix:** MAC_PTT / post-END talkspurt resets selected vocoder (keep abs-dedupe).

## 2026-09-13 — DEC-0061 catch-up overlap restore (153932 jitter)

- **Evidence:** After DEC-0060 280+80, emit WAV islands p50=40 ms / chop=76;
  workers 160+80 & 280+80 empty-audio heavy; bridge top-ups 68/69 @ ~11% fill.
- **Fix:** speaker backlog catch-up **240+280** (restore DEC-0024 overlap;
  keep fresh ≥ emit wall). Sustain 80+280 unchanged. Supersedes DEC-0060 sizes.

## 2026-09-13 — DEC-0060 half-audio catch-up pace (152348)

- **Evidence:** TG11108 RID 0x243754 pcm_vs_wall≈0.5; absDup≈missing feed;
  160+280 catch-up with emit dsp p50≈220 ms > fresh; budget burned on context.
- **Fix:** speaker backlog catch-up **280+80** (amends DEC-0058 sizes). Sustain
  80+280 unchanged.

## 2026-09-13 — DEC-0059 companion ESS aborting clear RIDs (145139)

- **Evidence:** Some clear RIDs perfect, others vanish/garble. TG30302 RID
  0x2391D7 ReturnEncrypted mid-emit while ess=clear; companion enc TG12068
  opposite slot; sticky traffic.encrypted ORed into follow.
- **Fix:** this-burst ESS only (traffic + target paint); recent clear clears
  sticky enc; MainWindow follow uses target ESS; no opposite-only pending drain.

## 2026-09-13 — DEC-0058 weak/choppy clear follow (142104)

- **Evidence:** TG10301 CLEAR; 80+280 sustain while dsp~200–600 ms → emit gaps ~1s;
  dual-slot MAC-dead waiting-clear after latch.
- **Fix:** speaker backlog catch-up 160+280; latched selected-dominant dual-slot
  continuation (feed + security). DEC-0012 companion-louder unchanged.

## 2026-09-13 — DEC-0057 TG30003 wrong-TDMA spam (`135857`)

- **Evidence:** 29s CLEAR audio; grant slot=1 correct; 75 wrong-TDMA status lines
  on companion-only windows. Catch showed lock-rel-only mislabels final-fragment C.
- **Fix:** no wrong-slot diag when grant immutable; keep I-ISCH absolute grantSlot
  (DEC-0055.3). Status spam was companion dwell, not absolute rebase.

## 2026-09-12 — DEC-0056 clear hang + WFM BW (`134135`)

- **Capture:** Enc returns fast; Clear TG30302 ~57s hang; WAV 5.64s CLEAR;
  CADENCE ok=2/64; budget/worker heavy; FEED_GATE primary.
- **BW/LPF:** P25 arm 12.5 kHz, LPF off, AMBE speaker — not the crackle path.
  WFM default raised 180→220 kHz for analog.
- **Hang fix:** grant-clear no longer extends 40s speaker grace; post-speech
  no-VCW 12s/6s; no structure-only lastActive after clear speech.

## 2026-09-12 — DEC-0055 epoch + dual-slot keep + I-ISCH origin

- **Audit (code-only):** DualSlot `audio.clear()`, soft `epochTrusted`, lock-relative
  slot map were the continuous-clear breakers.
- **Fix:** keep labelled Clear selected PCM on dual-slot; drop bare
  establishedClear+xor epoch; rebase absolute 0..11 when A/B I-ISCH agree.
- **Gates:** `verify_p25_phase2_dec0055_epoch_dual_slot_origin.py` + Catch rotate
  I-ISCH case; dual-slot verifier updated for keep path.
- **Not claimed:** live 100% — needs B-0001 start/stop + listen harvester.

## 2026-09-12 — DEC-0054 restore cold full-commit (`081416` no audio)

- **Regression:** 0.36s SILENT after DEC-0053; cheap-commit never logged.
- **Cause:** CQPSK headroom + cold forceCheap poisoned first eye.
- **Fix:** remove headroom; cold full-commit +200ms re-arm; sticky cheap 120ms.

## 2026-09-12 — DEC-0053 sticky cheap-commit (`064509` golden→silence)

- **Symptom:** first ~0.6 s CLEAR then nothing; WAV 1.24 s / 2 emits.
- **Cause:** DEC-0052 sticky skip-commit (+ expired deadline aborting cheap path).
- **Fix:** sticky+budget → cheap-commit with 50 ms allowance re-arm; no skip.

## 2026-09-12 — DEC-0052 mustAnnotateCommit hole (`061217`)

- **Capture:** ~713 s gapless; live listen CLEAR; CADENCE ok only 20/676.
- **Worker:** emit p50≈223, busy 690 — DEC-0051 never tripped (0 log hits).
- **Cause:** `phase2CqpskTrafficDemod` forced unbounded commit after deadline.
- **Fix:** sticky skip-commit + cold cheap-commit + CQPSK headroom + p25_log
  budget trips. Verifier `verify_p25_phase2_budget_skip_sticky_commit.py`.

## 2026-09-12 — DEC-0051 cooperative mid-decode budget abort (`044651`)

- **Forensic (no listen):** `run_p25_capture_full_forensic.py` on `044651`;
  `p25_logscan --audit` on `041612`. No `*_live_speaker.wav` yet (pre-0050
  builds).
- **Dominant live failure:** emit dsp p50≈212 ms / empty max 642 ms under
  budget 80 → worker-busy 135 → rolling ~16 s. Wall stamps never fired.
- **Fix:** shared processIq deadline + abort in sync/lock/mask/sticky loops;
  Catch budget ceiling 350 ms; verifier
  `verify_p25_phase2_cooperative_budget_abort.py`.
- **Not softened:** DEC-0012, DEC-0046 wall, streaming DDC default-off.
- **Next:** rebuild Release; one start/stop listen; PPM≈−2; harvester.

## 2026-09-12 — DEC-0050 PCM listen classifier (live vs file)

- **Gap:** File replay “pretty good”; live islands/garble/silence. Duty≠clear.
- **Add:** `p25_pcm_listen_classify.py` CLEAR/GARBLED/SILENT; start/stop
  `*_live_speaker.wav`; `p25 listenclassify`; forensic + listen bar harvester.
- **Use:** after next live start/stop →
  `python src/tools/run_p25_listen_bar_harvester.py <capture>`
  (flags `LIVE_WORSE_THAN_FILE` when file CLEAR + live bad).

## 2026-09-12 — Forensic `044651` + DEC-0049 Auto PPM harden

- **Live:** mixed — TG10120 duty up to 0.909; TG20202 stuck ~0.64 drop D;
  A=86/132; worker-busy 135; CC high TSBK dibit corrections.
- **Auto PPM bug:** AFC=1250 conf=0.45 stepped ppm to **−7.88** (twice).
- **File bars:** TG10120 duty 0.64; TG20202 0.615; proves RF/mbelib.
- **Fix:** reject ±1250 rail; conf≥0.55; step≤1.5; cooldown 120s; trusted
  offset only. Full pipeline script `run_p25_capture_full_forensic.py`.
- **Operator:** set device PPM back near **−2.0** before next listen.

## 2026-09-12 — Automated forensic `041612` + DEC-0048 eye-lost streak=1

- **Capture:** `20260912_041612` (~215 s) after DEC-0046 listen (“no audio”).
- **CLI:** `p25_logscan` + `p25_capture_audit` + voicetest.
- **Live:** CADENCE A=82/100, duty max 0.399, worker-busy 103, Auto PPM 0.
- **File same IQ TG20202:** duty **0.805** ambe=322/322 — **not mbelib**.
- **Fix:** eye-lost escalate cand=16 on streak≥1; add `p25 logscan`.
- **Next:** live listen + startstop; logscan should show fewer permanent A cliffs.

## 2026-09-12 — DEC-0046: wall clamp killed almost-working audio

- **Operator:** regressed to “really bad audio” after DEC-0044/0045.
- **Root cause:** decode wall is **post-hoc**. DEC-0045 clamped healthy wall
  to 105 ms → empty eyes finishing >105 ms stamped `decode-wall-timeout` →
  publish path **wiped speaker pending** mid-call. Classic
  almost-works → gap-fix → total break.
- **Fix:** reject wall clamp; wall stamps never clear pending (diags only);
  keep auto PPM on CC return. Catch `[p25][dec0046]` + verifier update.
- **Still open:** cooperative realtime abort so dsp cannot sit at ~451 ms
  under budget 80 (worker-busy class from `032907`).
- **Next:** live listen with capture started; look for continuous clear,
  `Auto PPM:` on return, **no** pending wipe after empty wall hops.

## 2026-09-12 — DEC-0044/0045 auto PPM + healthy wall (`032907`)

- **Operator:** “started promising then losing it” — suspected PPM.
- **Forensic `20260912_032907`:** CADENCE mean **0.060**; worker-busy **500**;
  emit-gate healthy dsp p50≈**451 ms** vs budget 80 / wall 320. Device ppm
  **0.0**, AFC≈**884 Hz** (~2.1 ppm). Cliffs co-timed with worker-busy; clear
  grants exist (TG20201/20202 ENC=clear). PPM is real LO bias; cliff class is
  still drop **D** / throughput.
- **Fix:** DEC-0044 auto-apply device PPM from trusted CC AFC on
  return-to-control only; DEC-0045 clamp healthy wall **105** / eye-lost
  **145** and refresh deadline.
- **Gate:** Release rebuild; `[p25]` + verifiers (new
  `verify_p25_phase2_auto_ppm_and_healthy_wall.py`).
- **Next:** live listen — look for `Auto PPM:` on CC return, CADENCE ok≥0.65
  on clear follows, emit-gate dsp near budget (not ~450 ms).

## 2026-09-12 — Forensic `024000` + twin-rescue revert

- **Operator:** audio “nonexistent / disconnected.”
- **Mixed RF in one capture:**
  1. TG **30003** @420.225: file **encrypted** (`PASS_ENCRYPTED_GATED`).
  2. TG **12068** @421.975: live ReturnEncrypted (correct).
  3. TG **30003** @421.975 slot1: file **clear**
     `PASS_CONTINUOUS duty=0.705` — live max **0.649**, **0** ok≥0.65,
     wrong-TDMA / `no-sf-mask` / worker-busy **80**. Real clear miss.
- **CADENCE:** n=78 mean **0.074**; A=61 D=17.
- **Action:** revert DEC-0043 ±1 DUID lock-twin rescue (kept post-speak
  invalidate debounce ≥3). Soft twin + debounce stuck bad epochs on live.
- **Next:** new Release live listen on clear follows; worker drop D still open.

## 2026-09-12 — DEC-0043 clear sticky vs wrong-TDMA thrash (`020758`)

- **Evidence:** TG20201 clear almost-works then permanent `no-vcw` after
  wrong-TDMA islands; file duty **0.795**. Immediate post-speak opp-dominant
  sticky invalidate + ±1 lock flip on block path. Encrypted unknowns stay
  gated (DEC-0012).
- **Fix:** debounce opp-dominant invalidate after speak (streak≥3); block-path
  preferred-slot ±1 lock twin rescue under sticky XOR phase. No hop/TTL /
  streaming default-on / DEC-0012 soften.
- **Gate:** Release rebuild; `[p25]` 109/109; verifiers **130/130** (new
  `verify_p25_phase2_opp_dominant_sticky_debounce.py`). File `020758`
  TG20201 skip=18439 8s: `PASS_CONTINUOUS_AUDIO duty=0.715`; TG12069:
  `PASS_ENCRYPTED_GATED` essEncrypted=yes. Keep-set 060036/095846 not on
  disk this host.
- **Next:** operator live listen on new Release (CADENCE clear follows vs
  `020758`/`002128`).

## 2026-09-12 — Forensic `020758` (DEC-0042 live listen, ~138 s)

- **Capture:** gapless, SNR ~10.6 dB, CC 420.475. Binary after DEC-0042.
- **Operator ears match three follow classes exactly:**
  1. **TG20201 clear** slot1 @421.225 — “almost works / choppy”: live max
     duty **0.943** but only **2** ok seconds; then D islands → cliff to A
     (`vcw-soft` + **wrong TDMA** storm) → permanent `no-vcw`. File same
     slice: **duty 0.795**. Still live worker-busy + eye-loss (emit dsp
     p50~145 ms, still all emit jobs &gt;80 ms).
  2. **TG12069 unknown** slot0 @420.225 — “nonexistent”: live all **B**
     `metadata-gate-waiting-traffic-mac-ess`. File:
     `PASS_ENCRYPTED_GATED` **essEncrypted=yes**. Correct mute — not a
     continuity miss.
  3. **TG12068 unknown** slot1 same RF — “brief choppy”: live B then ~3 s
     D (0.52/0.34/0.36) then A. File: **encrypted gated**; forced-clear
     only duty **0.23**. Do **not** soften encrypt/DEC-0012 to chase this.
- **Cross-TG:** clear follows = worker throughput / wrong-TDMA eye loss;
  unknown/encrypted follows = gate (correct). No hop/TTL invention.
- **Next fix target (clear only):** cut healthy emit below ~80 ms further
  and/or stop wrong-TDMA thrash from killing sticky eye after speak
  (without softening dual-slot mute).

## 2026-09-12 — Forensic `002128` + DEC-0042 healthy sustain (cand=4/80)

- **Capture:** `20260912_002128` (~54 min / ~50 GB, SNR ~15.1 dB, ring
  overrun only ~48 ms). Stopped on disk reserve. Binary mtime after
  DEC-0041.
- **CADENCE:** n=2731 mean duty **0.115**; A=1891 D=650 B=91 ok=99;
  worker-busy **2782**. Pattern **identical across TGs**.
- **Mechanism (named):**
  1. Drop D ≈ ok on feedRatio; D has ~half the windows/targetVcw →
     single-flight worker under-samples RF (emit dsp p50≈199 ms).
  2. Hard cliffs always co-timed with worker-busy, then eye collapse → A.
  3. Drop B residual (91) = tv>0 fed=0 (DEC-0012 class; do not soften).
  4. File TG30017 slot1 same IQ: **duty 0.83** → not RF/MAC absence.
- **DEC-0042:** healthy sustain **cand=4/80**; keep DEC-0041 eye-lost
  escalate. No hop/TTL / DEC-0012 / streaming default-on.
- **Gate:** Release rebuild; `[p25]` 109/109; verifiers 129/129.
- **File bars held:** 060036 duty **0.705**; 095846 duty **0.84**;
  `002128` TG30017 file duty **0.83**.
- **Next:** operator live listen on new Release (CADENCE vs `002128`).

## 2026-09-12 — DEC-0041 live eye-lost budget (drop D on `234224`)

- **Named evidence:** `20260911_234224` CADENCE D=101, worker-busy while
  single-flight busy; dsp p90 ~461 ms on 80+280 eyes; submit p50 ~159 ms.
- **Root class:** DEC-0035/0039 live eye-lost inherited replay **240 ms**
  wall with cand=16 → multi-hundred-ms jobs starve the next live windows
  (drop D), not hop geometry / DEC-0012.
- **Fix:** streak debounce (first miss stays cand=8/120; streak≥2 →
  cand=16) + live escalate budget **120** (`kP25LiveEyeLostReplayBudgetMs`).
  Realtime ACCH score rescue bounded to top **2** phases × deep **1**
  (keep alt-kind fanout on deep). No hop/TTL; no DEC-0012 soften; no
  streaming default-on.
- **Gate:** Release build; `sdr_town_tests` `[p25]` 109/109; verifiers
  129/129.
- **File bars:** 060036 TG10301 slot0 skip=261000 8s
  `PASS_CONTINUOUS duty=0.705`; 095846 TG10301 slot1 skip=68700
  `PASS_CONTINUOUS duty=0.8`.
- **Next:** operator live listen on new Release (CADENCE drop D vs
  `234224`); then commit + PR.

## 2026-09-12 — Operator live listen (gapless IQ `234224`, ~372 s)

- Operator report: when P25 emits, sound is much better / few gaps; some
  followed grants stay silent or only short &lt;1 s islands.
- Capture `20260911_234224` (CC 420.475, gapless, SNR ~12.5 dB) with HEAD
  Release including ACCH alt-kind branch binary:
  - Follows: TG **30003** unk, **10120** unk, **30302** clear, **10010**
    clear, **30314** unk.
  - CADENCE n=338: emit&gt;0 **123**; drop **A=211 / D=101 / B=18**;
    duty mean **0.134**, median **0**, max **1.07**.
  - Per-TG max duty: 30003 **0.56**, 10120 **1.07**, 30302 **0.52**,
    10010 **0.64**, 30314 **0.86**.
  - Dominant blocks: `no-vcw-from-live-window`, then
    `traffic-processor-audio-open` / `clear-grant-vcw-not-fed` under
    worker-busy. Matches ears: short islands = **A+D** thrash after brief
    `gate=emit`; mute follows = long **A**/occasional **B**.
- Do **not** soften DEC-0012. Next: cut live drop **D** (worker-busy while
  audio open) and eye-loss **A** on follow; ACCH/MAC still for residual **B**.

## 2026-09-11 — ACCH alt-kind rescue + soft-AMBE phase select fix

- **Root cause class (20202 IQ):** drop=B with `p2sf/p2mask` high,
  `ambeProbe` OK, `p2mac=0/N`, `essKnown=no`. Deep/forensic rescue across
  phases still `p2macCrc=0` — this follow IQ has no recoverable ACCH CRC
  (SNR ~5.5 dB). Do **not** soften DEC-0012 / feed without MAC|ESS proof.
- **Shipped on `fix/acch-rescue-clear-grant-mac`:**
  1. Deep ACCH rescue fans out ACCH **kinds** on nominal layout; accept
     CRC when layout is nominal even if DUID≠source (`altKind`).
  2. Soft AMBE no longer selects the commit XOR phase unless
     `allowPhase2SoftAmbeMaskPhaseLock` (matches prior sticky policy).
  3. Realtime score rescue tries top **2** mask phases (cost-bounded;
     was briefly 4 on this branch) with deep budget **1**.
  4. Unit test: sticky lock then Facch-mislabeled Sacch recovers
     `phase2MacAltKindCrcValid`.
- **Bars held:** 060036 duty **0.705**; 095846 TG10301 duty **0.84**.
- **Still open:** B-0001/B-0002 need a follow IQ that actually carries
  recoverable MAC/ESS (or ESS recovery improvement with evidence).
- **Follow-up same day:** DEC-0041 caps live eye-lost budget (drop D).

## 2026-09-11 — Live CLI clearaudio + capture hygiene

- **Space:** trimmed `iq_test_captures` from ~29 GB → ~8.6 GB keep-set
  (`060036`, `094846`, `095846`) + new live follow IQ (~125 MB). Rotated
  `sdr_town.*.log` and old voicetest WAVs removed. Free disk ~44 GB.
- **Merged:** PR #12 Add Receiver `rx.active` arm (`f7c201e`).
- **Live CLI** `p25 clearaudio 420.475` (HEAD Release):
  - TG **10301** clear slot0 @ 417.675: ~3 s target WAV / ~4 s companion;
    intermittent decode, `speakerRecent=no` then cliff to `no voice sync`.
  - TG **20202** clear slot0 @ 417.675: **target WAV empty**, companion
    ~47 KB; follow IQ `…082310…deadline…15.0s`.
  - File voicetest that IQ: slot0 **drop=B** `targetVcw=94 fed=0`; slot1
    drop=A. Class: **B (feed)** — VCWs present, mbelib not fed. Do **not**
    soften DEC-0012. Do not invent hop/TTL.
- **Still open:** ISS-0001 / B-0001 live duty ≥0.65; B-0002 cold-eye
  first-hop feed starve (this live drop=B is same class).

## 2026-09-10 — ISS-0008/0009/0010/0011 hygiene (no audio rewrite)

- **ISS-0009:** `definition_body` / `require_definition` in
  `p25_orchestration_sources.py`; migrated 14 high-risk verifiers
  (worker/backpressure/session/unknown-probe/sustain-adjacent) to definition
  anchors (`MainWindow::…` / `.cpp` free-function defs).
- **ISS-0010:** `startP25LiveDecodePipeline()` → `MainWindowP25Orchestration.cpp`
  (~1434 lines guiDspWorker rolling-IQ/chunk/submit/CADENCE). Ctor calls it;
  UI/diagnostics/updater timers remain in ctor.
- **ISS-0008/0011:** CODE_NOTES SoT sentence + live vs CLI/voicetest ownership map.
- Gate: Release `SDR_Town` + `sdr_town_tests` 214/10194; verifiers 129/129.
- No hop/feed/CADENCE/audio algorithm changes.

## 2026-09-10 — ISS-0004: MainWindowP25Voice TU + ISS-0010/0011

- Mechanical: live voice worker / submit / backpressure / take / purge / publish
  → `src/MainWindowP25Voice.cpp` (~1.3k); `MainWindow.cpp` ~11.8k.
- CMake + `p25_orchestration_sources.py` updated; no moc in split cpp.
- Docs: ISS-0010 (mega-ctor), ISS-0011 (GUI vs CLI/voicetest dual path);
  ISS-0004 follow-up refreshed; CODE_NOTES cadence/tail ownership table (ISS-0008).
- No hop/feed/CADENCE/audio algorithm changes (move-only).

## 2026-09-10 — ISS-0004 Phase A–C: leftovers + MainWindow out-of-line

- **Phase A:** session/decode-config/demod-mode/saved-freq leftovers → focused TUs;
  `main.cpp` ~200 (bootstrap + `main()` only).
- **Phase B:** `MainWindow.h` declaration-only (~520); bodies → `MainWindow.cpp` (~13k);
  verifiers anchored on `MainWindow::` definitions where needed.
- **Phase C:** ISS-0008 / ISS-0009 filed; CODE_NOTES / BN / LOG updated.
- Gate: Release build + unit tests + `verify_p25_phase2_*.py` 129/129.
- No hop/feed/CADENCE/audio algorithm changes (move-only).

## 2026-09-11 — Backlog + Windows CI

- Added `docs/BACKLOG.md`: ordered P0–P3 walk-through (ISS-0001 live gate,
  REQ-P2.2–P2.6, TIA gap, CI, hygiene).
- Added `.github/workflows/windows-ci.yml` (Windows 2022 Release build + unit
  tests + Phase 2 string verifiers). T-0013 / B-0020 in progress until green.

## 2026-09-10 — Close ISS-0002/0003/0008/0009/0010/0011 (leave 0001/0006)

- **Still open (cannot clear without product evidence / external specs):**
  - **ISS-0001** live clear Phase 2 continuity (T-0010)
  - **ISS-0006** TIA-102 PDFs not in-tree
- **Closed:** dead SpeakerCatchUp constants removed; verifier definition anchors;
  live pipeline extract; ownership maps; string-verifier ≠ continuity process lock.
- Gate: `verify_p25_phase2_*.py` 129/129 after ISS-0003 removals.

## 2026-09-10 — ISS-0004 / T-0009 closed (DEC-0040); push + PR

- Phases 0–8 complete on `refactor/iss-0004-split-main`.
- Gate: Release `SDR_Town` + `sdr_town_tests` (214/10194); `verify_p25_phase2_*.py` 129/129.
- Docs: ISS-0004 closed, T-0009 done; CODE_NOTES / SPEC_INDEX updated for new TUs.
- Follow-ups (not blockers): leftover session/decoder helpers still in `main.cpp`; optional MainWindow header/ctor split.

## 2026-09-10 — ISS-0004 Phases 6–8: P25VoiceTest / CliApp / MainWindow thin main

- Mechanical DEC-0040 split on `refactor/iss-0004-split-main` (no hop/feed/CADENCE changes).
- **Phase 6:** SigMF/WAV + replay follow/voicetest → `P25VoiceTest`.
- **Phase 7:** CLI batch helpers + `runCLI` → `CliApp` (`GuiRuntimeConfig`/`SavedFrequency` in header).
- **Phase 8:** logging/theme/instance → `AppBootstrap`; GUI class + `populateP25Table` → `MainWindow.h` (Q_OBJECT); `main.cpp` ~2.1k lines leftovers + `main()`.
- Build Release `SDR_Town` + `sdr_town_tests` green; 10194 assertions; 129/129 `verify_p25_phase2_*.py`.

---

## 2026-09-09 — Back to DEC-0035 live re-lock (DEC-0039)

- Operator: not like the DEC-0035 / 095846 ~95% path.
- 110941 TG 10301: max duty 0.452 (095846 was 0.947). Streaming detour was
  the wrong product path.
- **DEC-0039:** post-emit no-target (even with companion bursts) uses replay
  cand=16/240. Reopen GUI exe.

---

## 2026-09-09 — Streaming sticky Gardner; default-on still rejected (DEC-0038)

- Tried to ship SDRTrunk-style sticky HDQPSK as default.
- 060036 block duty **0.705**; stream env=1 after sticky Gardner **0.23**
  (lock-create trial **0.12**). essKnown stays no; p2macCrc=4 vs 156.
- **DEC-0038:** keep Gardner across streaming search; no weak lock freeze;
  no default-on until ≥0.65.

---

## 2026-09-09 — DEC-0036 always-advance chirped clear audio (DEC-0037)

- Operator: worse — little emits instead of ~95% continuous (full flip).
- 100909: max duty 0.40, 0× ≥0.65 (095846 was 0.947 / 10×).
- **DEC-0037:** restore clear-eye hold; advance only waiting-clear; hold
  without purging newer jobs.

---

## 2026-09-09 — Start silence = rolling cursor hold (DEC-0036)

- 095846 after DEC-0035: later clear TG 10301 max duty **0.947** (operator ~95%).
- Start TG 30302 unknown: targetVcw=14 fed=0 pending=0 → cursor hold + purge
  → silent rest of follow.
- **DEC-0036:** advance rolling cursor when VCWs were not queued/fed.

---

## 2026-09-09 — Live vs replay: cand=8 starves re-lock (DEC-0035)

- Operator: live unchanged; replay almost always good.
- 094846 live (DEC-0034 exe): drop A 60/62; emit>0 5; max duty 0.338.
- Same IQ voicetest TG 30302: duty **0.43** targetVcw=652 — RF is fine.
- Live worker after speak: cand=8/120; replay/voicetest: cand=16/240.
- **DEC-0035:** live eye-lost hops use replay caps; healthy eye keeps cand=8.

---

## 2026-09-09 — Capture 092250: clearBlock hint wipe after emit (DEC-0034)

- Operator: almost continuous clear regressed to one emit then silence.
- Live 092250: drop A 116; emit>0 9/131; max duty 0.416; 80+280 held.
- TG 12014: 10 s of targetVcw with fed=0, one emit, then p2bursts=0 forever.
- Root: DEC-0032 empty-eye called `clearBlockCqpskHint()` — block path’s only
  Costas continuity. DEC-0033 companion-only sticky was also ungated on block.
- **DEC-0034:** keep hint on empty-eye ForceMask; gate companion-only to
  streaming. File 060036 still PASS_CONTINUOUS duty=0.705. Reopen GUI exe.

---

## 2026-09-09 — DEC-0033 sticky HDQPSK (stop hop CPR)

- 083254: DEC-0032 80+280 held; hang is drop A / no-vcw after first emit.
- SDRTrunk/OP25: continuous Costas+Gardner + MessageFramer; we were discarding
  framer bursts while FSM stayed Cold and sticky-walking companion slots.
- **DEC-0033:** queue/commit persistent framer under streaming (anchor required);
  annotate with source dibits; no anchor wipe on one misalign; companion-only
  sticky fallthrough; GUI replay context=0 when streaming; voicetest diag.
- File: 060036 block **duty=0.705**; env=1 **duty=0.25** (better than
  historical ~0.09–0.16, still not continuous). 105622 IQ missing. Default-on
  stays off. Live prove still T-0010.

---

## 2026-09-09 — Capture 081701: DEC-0031 catch-up killed post-emit eye (DEC-0032)

- Operator: worse — single emit then hang on `no voice sync`.
- Live: drop A 234; emit>0 7/241. After cold emit, hops `fresh=120 ms`
  (backlogCatchUp) → immediate eye death.
- **DEC-0032:** restore DEC-0009 80+280 after speak; empty-eye soft rehunt
  without MaskEpochRepair steal; keep once-clear continuation.

---

## 2026-09-09 — Capture 062006: LO OK, DEC-0030 OK, once-clear holes (DEC-0031)

- Screenshot LO 421.964 vs CC UI 420.475 is one-RTL park (voice−11.2 kHz), not
  wrong tune. TG 30003 grant was 421.975.
- Live: drop A after one emit; DSP ~70 ms / 80 ms fresh (drain OK); rolling 4 s.
- File voicetest same IQ: **duty 0.46** drop D — dual-slot companion-louder /
  `unknown-waiting-clear` (keep DEC-0012). 060036 file still **0.705**.
- **DEC-0031:** backlogCatchUp before speaker-sustain; once-clear continuation
  without requireFedAudio chicken-egg. Next: live eye sustain if still A.

---

## 2026-09-09 — Capture 060036: live 2s rolling clamp vs file 0.705 (DEC-0030)

- ~392 s gapless, DEC-0029 exe. Quiet-return thrash reduced; voice still sparse.
- Live: max duty **0.639** then cliff; worker-busy 216; rolling stuck **4194304**.
- File voicetest same IQ TG 10301: **PASS_CONTINUOUS duty=0.705**.
- Root: DEC-0023 4.0 s active rolling still clamped to 2.048 s samples.
- **DEC-0030:** honor 4.0 s rolling (+ 16 s emergency hard cap).

---

## 2026-09-09 — Capture 053448: sparse islands / quiet-return thrash (DEC-0029)

- ~308 s gapless, SNR ~17 dB, DEC-0028 exe. Eyes OK; **0** ReturnEncrypted.
- Operator: almost no voice — maybe one small emit every few minutes.
- CADENCE: **16**/176 s with emit>0; **0×** ≥0.65. Drop **A** (162) / **D** (12).
- Smoking gun: TG 30302 emit=8 @ 15:35:28 (`callClearTrusted=yes`), then empty
  eyes; **15:35:33** `ended or went quiet` released traffic; **15:35:35**
  same TG cold-rearmed (`context=0`, generation++). Pattern repeats.
- Root: follow SM dropped 2.5s speaker grace without current structure, then
  3.5s activityGone mid clearTrusted call. structureNoVcw still cold ~484 ms.
- **DEC-0029:** clear-trusted 40s speaker hold + 15s activity silence; exit
  coldAcquire on sf+mask≥4; same-call unknown grant preserves clearKnown.

---

## 2026-09-08 — Capture 115603: perfect first emit then silence (DEC-0028)

- ~487 s gapless, SNR ~17.8 dB, DEC-0027 exe. Eyes OK; **0** ReturnEncrypted.
- TG 30302 @ 421.225: first emit **perfect** (duty 0.553 emit=28); operator
  heard response to a missed prior (encrypted TG 12068 on same RF skipped).
- Next hops dsp **470–605 ms** (structure/wrong-slot) → worker-busy; later
  companion-louder mixed → drop B (DEC-0012, correct isolation).
- Root: post-emit `emptyStreakReacq` (even emptyEye-only DEC-0027) arms cold
  240/64; the *following* structure/voice hop burns. emptyEye alone ~102 ms.
  **DEC-0028:** delete post-emit emptyStreak cold escalate; stay hot cand=8.

---

## 2026-09-08 — Capture 112922: structureNoVcw cold burn (DEC-0027)

- ~483 s gapless, SNR ~14.4 dB, DEC-0026 exe. Eyes OK; **0** ReturnEncrypted.
- CADENCE peak **0.639** (0× ≥0.65); worker-busy **417**; wrong TDMA **141**.
- DSP: structureNoVcw med **462 ms** (40× ≥400 ms); emptyEye med 101 ms;
  oppOnly med 202 ms. Soft mask rehunt already existed; cold 240/64 stacked.
- Root: `emptyStreakReacq` still cold-escalated on structureNoTarget /
  StructureNoTargetVoiceWindows / bare emptyStreak≥3.
  **DEC-0027:** cold escalate only on true emptyEye (+ DEC-0026 opp exclude).

---

## 2026-09-08 — Capture 110146: file continuous, live wrong-slot cold burn (DEC-0026)

- ~445 s gapless, SNR ~17 dB, DEC-0025 exe. Eyes OK; **0** ReturnEncrypted.
- 12 CADENCE ok seconds total. worker-busy **382**, wrong TDMA **238**.
- TG 20202 file duty **0.85** vs live 5 ok s; wrong-slot dsp p90 **~434 ms**.
- Root: after emit, opp-slot-only windows cold-escalated CQPSK (240/64).
  **DEC-0026:** exclude opposite-slot-only from structureNoTarget escalate.
- 30017 file 0.74 / live talkMed 0.303 — residual drop D still open.

---

## 2026-09-08 — Capture 103955: RID split is RF; false ReturnEncrypted (DEC-0025)

- Desktop after DEC-0024. Eyes: `context=81920` **0**, 280 ms dominant.
  Operator ~50/50; same TG **20202**, different RIDs.
- **0x1F83FF** @ 420.725: file duty **0.46** PARTIAL; SNR p10 **7.3**. Live
  `no voice sync`×30. Not a RID code path — IQ is hard.
- **0x1F95EB** @ 420.225: file `PASS_CONTINUOUS duty=0.83`; SNR p10 **12.8**.
  Live better then `ReturnEncrypted` while ess still logged clear.
- **DEC-0025:** Clear→Encrypted / grantEncrypted promotion needs MAC/PTT bar
  matching follow `trustedEncryptedEss`. Log ReturnEncrypted reason.
- Residual live drop D / worker-busy on good RF still open (T-0010).

---

## 2026-09-08 — Capture 101644: start/middle BAD = 40 ms catch-up eyes (DEC-0024)

- Desktop exe after DEC-0023. Soft-trim working (`context=573440` ~1000×;
  `163840` only twice). Companion-louder **0**. SNR ~13.8 dB.
- Operator: start/middle unusable; last voice ~90% good.
- First clear TG **30302** @ 420.225: after cold/280 ms eyes, planner
  switched to backlog catch-up `context=81920` (**40 ms**) for ~8 s →
  dutySec **0** / drop **A** / no voice sync. Recovered to 280 ms at
  20:17:07 (brief duty **0.681**) then choppy D/A. Late **10330** stayed
  on 280 ms context (148 hops) — matches last-voice quality.
- SDRTrunk Phase 2 traffic never shrinks CQPSK context to chase lag.
  **DEC-0024:** catch-up overlap **280 ms** (keep 120 ms fresh). Post-arm
  settle 80 ms not the named hole (cold eye already emitted). Live
  re-prove still required.

---

## 2026-09-08 — Capture 095936: good start then collapse = clipped overlap (DEC-0023)

- Desktop `build\bin\Release\SDR_Town.exe` (not stock cand path on voice).
  TG **30302** slot **0** @ 421.225. Companion-louder **0**. Voice SNR
  median **16.7 dB**. Operator: promising start, then unusable.
- Timeline: dutySec up to **0.60** then cliff ~20:00:18 drop **A**; return
  to CC 20:00:22. Talk median dutySec **0.40**, drop **D** dominant while
  speaking.
- Exact fail: rolling soft-trim protected only **80 ms**; live eyes fell
  from DEC-0009 **360 ms** to **160 ms** (110 hops). DEC-0009 already
  proved 160 ms eyes lose the lattice. Hard-cap then jumped the decode
  cursor under 2.6–2.9 s backlog.
- File voicetest of the same call skip=11000: `PASS_CONTINUOUS_AUDIO
  duty=0.685` — IQ is continuous; live path destroyed the eye.
- **DEC-0023:** protect **280 ms**; active rolling **4.0 s**; hard-cap must
  not jump cursor while protected. 105622 still duty **0.645**. Live
  re-prove required on new GUI capture.

---

## 2026-09-08 — Live audio diagnosis: 082235 + rejected speed trials

- Capture `20260908_082235` (stock **0.2.51**, `cqpskCand=32`): CC better
  (first follow ~10.5 s). Talk median dutySec **~0.24**; drop **D** on talk
  seconds; **54** worker-busy; eyes still **80+280** (`fresh=163840
  context=573440`); first job often **720 ms** cold. Companion-louder /
  DEC-0012 still heavy. File IQ of the same class still passes continuous
  because CLI **waits** (~17 s wall / 8 s span).
- **Not guessing hop/TTL.** Measured and rejected:
  - **DEC-0020** sustain 80/0 block eyes: wall 2.8 s, 105622 duty
    **0.645→0.055** drop=A.
  - **DEC-0019 cand=3** after speak (voicetest live proxy): wall ~7 s,
    duty **0.055** drop=A. Hard hint early-stop **kept**; cand stays **8**.
  - **DEC-0021/0022** streaming DDC: after first emit, wrong-slot then
    `p2vcw=0`. 160 ms sustain only reached duty **0.125**. Default-on
    still forbidden (DEC-0014).
- Desktop build retains DEC-0019 **hard** CQPSK hint stop only. **No
  release / push.** Operator must run `build\bin\Release\SDR_Town.exe`
  (not installed 0.2.51) for the next live CADENCE prove. T-0010 open.

---

## 2026-09-08 — Capture 075858: weak CC RF, not a DEC-0019 regression

- Capture `20260908_075858` (199 s, SNR **10.9 dB** vs 060221 **16.9**).
  GUI log `version=0.2.51`, `cqpskCand=32`, sticky=no — **stock release**,
  not the uncommitted DEC-0019 desktop build.
- **CC:** NID BCH-fail 21 vs 3; nid=none 9.6% vs 5%; ~86 s to first follow
  (vs ~12 s). High-correction grant TG 11108 waited hits=1/2 and never
  followed. Expected one-RTL CC pause while on traffic.
- **Voice:** same drop-D islands (talk median duty ~0.31, worker-busy 104).
  File TG 30017 slot 1 skip=85000 duty=**0.66**; TG 20202 skip=133000
  duty=0.50. Late TG 10330 grant=clear but file slice
  `PASS_ENCRYPTED_GATED` — live hung ~39 s without return-to-CC.
- No code change from this capture. Continuity hole remains T-0010 / DEC-0012.

---

## 2026-09-08 — Capture 060221 + DEC-0019 (hard hint stop; no release yet)

- Capture `20260908_060221` (148.8 s): same class as 053241 — DEC-0012
  companion-louder `fed=0` (14/14) + drop **D** (worker-busy 100, dsp
  median 161 ms). LO parks are clean 11.25 kHz. Not a tuner miss.
- SDRTrunk slot model: one HDQPSK stream + two AudioModules filtered by
  timeslot; no opp>target mute; no per-hop CQPSK grid.
- File peak TG 30302 slot 1 skip=128500: duty **0.922** continuous — IQ
  has the voice. First TG 10330 live call never opened (ESS/MAC gate then
  no-vcw); file skip=12000 duty 0.16.
- DEC-0019: block CQPSK hint with **hard** lock stops the grid; hot
  candidates capped at 3 after emit. Soft early-stop rejected (105622
  0.305). File duties match 0.2.51. **No push** until live CADENCE
  improves on a new GUI capture (T-0010).

---

## 2026-09-08 — Capture 053241 short islands are DEC-0012 + drop D (no push)

- Operator on v0.2.51 + `20260908_053241`: less garble, very short emit
  windows. Live: 151 s gapless, dual-TG same RF (TG 12068 slot 1 + TG 30003
  slot 0 on 421.975, LO 421.96375). CADENCE talk-seconds median duty ~0.30;
  worker-busy ~91; DSP median ~161 ms.
- Live DSP worker: 12 companion-louder `mac0 sf>0` hops all `fed=0` (DEC-0012
  hop-wide mute). Emit islands median length 1. Isolation is doing its job;
  dual-TG makes companion louder *normal*, so selected speech becomes short
  islands whenever the other TG dominates.
- Tried narrowing DEC-0012 to **overlap/context only** (fresh selected still
  feed). File voicetest: 053241 TG 30003 slot 0 skip=0 center=421.96375
  `PASS_CONTINUOUS_AUDIO duty=0.715` with companion-louder `fed>0`; but 105622
  fell to **duty=0.62** (stable on two reruns), 073304 **0.72** (was 0.795),
  and 041716 lost `unknown-waiting-clear` (8 companion-louder fresh emits —
  isolation regression). **Reverted.** Isolation outranks duty (SoT). Do not
  push. Next: live drop **D** (T-0010), not reopening DEC-0012.

---

## 2026-09-08 — DEC-0012 feed-only: companion-louder mixed MAC-dead after emit

- Capture `20260908_041716` (70.75 s gapless, SNR ~18 dB). Operator: one
  good emit, then wrong-slot garble. Call 2 seq=131 `opp=0 p2mac=5/6`
  `trusted-clear-pending-release`; seq=134 `target=6 opp=12 p2mac=0/0
  ess=clear` still `gate=emit`. DualSlotUntrusted stayed false (004206 ESS
  escape).
- DEC-0012 landed as **feed-only**, not hop `audio.clear()`. After the call
  has spoken, mixed windows with no selected MAC CRC **and companion louder**
  (`oppVcw > targetVcw`) do not feed unproven bursts.
- Voicetest after the companion-louder narrow (not the first blanket mixed
  mute, which dropped 105622 to 0.27):
  - 041716 TG 10330 slot 1 skip=32111 center=421.21375 8 s:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.87`. seq=134-class hops are
    `unknown-waiting-clear`. Selected-only / selected-dominant hops still emit.
  - 073304 TG 10330 slot 1 skip=107597 center=420.975: `PASS_CONTINUOUS_AUDIO
    duty=0.795` (was 0.76).
  - 105622 TG 30003 slot 0 skip=97334: `PASS_PARTIAL_AUDIO drop=D duty=0.645`
    (was 0.685; two frames under 0.65). Slot 1 same IQ duty=0.09 (was 0.11).
- Live drop **D** (worker-busy 4.5× CQPSK) unchanged. Equal mixed
  (`target==opp`) still ESS-authorized (T-0004). Did push 0.2.51: 041716
  isolation + 073304 duty improved; 105622 stays just under continuous.

---

## 2026-09-08 — DEC-0017 rejected; DEC-0018 does not recover streaming 105622

- Restored DEC-0009 80+280 after DEC-0017 360/0 independent eyes:
  105622 duty 0.685→0.35, 123525 skip=20000 0.64→0.325.
- Re-prove after revert: 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`.
- DEC-0018: streaming DDC no longer jumps the dibit lattice to RF-sample
  time. Env=1 on 105622 is still `PASS_PARTIAL_AUDIO` (0.16/0.09/0.045).
  Do not default-on. Live drop D remains 4.5× overlap CQPSK (T-0010).
- Capture cleanup: deleted 115315 (997 kHz edge), 010500, 064319, 104042,
  161748 (~14 GB). Folder now ~11 GB with the regression set plus a 90 s
  GUI capture `20260907_194706`.
- GUI `--gui-grant-test --p25-cc 420.475 --gui-start-iq-capture` 90 s:
  TG 30302 417.675 LO 417.66375. CADENCE on talk 0.94 / 0.80 then 0.11–0.52
  drop D/A. One worker `dsp=531 ms`. Not an improvement over 123525. Did
  not push.

---

## 2026-09-07 — 123525 capture: LO parked, emit gaps are drop D (+ opposite-slot 80 ms eyes)

- Capture `20260907_123525`: 258 s gapless, SNR ~15 dB. SigMF meta stays
  `center=420.475` (start LO); physical tuner retuned. First call TG 30003
  417.675 `rfCenter=417.66375` offset 11.2 kHz (`single-rtl-retune`).
- Live CADENCE 191 s: emit on 88 s, median duty **0.34**, only 7 s ≥0.65.
  On emit seconds drop **D** 59 / **A** 22 / **ok** 7. `fed≈emit` (1602/1608).
  `dups=2262` vs `target=3892`. Worker `iq=737280 fresh=163840 context=573440`
  (80 ms + 280 ms overlap). Ring `bridge=960` + climbing underruns.
- File voicetest same slice skip=20000 8 s `center=417.66375` slot 0:
  `PASS_PARTIAL_AUDIO drop=ok duty=0.64` (0.01 under the bar)
  `fed=260 emit=260 ambe=228/260 gaps=15`. Many hops `wrong TDMA slot`
  with `oppVcw` only. Not 997 kHz edge. Do not loosen quality/TTL.
  Next named hole: 80 ms independent eyes + overlap dups (T-0006 / T-0004),
  not a new LO.

## 2026-09-07 — 22:27 live follow: LO correct, gaps are soft-quality extract

- GUI `22:27:17`–`22:28:20`. No start/stop IQ file (`capture writer` never started).
- DEC-0016 park worked: TG **10128** voice **420.725** `centerFreqHz=420713751`
  `effectiveTargetOffsetHz=11249`.
- 29 validation hops: `targetVcw=212` `fed=190` `emit=102` `pcm=3.8s` over ~36 s
  (`duty≈0.11`). `iqRej=88`. Gate `hard-soft-quality-low` / `no-decoded-frames`
  / `Phase 2 AMBE rejected`. Eye `softDecisionQuality` mostly 0.33–0.42.
  Drop **A** (extract quality), not off-center tune. Need a real IQ capture
  to voicetest; do not loosen the soft-quality gate from this 1-minute listen.

## 2026-09-07 — 115315 bad audio is 997 kHz edge LO, not a missing gate

- Capture `20260907_115315`: 162 s gapless, SNR ~17 dB. Operator: really bad.
- DEC-0015 used SDRTrunk *set* `getCenterFrequency({cc, voice})`. TG 30003
  421.975 + CC 420.475 `canTune` (span 1.51 MHz < 2.007 MHz usable) →
  `rfCenter=420.97773` **offset=997.3 kHz**. CADENCE drop=D ~0.45 s then
  drop=A no-vcw. Same LO reused for 421.225. Capture 005246 already showed
  750 kHz-offset CQPSK dies.
- DEC-0016: follow LO is always the single-channel voice park (~11 kHz).
  Keep CC only if that LO still `isTunedFor` CC; else pause CC. No
  hop/TTL/feed change.

## 2026-09-07 — Follow LO matched SDRTrunk CenterFrequencyCalculator

- Operator: some follows looked off-center.
- Old park was `voice ± 250 kHz` and reuse of a CC-centered tuner whenever
  voice sat inside ~512 kHz. That is not SDRTrunk. Single-channel SDRTrunk
  center is `voice − 11249 Hz` (12.5 kHz channel + R820T 5 kHz DC hole + 1).
  CC+traffic on one tuner recomputes the LO so neither channel overlaps DC
  (095450: 420.475 CC + 420.225 voice → rfCenter ≈ 420.21375 MHz).
- Code: `include/P25SdrtrunkTune.h` + `p25Phase2LowIfTrafficCenterHz` wrapper.
  Same-wideband follows nudge without pausing CC. Physical one-RTL /
  dedicated / CLI waitgrant use the single-channel park. No hop/TTL/feed
  change. Streaming DDC stays default-off (DEC-0014).
- File voicetest of 105622/073304 still uses capture `--center`, not this LO.
  Re-prove after rebuild: 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`;
  073304 `duty=0.76 timelineOk`. Close/reopen GUI on
  `build\bin\Release\SDR_Town.exe` to see the new LO. Isolation on 095450
  dual-TG remains T-0004.

## 2026-09-07 — 095450 slot bleed is mixed independent eyes, not a missing TTL

- Operator capture `20260907_095450`: dual grant TG 30013 slot 1 + TG 30003
  slot 0 on 420.225 MHz. Sounded like slot bleed, wrong cadence, timing.
- SDRTrunk HDQPSK.receive is one contiguous Costas+Gardner stream, 2048
  samples @ 25 kHz ≈ 82 ms, one AudioModule per timeslot, queue until
  PTT/ESS. Our live path re-channelized 360 ms overlapping eyes (`dsp`
  327–573 ms, seq=2 `decode-wall-timeout`) and fed `targetVcw≈oppVcw`
  with `p2mac=0/x` `ctxDrop=0`.
- **Tried** default-on streaming DDC (DEC-0014). Voicetest 105622 TG 30003
  slot 0 skip=97334 fell duty **0.685 → 0.095** (`emptyWindows=80/89`).
  Same eye-loss as 20260830. Reverted default-on. Env `=1` still opts in;
  locked streaming hops are 80 ms not 40 ms.
- **Kept:** CADENCE `1s` no longer dumps 53 s of hops as one second
  (095450 `windows=593 dutySec=2.320` was `lastLogMs==0`).
- Re-prove after revert: 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`;
  073304 TG 10330 skip=107597 `duty=0.76 timelineOk`.
  095450 TG 30013 slot 1 skip=3741: `PASS_PARTIAL duty=0.625`
  `oppAmbe=759/920` `plc=180/290 concealmentOk=no` — companion still
  reaches the selected vocoder on MAC-dead mixed windows (T-0004 /
  DEC-0012 deferred: that mute dropped 105622 to 0.38).
- Restart GUI on `build\bin\Release\SDR_Town.exe`. T-0010 live CADENCE
  still needs a listen on this binary.

## 2026-09-07 — 073304 repeats were overlap replay, not a missing timeout

- Live `20260907_073304`: audio better after DEC-0008/0009, still garbled +
  repeats. TG 10330 seq=389 ctxDrop=0 after a good emit; CADENCE dups=78–100/s.
- DEC-0010/0011 lock-only after emit stopped those repeats but starved 105622
  missed-eye catch-up (duty 0.735→0.11). Superseded.
- DEC-0013: de-dupe by ISCH lattice `(slot, burstIndex, voiceIndex)` within
  1800 dibits (~300 ms). 105622 `PASS_CONTINUOUS_AUDIO duty=0.685`. 073304
  repeat slice `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`. Mixed MAC-dead
  garble (DEC-0012) deferred — applying it dropped 105622 to 0.38.
- Restart GUI on `build\bin\Release\SDR_Town.exe` to hear it. T-0010 live
  re-prove still open.

## 2026-09-07 — Phase 2 extract: sticky lattice was walking the wrong eye

- ISS-0001 voicetest on `20260905_105622` TG 30003 slot 0 skip=97334 8 s was
  `PASS_PARTIAL_AUDIO drop=D duty=0.28` (fed=112). Hop 2 (720 ms) had real
  Voice2/4; sustain hops were `p2sf/p2mask` with `p2vcw=0` or wrong-slot.
- Cause: block-channelize resets CQPSK/Gardner every hop, then the sticky
  stream-space superframe walk reused the previous eye’s dibit lattice
  (DEC-0008). 160 ms sustain eyes often locked one burst (DEC-0009).
- Fix: sticky lattice / anchor-aligned locks only when streaming DDC is on.
  Locked block-channelize sustain overlap 280 ms so overlap+fresh = one
  360 ms superframe. Hop/minFresh unchanged (80/40).
- Gate: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735` (fed=294, p2macCrc=122).
  Same IQ slot=1 duty=0.11. Live CADENCE on 161748 still T-0010.
- BN-0002: rebuilt Release `SDR_Town.exe` + tests.

## 2026-09-07 — Bring Athanor method to SDR Town; stop P25 hotfix circle

- Athanor repo not edited. Copied process only (DEC-0001).
- Holistic read: analog/CC/follow work; Phase 2 voice is partial (~50%).
  Root pattern is gates fighting (continuity vs anti-garble), overlap+dedupe
  starving unique frames, and ring/bridge filling islands — not one missing
  timeout.
- Landed SoT, cause/effect map, trackers. Retracted roadmap “continuous done”.
- ISS-0001 remains open: need a HEAD voicetest/live CADENCE `drop=` before
  any feed/emit/playout patch (T-0002).
- Started REQ-P2.0 classifier so the next session cannot honestly guess.
- BN-0001: MSVC 19.44.35227 Release; 206 unit tests / 10166 assertions PASS;
  SDR_Town.exe compiles. AppData captures classified 2026-09-07: dominant live
  CADENCE is A (`no-vcw-from-live-window`); HEAD voicetest of 105622 TG 30003
  is PASS_PARTIAL_AUDIO drop=D duty=0.28. Opposite-only dual-slot-untrusted
  predicate corrected (DEC-0003); duty on that slice did not move.

---

## Older P25 work

See `docs/p25_phase2_regression_tracker.md` and `src/*NOTES.md` for July–August
2026 hotfix archaeology. New facts go here.
# 2026-09-17 - 0.2.56 release preparation (DEC-0090)

Publication complete: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.56
Source tag 0acad74 includes all tested implementation and release evidence;
follow-up documentation records upload verification. All eight downloaded assets
match local hashes and the public updater endpoint serves 0.2.56 experimental.
SSTV reference-fixture work remains the next implementation task.

Accumulated workspaces, band plans, RDS/tone adapters, diagnostic automation
and configured RTL runtime are ready for tester packaging. Release helper now
checks commands and actual branch, signing preserves the trust anchor, and
portable/manifest verification has negative tests. README and roadmap updated;
SSTV/satellite are not implemented. 279 C++/Qt cases, four CLI gates, four GUI
layouts and live GUI RDS under CDB pass. Source commit precedes signed packaging;
publication and downloaded-asset verification are separate remaining gates.
# 2026-09-17 - SSTV VIS milestone (DEC-0091)

Implemented native bounded classic VIS inspection and offline `sstv inspect`
for mono WAV/FLAC; JSON distinguishes headers from image decoding. Checked
framing/parity/unknown IDs, all seven-bit vectors, five sample rates, four
partition sizes, resets, bad inputs, silence/noise and repeated headers.
Pinned independent M1 recording passes (VIS 44 at ~0.832 s). Fixed reproduced
Unicode batch/path loss with Qt arguments and wide Windows file opening.
284 core/Qt cases and existing decoder CLI gates pass. Docs/README updated.
No new release published; 0.2.56 release assets remain unchanged. SSTV image
reconstruction, GUI/live input and public satellite decoding remain future work.
