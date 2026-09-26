# Task list (canonical)

T-0059 | done (delivery; hardware/field acceptance open) | SDRplay packaged-driver parity and field regression evidence |
Initial comparison recorded in SDRPLAY_RELEASE_AUDIT_20260926.md. Confirmed
missing bundled SDRplay plugin versus InmarScope; remote failure, missing RTL
checkbox and P25 early-end reports require exact tester build/session evidence.
DEC-0132 extends this pass to correct sourced Aero presets and CI 0.2.98 release.
Source 6f0e3f3; CI release 36204228544/master 36204228658 PASS. Regular public
v0.2.98 downloaded/hash/provenance/presets/RDS/driver-registration checks PASS.
No claim of P25 field-regression repair or physical RSP reception; see audit.

T-0058 | in_progress | README community support appeal |
DEC-0131. Add donation link, verified first commit, sourced AUD estimate and
community acknowledgements. Exact subscription tiers and MIT formalisation
await maintainer clarification; publish only accurately qualified statements.

T-0057 | done | Mandatory CI publication and RTL bias-T tester release |
DEC-0130. Persist the user's always-publish rule, push the pending control
repair, follow Actions to completion, verify a public CI-built portable
prerelease by download/hash/provenance and executable smoke test. Do not claim
an installer/updater publication or physical DC verification from this gate.
Published v0.2.97-experimental from bf83d97; release Actions 36110284353 and
master Actions 36110284332 PASS. Public download/hash/provenance, CLI smoke,
RDS recorded-MPX tests and attached RTL read-only probe PASS. See BUILD_NOTES.

T-0056 | done (software; electrical acceptance open) | RTL-SDR bias-T control chain |
DEC-0129. No RTL bias-T UI, persistence or driver write exists in 0.2.96.
Add a capability-gated, default-off RTL-only path, explicit DC confirmation,
saved desired state, startup/teardown safety and GUI/CLI tests. Do not energize
attached hardware during automated checks; driver readback is not voltage proof.
Implemented in the local development build: adapter 32 assertions, native GUI
20 assertions and DeviceManager lifecycle 51 assertions PASS. Full CTest 13/13
PASS. Connected generic RTL driver advertises biastee and reports OFF; probe
only, no physical ON test. docs/RTL_BIAS_T.md contains the hardware checklist.

T-0055 | done | SDRplay capability and control-chain repair |
User RSPdx report: disabled Bias-T and missing antenna selection. Trace light
enumeration, async open, capability refresh, GUI/CLI/API controls, persistence
and hardware readback. Test actual driver calls with a hardware-free Soapy
fixture; physical RSPdx verification remains required. P25 DSP unchanged.
Source bb91aa6, signed metadata/tag ade17a7, Latest v0.2.96 published.
Final CTest 12/12; core controls 59 assertions; native GUI 50 assertions;
Windows CI 36102563157 PASS. Eight downloaded assets/hash/signature checks
PASS. RSPdx hardware checklist remains ISS-0023 acceptance, not simulated proof.

T-0054 | done | Fluid Aero watch display and independent channel workers |
DEC-0127. Separate visual/status refresh, circular waterfall, multi-click saved
channels, bounded per-channel workers, selected-channel real constellation.
Test ordering/isolation, live reconfiguration, reference PCM and throughput;
leave P25 and global RF/spectrum processing unchanged.
Local implementation complete: full CTest 11/11, serial/parallel real-IQ parity,
four GUI/CLI replay variants, renderer/click tests, actual RTL hot watch edits
and settings restore PASS. Source e46f437, signed metadata/tag d7979ee;
Windows CI 36095455129 PASS. Published Latest v0.2.95 has eight downloaded/
hash-verified assets and a verified signed updater. Extracted portable live
RTL watch edits and all four reference replay variants PASS. Actual RF display
cadence remains limited by the unchanged >80 ms FFT producer; live satellite
speech/antenna acceptance remains separate, not inferred from these checks.

T-0053 | done | Confirmed P25-to-Inmarsat receiver handover |
DEC-0126 / ISS-0021. Reproduce the unconditional host refusal, add explicit
confirmation and quiescence before the existing mode-switch operation, preserve
other-device reception and cancellation, test GUI/host failure cases, release.
Implementation and local checks complete: CTest 11/11, native dialog/failure
tests and three actual RTL GUI-host handovers pass. P25 DSP unchanged. Source
2952535, tag 5fc5fc4, v0.2.94 published with eight downloaded/hash-verified
assets and signed updater. Windows CI 36079172152 PASS; extracted portable
real RTL handover and four reference replay variants PASS. Field Aero voice
and physical RSP acceptance remain separate open items, not inferred here.

T-0052 | done | Saved multi-channel Aero watch and automatic position/voice cycle |
DEC-0124. Reference comparison, bounded scheduler, independent per-channel DSP,
single speaker focus, click-to-place GUI, atomic saved settings, diagnostic events,
deterministic and GUI regression tests complete. Source ca6354e, tag 9fef480,
v0.2.93 tester release published with eight downloaded/hash-verified assets.
Full CTest 11/11, watch 10 cases/929 assertions, reference GUI/CLI parity,
packaged reference voice/data and Windows CI 36073284286 PASS. ISS-0019 fixed.
P25 stays frozen. Field antenna/clear-conversation acceptance remains ISS-0016 /
ISS-0020, not inferred from synthetic events or reference silence. Assignment-
directed follow and dual-SDR reception are separate from this saved-list cycle.

T-0051 | awaiting_capture | Inmarsat tone-only report: live tuning and audio ownership |
DEC-0123 / ISS-0020. Preserve manual voice selection, apply visible tuning on
Start, park ordinary Listen audio during Inmarsat takeover, expose decoded-PCM
and speaker state. Regression-test GUI and lifecycle; preserve reference PCM.
Actual tester tone/clear speech remains unverified without matching IQ/logs.
Repair published in v0.2.93: 20 focused tests, actual live GUI controls and
hardware-free takeover failure/restore tests pass. Four GUI/CLI pacing variants
and a clean-path staged replay retain the reference WAV hash. Final full CTest
and Windows CI pass; downloaded release assets verified. P25 unchanged.

T-0049 | awaiting_hardware | Restore reliable SDRplay runtime discovery and diagnostics |
DEC-0122. Reproduce loader failures, verify actual factory registration, retry
failed API discovery on Rescan, cover installed/portable layouts and architecture.
No P25 DSP, sample delivery, tune sequencing or audio edits. Physical RSP acceptance
requires the affected tester's model/version/log; local PC has no RSP/service.
Implementation published in v0.2.93: app/core/GUI builds, five
executable loader failure/recovery cases, package verifier negatives and exact
protected-pipeline comparison pass. Existing Pothos module registers and actual
CLI reports missing service accurately. No claim of physical RSP acceptance.
Separate ISS-0019 discovered during the initial repair is now fixed by DEC-0125;
final full CTest 11/11 and independent Windows CI 36073284286 pass. Published
installer/portable contents and all eight downloaded asset hashes verified.

T-0050 | done | Diagnose SSTV active-producer lifecycle starvation |
ISS-0019 / DEC-0125. Control admission now prevents hot publishers starving
detach/stats/finish. The unchanged stress case passes ten times in 0.062-0.079 s
per run (before: 13.797 s this pass; earlier >246 s). Full CTest 11/11 passes.
No test sleep or weakened assertion. RF SSTV image acceptance remains separate.

T-0048 | awaiting_reference | Native Inmarsat Aero voice and decoded-position map |
DEC-0121 / ISS-0016. Integrate pinned receive/FEC/Aero codec, shared IQ chain,
validated ADS-C positions and map, sample-clocked output/replay tests. P25 frozen.
Do not close on synthetic PCM or a build alone; record independent sample result.
Implementation/reference subtask complete: shared native continuous/burst Aero,
isolated codec, CRC-valid ADS-C, offline live/replay map and speaker/WAV paths.
Public voice IQ produces identical PCM across four GUI/CLI pacing variants;
public burst IQ yields two real aircraft positions in GUI. Clear-conversation
and antenna acceptance remains open, as do automatic follow and aircraft photos.
Tester delivery complete: v0.2.92 tag 08c59d0, source 7e6eed7, eight hash-verified
assets and signed updater published. Local CTest, extracted portable reference
tests and independent Windows CI 36001921711 pass. Qualification task stays open.

T-0047 subtask | done | Inmarsat IQ replay / diagnostic acceptance harness |
DEC-0120. Bounded file input, shared live/replay processing, GUI and CLI controls,
local diagnostic report and opt-in remote summaries; reject false codec support.
13 focused core tests, GUI controls and actual GUI/CLI paced/fast parity pass.
Four authenticated summaries saved by the real HTTPS collector. Real Aero voice/
assignment/map qualification remains ISS-0016; off-LAN reachability remains ISS-0017.
Source 698dcd8, release tag dac8778: v0.2.91 published with eight hash-verified
assets and signed updater. Local CTest 4/4, extracted-package GUI/CLI checks and
independent Windows CI 35993275475 PASS. This closes replay/diagnostics only,
not Aero protocol/voice reception or the umbrella audit repair task.

T-0047 release subtask | done | Publish SSTV 0.2.90 | DEC-0119.
Source 401e2e3 and tag b8a27dd pushed; GitHub Latest v0.2.90 published with all
eight matching assets and signed updater. Local CTest 4/4, recorded RF/GUI tests,
extracted-package GUI/helper/API checks and remote Windows CI 35986929161 PASS.
All public asset hashes/sizes and public updater metadata verified. RF field
qualification remains open; a pre-existing CI guard baseline gap is ISS-0015.

T-0047 subtask | done | CI release fixture repair | DEC-0118 /
ISS-0014. Reproduce failed run 35979813492, repair packaging tests without
weakening verification, run locally and confirm the remote build/package gate.
Fixed in 2b615ba: run 35984373766 passed all Windows build/test/package/upload
steps in 10m56s. Workflow validation also passed. Runtime/SSTV work unchanged.

T-0047 subtask | implemented / RF qualification open | SSTV RF route audit and auto acquisition |
DEC-0117. Separate image-format Auto from USB/LSB/NFM detection, retain header
pre-roll, preserve manual routes and satellite Doppler, test failure cases.
Release app/core/workspace builds pass; CTest 4/4, six synthetic RF cases,
four recorded-image RF round trips and 16 worker/GUI recording cases pass.
On-air hardware qualification and extended/headerless RF Auto remain open;
manual routes handle those image formats. Existing noise-tail false partials
are recorded in ISS-0013. No P25 changes. Published by DEC-0119 as v0.2.90.

T-0046 | done | 2026-09-24 source/branch reconciliation and receive-chain audit |
DEC-0110; docs/AUDIT_20260924.md. Original folder fast-forwarded to exact 0.2.88
tester source. Local work preserved; 15 exact-tree duplicate remote branches
archived locally and removed. No product fixes/releases in this audit.

T-0047 | in_progress | Audit repair sequence A01-A17 | ISS-0012. Start with measured
HF blanker/throughput/alias tests and per-consumer input diagnostics; then
hardware contracts, P25 metadata isolation (not DSP), satellite oracle and SSTV
integration. Acceptance gates/order are in AUDIT_20260924.md. PR #32 not accepted.
2026-09-24 checkpoint: HF/AM sample clock + blanker/throughput, SGP4 oracle,
metadata isolation/selection, DS/tune error handling and SSTV transport repairs
implemented and tested. A09/A17 and partial hardware/data qualifications remain
open in the audit status table. Accepted P25 DSP unchanged.
Release follow-up: DEC-0114 digital Doppler/short-IQ repair, DEC-0115 lock-order
repair and DEC-0116 production table tests implemented. 0.2.89 experimental is
published (source 560cf85, tag 3820791), with source provenance and explicit
remaining acceptance gates. Full CTest and packaged GUI/helper smoke checks pass;
all eight uploaded assets match local hashes. Umbrella repair remains open.

T-0045 | done | SSTV extra modes + 0.2.78 | DEC-0107 QSSTV/handbook layouts;
Auto VIS then sync period. Robot 24 luma not YC.

T-0044 | done | SSTV Dayton modes + 0.2.77 | DEC-0106. Helper selftest Robot36
round-trip. VIS/GUI/CLI expose 18 crate modes. AVT/Robot B&W/SC2-30/60/120 not
in crate. [sstv] 9367 assertions PASS.

T-0043 | done | Release 0.2.76 | Honest Inmarsat: no fake voice follow/lock
(DEC-0105). Pair FUBAR 1.1.39. docs/INMARSAT.md remaining gaps.

T-0042 | done | Release 0.2.75 | TLE WinHTTP + UI busy flag; DEC-0104 public JSON
omits home lat/lon; T-0041 staging copies `data/inmarsat` and skips leftover
versioned control DLLs. Do not retag 0.2.74.

T-0041 | done | 0.2.74 package follow-up | Shipped in **0.2.75** (T-0042). Original: (1) `data/inmarsat/*.json` missing from
deploy_staging — Inmarsat uses built-in 4f2 fallback. (2) StageRuntime glob-copied
an extra `SdrTownControl-0.2.71-win64.dll` into the Town portable ZIP. The
**pairing** DLL is first-class: FUBAR loads `SdrTownControl.dll` beside
`FUBAR.exe`; testers copy GitHub `SdrTownControl-0.2.74-win64.dll` (or the ZIP’s
`SdrTownControl.dll`) and rename it. Do **not** use the leftover 0.2.71 file
with Town 0.2.74. Do not overwrite tag v0.2.74. See FUBAR_PAIRING.md.
SDRplay API/module remain host-installed (`docs/SDRPLAY.md`).

T-0040 | done | Release 0.2.74 | Tag v0.2.74 at 4f26f2e, branch pushed, GitHub
Latest experimental with installer, portable ZIP, SdrTownControl-0.2.74-win64.dll,
update.json/.sig, SHA256SUMS. CTest 3/3 PASS during scripts/release.ps1.
P25 DSP unchanged. Live RSP/SSTV RF acceptance not claimed. Packaging gaps
are T-0041, not hidden.

T-0039 | done | Tuner lease, home map, Doppler ECEF, TLE/CLI | DEC-0103. Unit tests
[sdrplay],[satcom],[adsb],[inmarsat],[devicemanager] PASS. Live RSP still tester hardware.
Shipped in **0.2.74** (T-0040).

T-0038 | done | Release 0.2.62 | Alias-cache hotfix shipped; later superseded as
Latest by 0.2.66 then 0.2.74 (T-0040). T-0029 Phase 2 string-verifier CI debt
unchanged.

T-0037 | done | Release 0.2.61 | Package CSV alias import + status Alpha Tag
labels; assets and updater verified. Known Windows CI Phase 2 string-verifier
failures remain T-0029 (not introduced by that release).

T-0035 | done | RadioReference CSV alias import | Header-based bounded CSV for
talkgroups and sites (DEC-0101/0102), explicit destination system and staged
review; preserve manual names. TG-SITES-compatible `trs_tg_*` / `trs_sites_*`.
Then resume AX.25/APRS recorded-input backend qualification (T-0036).

T-0036 | planned | AX.25/APRS recorded receive | Evaluate established backend,
pin source/license and independent recording before claiming audio decode.

T-0031 | pending RF acceptance | Live SSTV ingress | DEC-0095 bounded queue and
discontinuity tests first. No RF/UI wiring until streaming conversion and
independent full/partial recording equivalence are qualified.
Queue stage verified: six cases, 20 repeat runs, full CTest and recorded GUI
regressions pass.
DEC-0096 helper transport verified: five Rust units and eight independent
full/partial, forced/auto file/pipe parity cases with rows before EOF.
DEC-0097 converter verified: exact partition equality, reset/error isolation,
six-minute count/drift tests, in-band tones and independent full/partial image
quality gates. DEC-0098 combined worker passes eight independent recorded pixel
parity cases, cancellation, empty EOF and nine injected fault paths.
DEC-0099 adds receiver attach/detach and Live NFM GUI controls. Combined GUI
recording parity and lifecycle tests are the implementation gates; actual
known-transmission RF image acceptance remains open.

T-0032 | done | Talkgroup alias lists (JSON baseline) | Existing Add TG Alpha Tag is persisted
and displayed. Add validated system-scoped alias imports with source/date and
manual override preservation; never use labels as encryption/audio evidence.
Do not assume the same numeric TGID denotes the same group on another system.
DEC-0100 implements New/Add/Edit/Search/Import/Export/Save with system isolation,
source/date and protected manual overrides. Schema/storage/GUI tests and app
layouts pass. CSV/XML, RadioReference and radio-ID/transcript labels deferred.

T-0034 | done | Release 0.2.60 | Package and verify alias-list feature,
signed manifest and downloaded assets before publishing Latest experimental.
Tag3ef2383 pushed, eight downloaded asset hashes match local, published Latest
experimental and public manifest verified. T-0029 remains an existing QA gap.

T-0033 | done | Release 0.2.59 | Experimental live NFM SSTV UI/feed.
Build, combined recording/lifecycle tests, GUI review and package QA before
publishing. Disclose that off-air SSTV image acceptance remains open.
Local build, signed manifest and fresh extracted-runtime gates PASS. Tag
aa5e766 pushed; eight downloaded asset hashes match local. Published Latest
experimental; public updater manifest matches signed local bytes. T-0031 RF
acceptance and T-0029 existing P25 QA gap remain open.

T-0030 | done | Release 0.2.58 | DEC-0093/0094 recorded SSTV GUI,
progressive previews, cancellation, independent pixel parity and transport
validation. Publish source/signed assets only after complete feature gates pass.
Local source/CLI/Qt/full+partial reference and extracted-runtime gates pass.
Tag 4006b94 pushed; eight downloaded asset hashes match local. Published Latest
experimental, public updater manifest verified. T-0029 existing P25 QA gap open.

T-0029 | pending | P25 static-verifier reconciliation | 14 existing hosted/local
string failures (133 passing) at 0.2.56 and 0.2.57. Audit current definitions and
DEC intent, add behavioural gates as appropriate; do not retune P25 or weaken
security/slot checks merely to obtain green strings. See ISSUES/BUILD_NOTES.

T-0028 | done | Release 0.2.57 | Publish DEC-0091/0092 recorded SSTV
VIS and qualified Robot36/Martin1 image decoding with signed installer, portable
and control DLL. Local source/CLI/core/GUI and extracted portable gates pass.
Eight draft-download hashes match, now published Latest; public updater manifest
matches signed local asset. Tag 9347f82; existing P25 static failures remain T-0029.

T-0027 | done | Release 0.2.56 | DEC-0090 checked packaging/publication,
document current feature and validation scope, verify signed assets, push source
and matching release. SSTV implementation remains T-0022, not a release claim.
Published eight assets from tag v0.2.56 after draft upload and byte-for-byte
SHA-256 comparison of every downloaded asset. Latest updater endpoint verified.
Hosted CI is separate and was still running at publication; local gates passed.

T-0021 | done | Shared decoder contracts/registry | Versioned capabilities,
bounded per-stream input, explicit gaps and common CLI/GUI replay/live session.
First adapter must prove RDS parity; do not migrate P25 without its own gate.
DEC-0085 implemented RDS/CTCSS/DCS adapters, CLI registry, shared RDS GUI/file
path and strict stream/domain validation. Recorded parity, 279 core/Qt cases,
CLI and four GUI layout checks pass. Live RDS regression gate failed twice
(1/0 groups on 98.1 MHz); cause unproven, tracked in ISSUES/BUILD_NOTES.
Keep acceptance open and isolate with identical live input before SSTV.
DEC-0086 completes that comparison: 1,446 live blocks, zero differences from
native, zero failed inputs. Station gate remains open for an independent
RF/shared-backend diagnosis; gapless IQ and offline MPX evidence retained.
DEC-0087 closes the station gate: excessive gain reproduced independently
(40.2 dB: zero groups and ~32% raw components at rails; 19.7 dB: 53 clean
groups). Actual GUI at requested 20 dB passes twice: 396 and 261 groups, PI/PS/RT
correct, normal exit; native/adapter live parity also passes. Earlier failed
attempts remain in BUILD_NOTES. P25 unchanged; no global gain default changed.

T-0026 | done | Intermittent native shutdown fault | One gain-test GUI
run exited 0xC0000005 after shutdown began (Windows Event 1000, ntdll.dll).
Subsequent live exits pass; CDB probes did not reproduce an unhandled fault.
Capture fault stack before patching teardown. One subsequent Soapy enumeration
failed; direct RTL check recovered. Do not call caught native faults harmless.
DEC-0089: GUI CDB probe 5 caught AV in RTL/libusb control transfer during
Soapy unmake. Standalone old RTL runtime reproduces AV on cycle 2, without
application DSP/Soapy/Qt. Configured manifest runtime passes 10 native cycles,
then deployed copy passes another 10. CMake now stages that runtime explicitly.
Five real-RX GUI cycles under CDB and 30-second live RDS parity/reception pass;
279 core/Qt cases pass. Release staging hash/license verified. Scope: local
Generic R820T acceptance, not a universal driver/hardware guarantee.

T-0022 | in_progress | SSTV RX | Robot 36, Martin M1/M2, Scottie S1/S2 and PD120
DEC-0093 active: nonmodal recorded-image GUI on shared CLI decoder, responsive
single worker, cancellation/close safety, previews and independent Qt replay tests.
GUI sub-milestone implemented/tested: Tools window, auto/manual modes, PNG list,
partial status, preview, output-folder access. Real Robot36/Martin1 GUI/direct
pixel parity, cancellation/close/teardown and regressions pass (BUILD_NOTES).
Published 0.2.57 was CLI only; 0.2.58 ships the recorded GUI and progressive
previews. Next is bounded live input with discontinuity/retune handling.
reference fixtures, VIS/line sync, slant handling, image preview/gallery/export.
Depends on T-0021; see SATELLITE_AND_SSTV.md / DEC-0083. DEC-0091 starts bounded
recorded-audio VIS identification and independent fixture validation; image
decoding, GUI and live routing are not implemented in this milestone.
VIS sub-milestone done: native bounded detector, offline sstv inspect CLI,
five cases/643 assertions, independent hash-pinned M1 header, file/Unicode
negative-to-positive tests and full 284-case regression pass. Continue with
independent line-sync/image fixtures; do not mark SSTV RX itself complete.
DEC-0092 image sub-milestone implemented: pinned MIT helper, offline CLI PNG/report
export, Robot36/Martin1 auto/manual acquisition and partial-image status. Both
independent recordings pass; actual app/helper pixel parity and input/overwrite
failures pass. GUI preview/gallery, live integration and other modes remain open.

T-0023 | in_progress | Public satellite catalogue/pass planner | 0.2.74 ships
observer map/CLI, CelesTrak TLE (WinINet), compact SGP4, lookAnglesTeme Doppler,
ISS SSTV arm, satcom GUI/API. Unit tests `[satcom][pass]`. Live pass/Doppler RF
acceptance and worldwide catalogue coverage remain open. Depends on T-0021.

T-0024 | open | Weather satellite RX | 0.2.74 satcom has experimental NOAA APT
grayscale preview (not line-sync/slant qualified). Meteor LRPT first for a
claimed weather decoder; SatDump integration/license review before selection.
Depends on T-0021 and T-0023 for live scheduling.

T-0025 | open | AX.25/APRS and public satellite telemetry | 0.2.74 satcom lock
path has an experimental AFSK correlator. Do not claim audio/packet decode until
T-0036 pins a backend, license and independent recording. Depends on T-0021.

T-0019 | open | NFM CTCSS RF acceptance | DEC-0081 independent raw FM
tap, conservative tone bank, GUI display and bounded CLI replay diagnostics.
No audio gating; require independent live tone capture before tone squelch.
DCS requires its own codeword/framing/normal-inverted polarity evidence.
Implementation and automated gates pass: 38 tones, raw NFM tap, CLI files,
GUI status, 45-second hardware routing test. DEC-0082 removes verified data
reset defects; user defers known-tone RF check until bringing their radio home.

T-0020 | open | DCS RF acceptance / maturity | DEC-0084 experimental decoder
implemented: nominal-rate recovery, exact repeated-word validation, physical
polarity and cyclic aliases, GUI NFM display and offline CLI diagnostics.
Eight DCS cases / 74,727 assertions and independent CLI recordings pass. Live
GUI routing passed with matching CTCSS/DCS cursors; known-code RF reception is
not yet independently verified. Further fading/clock/sensitivity qualification
and optional tone squelch remain open. User's radio validation is deferred.

T-0017 | done | Decoder expansion | User explicitly deferred further
P25 optimisation. First RDS milestone: pinned redsea block/FEC reuse, bounded
bitstream metadata decoder/CLI validation, and optional pre-filter WFM MPX tap.
RF demodulation and live GUI metadata require separate validation; not advertised
as available in this milestone. P25 source paths are frozen for this work.
Gate: Release build, 247 core / 4 GUI cases, six RDS/tap cases (49 assertions),
actual CLI reference/malformed/missing-file tests. See DEC-0078 and RDS.md.

T-0018 | open | RDS follow-ups | Tested 57 kHz carrier/timing recovery,
MPX continuity/Windows dependencies, capability registry and live station UI.
Require reference MPX/RF fixtures and no WFM audio regression before availability.
Checkpoint DEC-0079: isolated DSP DLL, MPX file CLI and causal data tap implemented;
nine RDS tests / 210 assertions pass, including upstream recorded MPX and chunk
invariance. Live receiver routing, GUI metadata and RF acceptance are still open.
DEC-0080 checkpoint passed: automatic WFM live routing/status, source-gap reset,
waterfall band sections and release-committed drag tuning. 257 core/Qt cases,
four actual GUI layouts and real drag test; 45-second live 98.1 MHz run received
402 groups with PI/PS/RT. General registry, wider RF QA and text extensions
remain open under T-0018, rather than claiming the entire roadmap complete.

T-0016 | open | REQ-P2.4 / feature QA | Reproduce user-reported live
P25 jitter after workspace/profile work. Compare bounded live GUI captures,
per-call producer/playout counters and reference replay; inspect actual GUI.
Separate supported-feature test results from untested RF services. Do not
change timing/security thresholds without measured cause and regression tests.

T-0016 checkpoint: DEC-0077 resampler/display/settings fixes verified. GUI/CLI
TG30003 replay counts match but PCM does not; isolate that difference before
claiming parity. Live continuity and full RF feature validation remain open.

T-0014 | done | REQ-BP.1 | Country/region/local receive profiles,
source-labelled data, validated JSON import, AUTO priors and waterfall overlay.
Initial country coverage is partial AU/GB/US, not a worldwide allocation table.
Gate: 240 core + 4 Qt cases, four GUI profile/layout checks and byte-identical
CLI/GUI P25 reference WAVs. See BAND_PLANS.md and BUILD_NOTES.md.

T-0015 | open | REQ-BP.2 | Complete sourced country/HF sub-band coverage and
local channel inventories; add capability-aware automatic decoder routing after
each decoder exists and has protocol validation tests. Frequency alone must
never grant P25/audio trust. This is the remaining part of the full-plan request.

T-0013 | done | REQ-UI.1 | User-authorized next-feature foundation:
dockable, persistent workspaces using existing receiver widgets; preset/reset
menu and deterministic GUI screenshots/tests. P25 DSP behavior must not change.
Gate passed: 235 core + 3 workspace tests, four actual-GUI screenshot cases,
byte-identical CLI and GUI P25 reference WAVs. Decoder registry and RDS follow
this layout gate, not a placeholder decoder; they are not implemented yet.

2026-09-17 release task: v0.2.55 from DEC-0069 through DEC-0074 rebuilt and
tested (235 cases); clean packages and signed updater verified. Source/assets
ready for publication. Experimental channel retained; audio acceptance stays open.

Update this file in the same commit as the work. SoT checkboxes move only
after the cause/effect verification gate is green.

Status: `open` | `in_progress` | `blocked` | `done`

---

## Now

T-0010 downstream pass: reproduce/exclude exceptional audio read-cursor races
(DEC-0074), then inspect callback losses separately from decoder feed gaps.

T-0010 current pass: full-frame provenance collected (DEC-0071); physical
mapping and missing block-tail defects fixed/tested (DEC-0072/73). Live
underruns and remaining valid-frame loss remain in progress. Evidence and
non-completion caveats: `P25_MAPPING_AUDIT_20260917.md`.

2026-09-17 follow-up (T-0010 remains in progress): slot-state mismatch reproduced
and corrected with clear/encrypted companion-slot tests (DEC-0069). Reference
103841 retains four additional frames; 060515 still has six feed gaps. GUI EOF
tail priming also reproduced and fixed (DEC-0070). Remaining live continuity
and concealment are not closed by these scoped repairs.

2026-09-17: T-0010 remains in progress. Capture 060515 isolates excessive RS
recovery work; exact GF64 tables and cached syndrome columns reduce replay
wall time without changing the 103841 reference WAV. Six feed gaps remain
on 060515. Version 0.2.54 is an experimental measurement build, not completion
of REQ-P2.2-6. Evidence: `P25_AUDIO_FORENSICS_20260917.md`.

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0013 | in_progress | CI / B-0020 | GitHub Actions Windows Release build + `sdr_town_tests` + Phase 2 string verifiers |
| T-0010 | in_progress | ISS-0001 | DEC-0038: stream env=1 still duty 0.23 on 060036 (block 0.705). Default-on off. Live path = block + DEC-0035/0037; re-prove ~095846. |

---

## Backlog (do not start early)

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0004 | open | REQ-P2.2 | Mixed MAC-dead first hop still drops target=6 opp=28 (105622 hop 1). Post-emit mixed skip is DEC-0012. Do not reopen dual-slot 034136. |
| T-0005 | open | REQ-P2.3 | Emit proven PCM (only if drop=C) |
| T-0006 | open | REQ-P2.4 | Playout duty after extract rate is honest (drop=D residual) |
| T-0007 | open | REQ-P2.5 | Follow hold (only if drop=E) |
| T-0008 | open | REQ-P2.6 | Isolation non-regression beside every P2 change |

---

## Done

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0009 | done | ISS-0004 | DEC-0040: split orchestration out of mega-`main.cpp` into focused TUs; Release build + 214 tests + 129 string verifiers green |
| T-0012 | done | ISS-0001 | DEC-0015 tuner LO = SDRTrunk CenterFrequencyCalculator. Units: voice−11249; 095450 pair → 420.21375 MHz. File voicetest 105622 duty=0.685 / 073304 duty=0.76 unchanged (capture `--center`). |
| T-0001 | done | REQ-0.1 | Athanor-method desk: SoT, cause/effect, trackers; retract false continuous-done |
| T-0002 | done | REQ-P2.0 | Classified AppData captures + HEAD voicetest (ISS-0001) |
| T-0003 | done | REQ-P2.1 | 105622 TG 30003 slot 0 skip=97334 8 s: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735` after DEC-0008/0009; DEC-0013 re-prove duty=0.685. Slot 1 same IQ duty=0.11. |
| T-0011 | done | ISS-0001 | 073304 TG 10330 slot 1 skip=107597 8 s (live repeats): `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk` after DEC-0013 lattice de-dupe. DEC-0010/0011 lock-only starved 105622 and were superseded. |
