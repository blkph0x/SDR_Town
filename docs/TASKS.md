# Task list (canonical)

T-0123 | in_progress | Station front-end LNB, Bias-T, rotator arm, D1 survey (DEC-0210) |
Separate `frontend` session. Claimed NF, IF conversion, fail-closed Bias-T,
arm checklist, `StationPassSession` 1 Hz `rotctld` ticks, abort stop/park/
power-off, metric JSONL, spectrum survey, clear-TS PID inventory.
Read/Follow armed pass copies planner AZ/EL and Doppler RF when the look
and TLE age pass `skyFeedFromPass`. Pass folder is profile JSON plus
metrics JSONL. GEO park slews once. GEO box scan is capped at 49 dwells
and does not declare a peak. Manual does not move. LEO lead is one
observed step from the last two planner samples. Horizontal is 18 V and
high band is 22 kHz. The station panel and Tools rotator share one
`rotctld` client. A selected radio index queues the IF and can request
internal Bias-T. External Bias-T does not enable the dongle supply. A supply port must
answer OK to 13/18/OFF. Header frequency and phase feed PLS. QPSK symbols
become pre-FEC hard bits after the PL Gold code. A BBFRAME can yield a
clear transport stream and omits scrambled packets. Short-frame nominal
rate 1/2 BCH and LDPC correct hard bits. Other rates are not implemented.
Lease/IF boxes stay
attestations. No Hamlib link, no P25
edits, no commercial decrypt. D2 demod not linked. Tests: `frontend_station_tests`.

T-0122 | in_progress | Listen identity, unused stream stop, HF filter (ISS-0089 / DEC-0209) |
PR #34 does not merge (draft, frozen P25 CI). Extract Listen `stableKey`
persist + stop unused Listen streams, analog 200–2800 Hz HF filter below
30 MHz, display DC/spectrum controls, diagnostics teardown. No P25 DSP.
Tests: `Regression-hf_audio_filter`, `Regression-listen_device_selection`,
`Regression-spectrum_dc`, `test_no_p25_guard.py`.

T-0121 | done | Empty-ring underruns and dual-SDR overflow (ISS-0088 / DEC-0208) |
Collector: ~100 WASAPI underruns/s with ringFill 0 on HyperX/VB-CABLE, all
analog modes, including RF stopped; RSPdx+RTL liveIoWaitUs/overflows from
one global Soapy lock. Stereo callback, start after two periods, per-USB
IO mutex. No P25 DSP. Tests: `[audioengine]`, `[satcom][log][ownership]`,
`[tx-safety]`. Public signed Latest v0.2.134 verified (tag v0.2.134, source
cfdb6a3, installer SHA256
78f500fa904da0ff5f69d559ca42eee449c440cd0e3dcf584bb1dac743719425).
Portable-only v0.2.134-experimental remains
b248823f6fd173874700d4daf9aeda54315783f20b88f8d94b42a28de04027bd.

T-0120 | queued | Quarantine USB identity after leaked RX/TX Soapy handles (ISS-0085) |
`stopStreamingImpl` leaks the Soapy handle on `rxDetached` and still
resets `StreamState`. `startToneTx` can `Device::make` after a leaked
`stopTx`. Next open must not reuse that USB identity while live IO may
still hold `gSoapyLiveIoMutex`. After T-0119. No unmake-after-detach.

T-0119 | done | Unstick wedged TX stop from gSoapyLiveIoMutex (ISS-0084 / DEC-0207) |
`stopTx` try_locks the live-IO mutex after detach. A wedged `writeStream`
leaks the Soapy handle instead of hanging process exit. Reclaim on a later
idle stop. Hardware path is `startToneTx`, not the P25 voice stub.
`[tx-safety]` and DriverIoMutex try_lock tests lock it. No voice heuristics.
Public v0.2.133-experimental verified (Actions 37735043869, SHA256
997e4cbcbb124d0526e5cdae78c520310f44dc6de44bf353f5ba1b136f09ea04).

T-0118 | done | Default-deny loopback control + constant-time token (DEC-0206 / ISS-0083) |
Empty token is unauthorized. Health stays open. `/v1/status` and mutating
routes require Bearer or `x-sdrtown-token` unless
`--control-allow-unauthenticated`. `RemoteDiagnostics` / `[control]` ctest PASS.
No P25/DSP change. Public v0.2.132-experimental verified (Actions 37725002808,
SHA256 0835727e6563e139a82834f5a7ad0121feb812d5cf8e0d94832aba2f2b93ff88).

T-0117 | pending_live_acceptance | Stop satcom auto-capture from locking Listen at open (DEC-0205 / ISS-0082) |
Saved autoCapture=true was seizing the radio onto SO-50 436.795 MHz when the
GUI opened, so Monitor Freq could not retune. Default/named engines now require
a fresh Auto-capture check this session. No P25/DSP change.
Public v0.2.131-experimental verified (Actions 37614874125, SHA256
28fd34c892e3aab3c1f840515290b6b33cb82515adf1eaa733d7698227dd1564). Reopen
with Auto capture unchecked and confirm Listen retune.

T-0116 | pending_live_acceptance | Encrypted P25 grant IQ capture button (DEC-0204) |
Tools/P25 "Record Enc Grant IQ" starts live IQ, follows known-encrypted grants
with speaker muted, holds until teardown/carrier-drop, writes timing log.
Does not decode encrypted audio. Default auto-follow skip/return unchanged.
Public v0.2.130-experimental verified (Actions 37611289917, SHA256
1559dd5e4716c63d87cde4e2a92419f7cc9e86d4cafdb82f758ca694cb1cb4dd). Live
encrypted grant capture on a CC still required. Do not use 0.2.130 if the
GUI locks on 436.795 MHz; that is ISS-0082 / 0.2.131.

T-0115 | pending_live_acceptance | P25 locked-lattice empty-hop search (DEC-0203 / ISS-0080) |
Capture 093930: mid-call duty 0.439 sits on empty hops that still have SF+mask
locked; DEC-0048 then spends cand=16/120 on the next unique-speech window.
Keep healthy 80/4 on the first locked-lattice empty; escalate on the second
or when structure is gone. No minFresh/PLC/slot/security change. Public
v0.2.129-experimental verified (Actions 37448227980, SHA256
48108ba4b131eda0835e463bd8e4b101249440c245116d4c59ebb40c9f2a461b). Live
093930-class mid-call dip re-prove still required.

T-0114 | classified_no_code | P25 093930 remainder (ISS-0080) |
v0.2.128 capture 093930 at 423.35 MHz: gapless IQ, CLEAR 88.9 s WAV, audio-open
duty 0.758, hops 1080×208+280. Remaining holes are PTT/retune first-eye,
RF/MAC lattice (`gaps=1` / imperfect MAC), one vocoder-zero at seq=454, and
encrypted TG12068 mute. Audit underpush is ordinal de-dupe. No hop/PLC/security
change without a non-regressing saved-IQ sweep.

T-0113 | done | P25 residual active-clear gap repair (DEC-0201/0202) |
Capture 081738 on v0.2.127: gapless IQ and clear audio restored, but active
epochs duty 0.582/0.633 on dominant 160+280 windows. Six-call replay sweep
selects active-only 200 ms minimum. Live capture 090937 on v0.2.128: gapless
IQ, 208+280 hops, long-call duty 0.88–1.07, listen=CLEAR, two leftover 20 ms
lattice zeros (`gaps=1`) not closable without PLC. Keep 240/200/280.
Public v0.2.128-experimental verified (Actions 37438167836, SHA256
c35ebcb404e8aa9bb1c89dae43197cb7459cd4dda955dc94821a39dcf6f60953).

T-0112 | done | Revert regressing P25 realtime catch-up (DEC-0200) |
Capture 075758 on 0.2.126: gapless IQ, empty speaker WAV, mass AMBE rejects.
Removed realtime 360/160 profile; kept spectrum worker. Published
v0.2.127-experimental; live re-prove on v0.2.128 capture 090937 (CLEAR WAV,
not empty, two AMBE-reject lines).

T-0111 | pending_live_acceptance | P25 emit-gap repair (IQ continuity + live cadence) |
DEC-0199: spectrum worker off Soapy readStream (kept). Realtime 360/160/280
catch-up caused 075758 AMBE/audio collapse and is reverted by DEC-0200 /
T-0112. Prior evidence 062201 bucket A overflows.

T-0110 | pending_release | Classic Aero audit validation and hardened recovery |
DEC-0198: confirm audit claims against source, repair ACARS digit reassembly,
preserve 8400 codec state across erasures, mute flagged voice words, and add
regression fixtures. Keep scheduler/parser recommendations evidence-bound,
preserve P25, then run full release qualification and publish a tester asset.
Local native and full17/17 CTest gates pass; exact source CI and public asset
verification remain.

T-0109 | in_progress | Aircraft map direction, RF watch planning and workflow audit |
DEC-0197: confirmed 1529-1530 MHz, applied ADS-B rate, ACARS and voice/data
association tests; preserve P25. Build, test and qualify public release.
Local17/17 suites,1550 focused assertions, RDS/DTMF CLI and visual checks pass.
v0.2.124-experimental publication/anonymous verification pending.

T-0104 | done | Close the four finite publication requirements (DEC-0196) |
Public v0.2.123-experimental verified; release Actions37273822180 successful.
Acceptance: commit1b035aa comment203450320; public144-file package, zero blockers.
User priority2026-10-05: finish materials, distribution scope, independent USB
rebuild, compiler-runtime provenance, then publish and verify a new tester asset.
Do not change P25 or use a permanent untestable release-blocker list.

T-0108 | pending_after_release | P25 first caller clear, missing response diagnosis |
DEC-0194: passive per-stage capture timeline, CC stale-result fencing and
logging/synthesis non-interference tests. Re-measure live gaps without changing
audio/DSP timing. Public package work remains T-0104, not a waived gate.
Current-tuning CC input and startup applied metadata repaired; full17/17 PASS,
both replay WAVs identical across normal/validation/deep modes and baseline.
First trace confirms53.28s of unsubmitted CC IQ while enabled; no significant
voice-job queue delay. Final live repeat and exact source CI acceptance follow.
DEC-0195 repairs missing MSBuild/transitive helpers in the release source kit;
actual981-file export and5 tooling tests pass. Four package requirements remain.
DEC-0193 continuation: qualify repeated selected FACCH teardown and allocation
identity before changing follow behavior. Keep decoder/audio baseline unchanged.
Scoped repair locally verified: full17/17,65 focused assertions, identical
first-caller/tail PCM,373.904s gapless live GUI run/six confirmed returns.
Exact pushed-commit CI remains required. T-0108 stays open: active in-passband
CC monitoring and independent RID-level reply acceptance are not completed.
2026-10-05 user redirects from controller work. Preserve6805bf6 baseline;
capture a complete live GUI exchange, compare grant/TG/RID/slot/ESS/PCM epochs
and replay identical IQ before any narrowly evidenced repair. DEC-0191 /
ISS-0073. Do not assume slot leakage or alias failure from the symptom alone.
DEC-0192 observer fixes locally pass97 assertions/16 cases and full17 suites;
paired first-caller and later-call WAVs are byte-identical before/after.
600.032s gapless live IQ establishes a missed CC allocation during follow.
Do not close T-0108 on these repairs: prompt reply handoff remains unproved.
Previous controller source6805bf6 passed Windows37240056799 and YAML37240056806;
public package gate remains separate. T-0103 controller extraction deferred
while this user-requested diagnosis is active.

2026-10-05 continuation: T-0103 / DEC-0189 Satcom/Aircraft controllers locally
verified; exact source CI pending. Named planners/audio/tracks/settings, GUI/API
controls, hidden-worker continuity, isolated stop/reopen and log concurrency
are implemented. Native17/17 PASS86.44s; actual session APIs, four workspace
launches and101 Aircraft/Satcom GUI assertions pass. Final verification follows
the observer-isolation and stronger producer/hide fixtures. P25 per-system
CC/follow controller extraction remains open; concrete shared decision state
and required acceptance are recorded in WORKFLOW_DEVICES.md. No frozen DSP/
audio edits, physical multi-radio qualification or public release claimed.
Previous26716b3 exact CI passed
(Windows37209955622, YAML37209955649; acceptance comment203373945).
DEC-0190 additionally fixes reproduced mixed-workflow startup starvation with
FIFO driver admission, preserving critical sections and timeouts. Five mixed
worker and five GUI runs PASS30.16s;271 FIFO/log assertions pass. Final full
suite and exact pushed-commit CI are required before source acceptance.
Final DEC-0189/0190 build and full17/17 PASS84.23s; real three-workflow GUI/API
automation PASS. Source push/Actions acceptance pending; P25 extraction and
ISS-0060 remain open, not reported as completed by these controller gates.


2026-10-05 active continuation: T-0103 / DEC-0187 replaces the Inmarsat
singleton restriction with named, isolated engines, maps and GUI/API controls.
Two real engines consume mock-radio IQ concurrently; stop/ownership conflicts
and settings/message isolation pass. Initial full native gates17/17 PASS81.33s.
Final native17/17 (82.66s), actual GUI Inmarsat/SSTV session automation,
four layout launches and33 map/popout/session assertions PASS. Exact source
CI remains pending this commit. Final input-validation follow-up native17/17
(82.25s), actual GUI session API and negative guard tests also PASS.
T-0104 / DEC-0188 adds the missing dependency build-tool kit (scripts, triplets,
bootstrap metadata and receipt-verified CMake helpers);5 new tooling tests and
20 inventory tests pass. Compiler-runtime evidence, independent USB rebuild and
distribution review remain open. Repeated P25/Satcom/Aircraft controllers and
physical multi-SDR acceptance remain T-0103 work, not marked complete.

T-0107 | source verified / binary gate independent | GUI dry-run satellite auto-capture isolation |
DEC-0176 / ISS-0066. Reproduced while qualifying T-0104's actual package.
Prevent saved automatic pass capture from opening RX during layout QA; preserve
normal hardware operation and settings. Gate: packaged four-profile GUI smoke
with suppression evidence and no transient RX, native suites and source CI.
Four packaged GUI profiles and16/16 native suites PASS after repair. No frozen
pipeline edits; T-0104 resumes.8c8b529 Windows37150938612/YAML37150938614 PASS;
acceptance comment203306079 records exact inventory/hashes. The four actual
GUI layout launches passed locally; CI runs the native Qt suites, not that script.

T-0106 | source verified / binary gate independent | Control shutdown crash exposed by final source CI |
DEC-0173 / ISS-0065. CI37117769454 and local repeat/CDB reproduce a socket
lifetime failure during reentrant cancellation on unchanged application code.
The deterministic ordering fixture fails before production changes. Queue
guarded dispatch outside native read notifications, test retired/deleted sockets
across restart, then stress/debug-heap and full local/CI gates. This confirmed
CI blocker interrupts T-0104; do not weaken tests or change receive/audio code.
Ordering fixture FAILS before repair and passes after.100 independent control
suite processes/CDB check and full16/16 local suites PASS. Final buffered-request
retirement fixture passes59 assertions. Track exact pushed commit CI before
claiming source acceptance. Binary gate ISS-0060 remains independent.
Post-push92ab955 Windows37120041634 and YAML37120041639 PASS; downloaded
122-file inventory source/hash verified. Final acceptance evidence is on that
commit, comment203265356. No binary published while ISS-0060 remains open.

T-0102 | source repairs verified; binary publication blocked | Shared infrastructure hardening and diagnostic evidence |
DEC-0171; baseline d47f000 / 0.2.122. User authorizes the review follow-up and
requires tracked progress. Preserve P25/FM DSP, vocoders, speaker gates and
recording consent. Complete independent testable repairs before shared RX work.
- [X] Diagnostics opt-in lifecycle, monotonic heartbeat and bounded status GET (local tests pass).
- [X] Loopback control request bounds, shutdown, parsing and error redaction (local tests pass).
- [X] Explicit fail-closed hardware tone-TX configuration and authorization (mock-driver tests; no RF TX).
- [ ] Binary dependency notices/source inventory and release packaging gate.
- [X] Hardware-loss publication with executable overflow/epoch tests (live field acceptance still open).
- [ ] Physical-device ownership tokens and worker-lifecycle fault tests.
- [X] Per-stream read/wait/error counters and nonblocking HF diagnostic publication.
- [ ] Per-stream latency histograms, operation correlation and independent hang watchdog.
- [X] Local Release build and 16/16 regression suites; exact-patch guard negative tests.
- [X] Source CI and downloaded CI-artifact hash/provenance/smoke (65e3da3; run 37111782581).
- [ ] Actions release and public downloaded-asset verification (binary gate ISS-0060).
RF qualification, physical two-SDR arbitration and full driver-hang recovery
must remain open unless their acceptance evidence is actually obtained.
Repair details and follow-up order: INFRASTRUCTURE_HARDENING_20261003.md.
The completed source batch is 90edc4a + 65e3da3. T-0103..0105 below retain the
unimplemented larger work; there is no new public binary or version bump.

T-0103 | in_progress | Device ownership, workflow assignment and worker lifecycle qualification |
DEC-0185 local PASS: visibility no longer cancels radio/decoder workflows;
named SSTV and additional CW/DTMF windows use per-instance workers. Two-worker
cancellation/settings tests, actual GUI API automation, four layout launches
and17/17 CTest suites pass. Exact pushed-commit CI still requires acceptance.
User also requests remaining T-0104 release work in this pass; independent
evidence is required for both and neither implies completed P25 multi-system RX.
DEC-0186 supplies the exact committed application and three external module
sources in the release package, with nested hash/revision validation and
dirty-input/secret-exclusion tests. Qt's completed rebuild is no longer listed
as unfinished. Remaining distribution/toolchain reviews in ISS-0060 still gate
public binaries; this is evidence progress, not release completion.
DEC-0181: user requires identical ownership/selection across ALL modes, demods
and workflows, with repeated instances; P25/SSTV are examples. First
milestone: persistent per-device workflow reservations, per-device leases/token
invalidation, visible conflict/status UI, P25 pool isolation and dedicated SSTV.
Concurrent P25 controllers/calls and driver-hang recovery are separate remaining
milestones; do not present an assigned pool as concurrent call decoding.
DEC-0182 adds selected-radio GUI/CLI/web routes, exact satellite restoration,
per-radio GUI takeover, aircraft worker ownership and logical receiver pickers
for CW/DTMF/SSTV/RDS/tone status. Current migration matrix and concrete limits:
WORKFLOW_DEVICES.md. Main Listen's remaining raw lifecycle, primary index
assumptions, repeatable P25/satellite engines and source/output persistence stay
open. P25 DSP/vocoder/speaker cadence are unchanged; selectors are not proof of
independent concurrent P25 controllers.
DEC-0183 isolates primary repeater pair tuning/status from secondary NFM VFOs;
this controller-only follow-up has local17/17 and exact-guard evidence.
DEC-0184 closes legacy hardware-setting/start bypasses using per-domain control
permits, exact-token settings and a scoped startup catch-up. Pre-repair fixture
reproduced five unauthorized setting changes. Main gain targets the selected
radio; GUI/CLI/web report refused controls. Slow-driver command/stop/reclaim
tests and existing hardware control regressions are the acceptance gates.
Local full17/17 PASS78.20s,45 lifecycle repetitions PASS80.58s and four GUI
profiles PASS. ISS-0068 receive-control repair is locally verified; exact CI
acceptance is recorded on the published commit rather than inferred from build.
Repeated controllers and driver-process isolation remain distinct open work.
Depends on T-0102 loss-boundary/source regression evidence; ISS-0055. Define
stable physical identity and generation-bound consumer tokens, including shared
SDRplay tuner domains and ambiguous duplicate serials. Inventory every raw tune/
release caller before migration. Gates: two independent devices, two consumers
of one device, stale token after reopen, forced takeover, unplug, slow/stuck
make/read/stop, stub handoff and destruction. Do not simply remove the global
driver mutex or block the GUI on an unbounded join. P25 callers require a new
reviewed change and capture/field acceptance, not this pass's guard exception.

T-0104 | remaining materials/review / binary gate | Exact binary dependency notice/source kit |
DEC-0172: first implement exact-file inventory, staged original/vcpkg notices,
binary-to-build-input checks and negative ZIP tests. Do not label mechanical
coverage as complete transitive licensing or silently clear source/runtime
distribution questions. No RX/DSP or live server changes in this pass.
- [X] Project/codec/configured-vcpkg notices and exact input metadata staging.
- [X] Deterministic inventory, archive integrity and malicious/missing input tests.
- [X] Missing RTL USB runtime repair and isolated loader positive/negative gates.
- [X] Enforce remaining source/notice block in CI, local helper and direct CPack.
- [ ] Complete Qt/RTL/libusb/compiler-runtime source/notice/rebuild materials.
- [X] Qualify clean local staging without old/tooling DLLs (ISS-0064).
  DEC-0174: declared runtime inputs, shared configured Qt/MSVC deployment,
  bounded clean destination and no raw-tree CPack fallback; negative fixtures
  and staged executable/loader/GUI checks before source CI.
  Local9 staging/16 inventory/17 release tests,16/16 native suites, isolated
  loader/CLI and four staged GUI profiles PASS. Actual124-file ZIP verifies;
  c2231bf Windows37139041871/YAML37139041873 PASS; downloaded125-file inventory
  checked, final acceptance comment203291204. Five source/notice blockers remain.
- [X] DEC-0175: collect and independently verify exact vcpkg upstream archives,
  port recipes/patches and installed receipts; add hostile/missing-input tests.
  No source replacement, decoder changes or inferred publication clearance.
  Local10 archives/48 recipes,12 exporter and18 inventory tests PASS; full16/16
  suites pass.8c8b529 Windows37150938612/YAML37150938614 PASS. Clean CI includes
  10 archives,3 downloaded backport patches,48 recipes;126-file inventory checked.
- [X] DEC-0177: preserve per-file ASN.1 copyright notices and pinned data
  dedication/provenance; verify exact bandplan copies and negative fixtures.
  Local11 notice/19 inventory/12 source/9 staging/17 release tests PASS;
  deploy,16/16 native suites, isolated RTL loader and129-file ZIP verify PASS.
  8eaedb8 Windows37154345677/YAML37154345681 PASS. Downloaded130-file
  inventory verified; exact acceptance comment203310141. Five full-release
  gates stay open; no new binary published.
- [X] DEC-0178: pinned Qt6.7.3/6.11.1 module source kits, source notice catalogs,
  SDK feature headers and replacement instructions; verified inside the ZIP.
- [X] Exact Microsoft runtime file/bundle versions, signed original installer
  and embedded end-user license; distinguish publisher redistribution rights.
- [X] Local source-built QtSvg library/plugins, pixel checks and application
  CLI replacement test; original SDK/package unchanged. Full Qtbase rebuild
  and distribution review remain open.3e4956e Windows37160094253 and
  YAML37160094287 PASS; downloaded135-file inventory and Qt6.7.3 replacement
  result verified, acceptance comment203316479.
- [X] DEC-0179 local: source rebuild of Qtbase+QtSvg, complete packaged Qt replacement,
  networking/TLS/rendering and actual CLI/four GUI dry-run gates; preserve SDK,
  package and RF behavior.16 Qt6.11.1 replacements/4 no-RX GUI profiles PASS;
  96 packaging/helper tests and16/16 native suites PASS. Input hashes and
  optional-feature differences recorded. Independent CI6.7.3 now PASS10259ca;
  no claim of all Qt modules, exact upstream feature reproduction or licensing
  completion from runtime smoke. Remaining source-kit/toolchain review below.
- [X] DEC-0180 / ISS-0067: source CI full rebuild passed but large native GUI
  viewport was clamped. Keep native smoke, add recorded desktop metrics and
  exact-size headless layouts using rebuilt QA-only qoffscreen/installed fonts.
  Local full source-built integration PASS747.656s, native listening plus all
  four offscreen profiles,98 helper tests.10259ca Windows37168422913 and
  YAML37168422917 PASS; downloaded17-runtime result/135-file inventory match.
  Acceptance comment203327059. These technical checks are closed, not pending.
- [ ] Full transitive/combined-distribution review, then public asset verification.
15 new inventory tests, 17 release-verifier tests and 16/16 native suites pass
locally. Source929cb11 Windows CI37116308564 and YAML37116308593 PASS; clean
ZIP/loader gates PASS and downloaded122-file inventory verified. Evidence-only
artifact upload confirmed. PACKAGE_HARDENING_20261003.md records
coverage and limits. No new binary/version, no P25/DSP changes.
ISS-0060. Inventory actual staged runtime/plugin/helper/data files and versions;
include root license/scope/credits plus all upstream notices and required
corresponding source/build/relink materials. Test missing/mismatched inventory,
download the eventual public asset and verify it. Do not treat dependency URLs,
upstream acknowledgements or a green compiler run as completion of this gate.

T-0105 | queued | Diagnostic timing and field-quality expansion |
Bounded per-stream latency histograms and operation IDs; independent GUI/worker
hang detection with opt-in privacy and rate limits. Then measured WFM blocker/
RDS/CPU sweeps, fading AM/weak adjacent SSB and hand-keyed Morse/DTMF corpora.
Existing clear NFM/WFM/P25 receive behavior remains the regression baseline.


T-0101 | done (documentation correction) | Correct reference-versus-code licensing attribution |
DEC-0170. User clarifies SDRTrunk was used for principles, not copied code.
Recheck helper history and technical decisions; retract the unsupported
confirmed-GPL-derivation conclusion without claiming a complete provenance
audit. Preserve actual third-party notices and binary notice work. Docs only.
146 local links and P25 guard self-tests PASS; source CI remains the
publication check, with no new application version or binary release.

T-0100 | done (source documentation; binary licensing remediation open) | MIT grant and evidence-based acknowledgements |
DEC-0169. Replace the non-license root notice with standard MIT terms for
original SDR Town contributions; retain separate third-party terms. Inventory
bundled components, references, data sources and development tools. Do not
claim the combined binary or upstream-derived code is MIT-only. Document
provenance/distribution gaps, preserve all DSP and publish documentation only.
Standard grant/local-link checks and P25 guard self-tests pass. Source CI is
the publication gate; no version/asset change. T-0101 / DEC-0170 corrects the
unsupported confirmed-derivation conclusion in ISS-0059. Actual binary notice
coverage (ISS-0060) remains open, separate from reference ideas or the MIT grant.

T-0099 | done (bounded scope; field qualification open) | Bounded fast-burst and transformed DTMF analysis |
DEC-0168. Reproduce confidence normalization and short-burst/EOF defects;
qualify polarity-independent detection, explicit fast and frequency-inverted
profiles, thread-safe events and chronological dual-watch input. Add operator
controls, negative fixtures and sample-time diagnostics. Preserve P25/audio.
Published 0.2.122-experimental from 60258a7. Source/release Actions and YAML
checks passed. Anonymous public ZIP/hash/provenance and shipped DTMF/RDS smoke
verified. No arbitrary scrambling recovery or field talk-off acceptance claim.

T-0098 | done (bounded HF/CW scope; hardware acceptance open) | Receive-chain audit, HF integrity and Morse window |
DEC-0167. Validate the supplied audit against badcba4, record confirmed and
unsupported claims. Reproduce HF invalid-input and same-station correction
resets, add bounded diagnostics and tests, integrate a pinned Morse backend in
an independent receive-only window. Preserve P25/FM production paths; qualify
synthetic RF/audio, GUI lifecycle and published Actions assets. Hardware CW,
fading AM/SSB and broader device ownership remain separate acceptance gates.
Published 0.2.121-experimental from 1d5b65a. Source/release Actions passed;
anonymous ZIP checksum/provenance, Morse license, opt-in collector config and
shipped CLI/RDS smoke verified. Detailed evidence is in BUILD_NOTES.

T-0097 | done (UI scope; hardware acceptance limits documented) | Desktop and companion usability qualification |
DEC-0166. Run both shipped apps and web workflows; improve workspace navigation,
main receiver space, keyboard access, mobile layout and visible command feedback.
Reproduce polling/draft defects; preserve DSP, leases and privacy boundaries.
Qualify local builds, browser/Qt tests and matched public Actions releases.
Published 0.2.120-experimental / FUBAR 1.1.44 from 6dc66fa / d9bcb0a.
All source/release Actions passed; anonymous public ZIP hashes, source provenance,
matching control DLL and shipped CLI/RDS/self-tests verified. BUILD_NOTES records
evidence and remaining external directory/routing/physical UI test limits.

T-0096 | done (source) | Requalify FUBAR web workspace against current Town |
DEC-0165. Inventory native/control/DLL/web differences; expose shared map snapshots
and bounded workspace commands, preserve local privacy consent and P25/audio.
Update companion controls, permissions, lease isolation and map lifecycle; test
actual loopback DLL/API plus desktop/mobile browser paths and public CI packages.
No claim of full parity for features without an implemented safe public contract.

T-0094 | done | Restore tester upload capacity and trace missing Aero map positions |
DEC-0163. User requests 15 recordings per rolling day after the current four
upload quota blocked further evidence. Preserve authentication, consent, file
size/storage/retention bounds; update server, client notice and quota tests.
Diagnose new 10500 content before changing ADS-C acceptance or working voice.
New receipts now diagnosed; add direction/ADS-C/position counters and empty-map
status. Source comparison confirms InmarScope has a separate online map source;
do not silently add one or call these non-position clips a decoder failure.
v0.2.117-experimental published from 884c87d; master/release Windows Actions
36317237872/36317237782 and YAML checks PASS. Public ZIP/hash/provenance and
shipped GUI/CLI map reference, unchanged 8400 PCM, three submitted 10500 bundles,
RDS and recording negative tests PASS. Field position acceptance remains open
in ISS-0048; optional online enrichment is T-0095, not included in this release.

T-0095 | done (field association acceptance remains open) | Source-labelled Inmarsat received-aircraft online positions |
InmarScope comparison in INMARSAT_APPLICATION_VOICE_AUDIT_20260927.md. Explicit
opt-in, verified identity association, bounded async lookup/cancellation, source
and age labels, independent RF layer and disable/removal tests. Do not guess
AES-to-ICAO or upload receiver message contents. No change to working voice.
DEC-0164 corrects the previous identity restriction from primary Classic Aero
documentation. Implement received-identity mapping, voice identity publication,
bounded optional lookup, independent source expiry, conservative estimates,
map/activity diagnostics and negative lifecycle tests. P25 and vocoder frozen.
Implementation locally qualified: 72 Inmarsat unit cases, full 16/16 CTest,
real GUI/CLI IQ-to-ICAO/map, byte-identical 8400 PCM, privacy/schema tests and
rendered marker checks PASS. Retained RF position history survives identity
eviction. Released v0.2.118-experimental from 91b455a; master/release Windows
Actions 36401138823/36401138563 and both YAML checks PASS. Anonymous public ZIP,
checksum, source/version/run provenance and shipped executable verified. GUI/CLI
reference replay: eight validated units, four position reports, two ICAO map
tracks; 8400 PCM remains byte-identical to v116/v117. Stable updater v0.2.96
unchanged. Real-installation call-to-aircraft association and RF position
acceptance remain open (ISS-0050/0048); no attribution is invented for unknowns.

T-0093 | done (diagnosis; no application change) | First submitted 8400 speech |
Replay submitted bundle 086bf146 and correlate live output with remote counters.
Five seconds of live PCM, coherent local STT at beam 1/5, no clipping; four
zero intervals up to 100 ms are present before speaker playback. No new speaker
overflow/zero-fill, input gaps, codec failures or resets around capture. Fresh
decoder acquires late and produces the final three seconds; released GUI/CLI
fast/paced analytic-IF replay is byte-identical to the native direct-IF probe.
No quality percentage or universal RF acceptance inferred from this short clip.
Private audio/transcripts remain under ignored build/. See BUILD_NOTES and
INMARSAT_APPLICATION_VOICE_AUDIT_20260927.md. Documentation-only publication.

T-0092 | done | Inmarsat 10500 field evidence and aircraft receive controls |
Replay the three submitted IF bundles before changing DSP. Separate internet
and locally received aircraft; persistent network off must reject late replies.
Qualify 1090 capture-rate/bandwidth selection against device capabilities, not
the unrelated analog channel filter. Add negative tests and diagnostic evidence;
preserve P25 and publish a tested experimental release. DEC-0161.
Scope extended by user: interpret supported ACARS application families, retain
independent identity/position evidence, audit 8400 voice end-to-end. DEC-0162.
Released v0.2.116-experimental from 238d48f via Actions 36311464893; anonymous
ZIP/hash/provenance and shipped CLI/RDS/10500/8400 replay checks PASS. Local
CTest 16/16 PASS. CI selection now includes AircraftGui explicitly (12/12 local
lifecycle selection PASS). Multipart applications, matched 8400 field speech
and maximum-rate physical ADS-B acceptance remain open in ISS-0046/0047 and
INMARSAT_APPLICATION_VOICE_AUDIT_20260927.md; this task does not close those gates.

T-0091 | done | Automatic opted-in diagnostics and event dashboard |
DEC-0160. Startup PC/build details, settings/control/runtime evidence, bounded
priority transport, authenticated searchable event history and safe deployment.
No P25 DSP changes. Local 15/15 suites, collector 6/6, package tests and GUI
action/startup HTTPS receipt PASS. Source 31235b8, master 36303530136 and release
36303530038 Actions PASS; public v0.2.115 ZIP/checksum/provenance and shipped
CLI smoke PASS. Shipped CLI build metadata received by HTTPS collector. Remote
RSPdx physical reproduction remains separate; see ISS-0045 and BUILD_NOTES.

T-0087 | done (diagnosis; repair pending) | P25 missed-grant/no-voice audit |
DEC-0159; P25_REGRESSION_AUDIT_20260927.md. Fresh 60s GUI capture, identical
v113/v114 replay, stale-vs-current TG same-IQ test and local STT. Isolated
production-cpp harness proves activity/end-state defects. ISS-0042..0044 open.
No production P25 changes or new asset; documentation publication required.

T-0088 | planned | Selected-call lifecycle evidence repair |
ISS-0042. Deterministic regression tests, target-call evidence contract across
traffic processor/GUI/follow SM; preserve acquisition and encrypted/slot gates.

T-0089 | planned | In-passband control monitoring and allocation handoff |
ISS-0043; depends on T-0088. Bounded independent CC work, sample/epoch ownership,
validated allocation replacement and unchanged good-call output qualification.

T-0090 | planned | Retune-aware capture and explicit follow decision reasons |
ISS-0044. Existing SigMF/events, sample-indexed center/rate/epoch boundaries,
selected/companion/stale-TG reasons; no unbounded telemetry or raw upload.

T-0086 | done | Continuous SSTV autosave |
DEC-0158. Remember default image root, unique sessions, archive verified images
as completed, bounded long-running live helper with unchanged file safety limits.
Local gates PASS: 15 suites, repeated archive/cancel, GUI fixtures, paced NFM.
v0.2.114 from 68a6586 published; Windows CI 36297862769/36297862530 and YAML
PASS; public ZIP/provenance and shipped continuous helper verified. Separate
pre-existing Auto trailing-partial issue documented as ISS-0041.

T-0085 | done | Paced SSTV virtual-SDR reproduction |
DEC-0157. Feed user Scottie1 audio through Soapy readStream, DeviceManager ring,
live RF session and streaming helper. No protected runtime edits or RF hardware.
Complete image with full header; late Auto reproduces zero input; manual NFM
recovers 248 rows. Local 15/15 suites and Windows CI 36295831518 PASS. Test harness
published as b8bd4e0; no runtime change/release asset required. ISS-0040 stays open.

T-0084 | done | Inmarsat fail-closed audit and recovery instrumentation |
DEC-0156. Validate supplied audit against executable paths; reject malformed
blocks before mutation, preserve unknown PCM, qualify activity by identity,
report real codec availability and bounded failure counters; test all branches.
50 Inmarsat cases / 23168 assertions, full 15 suites, both Windows CI builds PASS.
v0.2.113 public asset provenance/hash and shipped GUI/CLI replay verified.
Real RF acceptance and scanner qualification remain ISS-0038/ISS-0039.

T-0083 | done | Bounded Inmarsat diagnostic recordings |
DEC-0155. Manual capture/review/send, authenticated collector quota/storage,
reproduction tool, privacy/error tests and public release qualification.
15/15 suites, both Windows CI builds and downloaded app checks PASS; synthetic
public HTTPS receipt verified with shipped configuration. Live RF: ISS-0039.

T-0082 | done | Inmarsat identity isolation and map usability |
DEC-0154. Preserve PCM AES in all paths, flush on changes and show unlocated or
unknown voice without guessing aircraft. Test native audio, GUI and privacy.
15/15 local suites and both Windows CI runs PASS. v0.2.111 public ZIP/source
provenance and shipped CLI/GUI/RDS verified. Remaining scanner work: ISS-0038.

T-0081 | done | WFM candidate RDS and signal-level qualification |
DEC-0153. Recorded MPX remodulation through candidate and actual RDS decoder;
stationary full/retained filter-power comparison before live integration.
3207 assertions PASS. Double-filter candidate data route regresses 2 groups to
1; direct retained-FIR discriminator recovers 3/2 groups at 2.4/10 MS/s. Do not
route RDS through speech cascade. Transient/configuration and runtime work open.

T-0080 | done | Efficient WFM FIR prototype |
DEC-0152. Qualify retained-output convolution, stream clock and actual PCM/cost
before integrating shared meter/RDS behavior.
Prototype 478 assertions PASS; repeated actual downstream PCM benchmark fixes
isolated 50 kHz-deviation blocker case with ~0.29 processing/input ratio at
10 MS/s. Runtime integration, encoded-RDS and power semantics remain open.

T-0079 | done | Isolate WFM filter leakage and candidate response |
DEC-0151. Independent causal oracle, pre-decimation leakage and clean-reference
error; quantify candidate delay/cost before any production adoption.
24 offline cases confirm pre-decimation blocker leakage. Longer candidate improves
the 50 kHz deviation case but full-rate cost is unacceptable; no runtime adoption.
See WFM_FILTER_ISOLATION.md. Rate-efficient implementation remains open.

T-0078 | done | WFM bandwidth/deviation/image sweep |
DEC-0150. Two 72-case sweeps reproduce closer-blocker corruption with identical
signal metrics. Seven parser tests and 15/15 CTest suites PASS. Runtime unchanged;
follow-up must separate filter skirt leakage from aliases and spectral overlap.

T-0077 | done | Verify supplied 0.2.110 follow-up audit |
DEC-0149. Source trace and fresh measurements correct AUTO, WFM rate-plan and
short CTCSS proposals. docs/AUDIT_0.2.110_FOLLOWUP.md records scope, gates and
revised sequence. 36 benchmark cases and 9104 focused assertions PASS. No DSP edit.

T-0076 | done | NFM first-stage alias rejection |
DEC-0148. Compare old moving average, cascaded response and decimating FIR.
Gate passband, image rejection, timing, tones and CPU before production adoption.
Source 1f7740e / experimental 0.2.110 published. Local 15/15 suites, response,
convolution, image sweeps and three benchmark runs PASS. Master/release CI PASS;
public download hashes/provenance and CLI/RDS/GUI smoke PASS. RF acceptance remains.

T-0075 | done | WFM FIR computation optimization |
DEC-0147 / ISS-0037. Preserve full-rate filtered IQ and coefficient accumulation
order while removing per-tap ring wrap and vectorizing independent output samples.
Require reference equality, whole-chain regression gates and repeat benchmarks.
Source5c205cb / experimental0.2.109 published. Local15/15 and25360 focused
assertions PASS; Windows/YAML master/release CI successful. Downloaded asset
hashes/provenance/CLI/RDS/GUI PASS. NFM rejection remains separate ISS-0037 work.

T-0074 | done | FM interference and throughput baseline |
DEC-0146. User confirms WFM sounds good. Preserve production DSP; build actual
two-signal IQ benchmark and structured report before selecting filter changes.
Source0dfe8cf pushed; three36-case runs, measurement/parser tests and full15/15
local suites PASS. Windows36247770217 and YAML36247770228 SUCCESS. Test-only
tooling; runtime release0.2.108 unchanged. Follow-up repairs remain ISS-0037.

T-0073 | done | WFM speech stream continuity |
DEC-0145 / ISS-0037. Repair reproduced WFM FIR/decimator/PCM boundaries,
preserve separate RDS branch and NFM/P25/HF. Promote characterization to gate,
add tiny blocks, output-rate/reset tests and verify release.
Sourceffd68c6 / experimental v0.2.108 published; local15/15, WFM80, NFM1626,
RDS23235 assertions PASS. Windows/YAML master and release CI SUCCESS; downloaded
hashes/provenance/CLI/RDS/GUI PASS. Physical listening remains acceptance work.

T-0072 | done | Global monitor Auto BW control |
DEC-0144. Persistent checkbox gates automatic bandwidth in all monitor modes,
including classifier/band-plan/mode/remote paths. Test policy and persistence;
preserve explicit widths and P25 protocol setup. Then continue WFM evidence.
Source2133b44 / experimental v0.2.107 published; local15/15, Qt18 assertions,
both Windows and YAML CI successful; public hashes/provenance/CLI/RDS/GUI PASS.
WFM evidence now recorded in ISS-0037; its DSP repair remains separate work.

T-0071 | done | NFM PCM sample-clock continuity |
DEC-0143 / ISS-0037. Preserve the user-confirmed clear 0.2.105 baseline;
characterize PCM partitions, then remove NFM-only block-sized interpolation
and unavailable lookahead. Do not alter bandwidth/classifier/WFM/HF/P25.
Source ff98664 / experimental v0.2.106 published. Local15/15 suites and PCM51
assertions PASS; both Windows CI and YAML runs successful. Downloaded public
hashes/provenance/CLI/RDS/survey/GUI smoke PASS. Physical acceptance remains open.

T-0070 | done | NFM stream-boundary repair and bounded FM diagnostics |
DEC-0142 / ISS-0037. Reproduce actual NFM tap count/waveform failures, repair
decimator phase and causal filter history without changing filter coefficients.
Instrument FM counts, resets, processing cost and resampler shortfalls. Preserve
WFM/HF/P25 processing. Source 80155ac / v0.2.105 published and downloaded;
master/release Windows CI and YAML PASS, public hashes/provenance/CLI/RDS/GUI
checks PASS. PCM/WFM follow-up and physical acceptance remain ISS-0037.

T-0069 | done | Evidence review of proposed DSP improvements |
DEC-0141. Audit the supplied write-up against current source and baseline tests;
record confirmed defects separately from unmeasured proposals. No receiver DSP
or P25 changes. DSP_AUDIT_20260926.md records findings, all 15 baseline suites
passing, calculated sample-count mismatch and the ordered repair gates.
Implementation remains future work under ISS-0037; this closes the audit only.

T-0068 | done | Hamlib rotator control and hardware SWR monitoring |
DEC-0140. Separate window/menu, bounded asynchronous protocol, saved limits,
explicit arm/stop, real position feedback and read-only SWR. Hardware acceptance
depends on tester controller/meter. Preserve frozen receive paths.
Published source 25236d2 / v0.2.104. Full local 15/15 plus five repeated
eight-case antenna suites PASS. Master/release Windows CI and YAML PASS;
downloaded public asset/provenance/hashes/runtime/defaults verified. Physical
acceptance remains ISS-0036; automatic tracking is not claimed.

T-0067 | blocked | Performance evidence and reliable diagnostics delivery |
DEC-0139. Instrument Inmarsat stages/workers with bounded numerical telemetry,
repair build configuration and consent precedence, verify server receipt and
release packaging. Profile high-rate cost before any DSP optimization. Preserve
P25; hardware acceptance and per-device ownership remain distinct gates.
Local build and 14/14 CTest plus collector/packaging tests PASS. Menu opt-in
persists; automatic config contains no admin authority. Collector restart
rejected by environment policy: collector deployment/public receipt pending.
Client/menu/performance changes shipped with v0.2.104 and verified CI assets.
ISS-0035 tracks this and per-install security/retention gaps. Not marked done.

T-0066 | done | Inmarsat decoder monitor and aircraft registry |
DEC-0138. Add bounded aircraft tracking, per-channel status/history and pop-out
views. Repair combined-watch status. Test identity, retention, clear/copy/filter,
inactive channels and rendering; qualify and publish a new CI-built release.
Source cb2ee37 / public v0.2.103. Local 13/13 plus 20 repeated GUI/lifecycle
runs PASS. Release CI 36231029153, master 36231028781 and both YAML runs PASS.
Downloaded ZIP/hash/provenance/executable/RDS/presets PASS. See BUILD_NOTES.

T-0065 | done | Selective fubarzi PR 33 integration |
DEC-0137. Adopt and strengthen map GUI and synthetic SDRplay/Inmarsat lifecycle
coverage without runtime changes. Run complete local/CI gates, then close PR
with attribution and exact disposition. Do not reuse contributor T-0064 ID.
Integrated 9237e77 with contributor credit. Local 13/13 plus ten repeated
GUI/lifecycle runs each PASS; Windows CI 36224393076 and YAML 36224393080 PASS.
PR #33 closed as selectively integrated; contributor branch preserved. Runtime
and public v0.2.102 remain unchanged, so no new binary version was manufactured.

T-0064 | done | Continuous in-band Aero and spectrum navigation |
DEC-0136. Combine data/voice when the whole watch fits RF and CPU budgets;
retain scheduled fallback. Add aligned mouse zoom/pan and regression tests.
Source 89a0067 / public v0.2.102. Local 13/13 PASS; release 36222945435 and
master 36222945379 PASS. Independent public asset verified. 10 MS/s realtime
remains ISS-0032; multiple radios/audio focus remain T-0062.

T-0062 | planned | Per-device multi-mode sessions |
User authorized shared ownership redesign. DEC-0135 / MULTI_SDR_SESSIONS.md.
Replace global leases and GUI takeover, add persistent role/device assignment,
then dual Inmarsat roles alongside P25. No claim this is implemented yet.

T-0063 | done | Aero multi-channel capacity and collection evidence |
DEC-0135. Raise tested worker budget, retain bandwidth grouping, require majority
CRC evidence plus distinct ADS-C positions for early voice transition.
Also retain map positions independently of rolling message history. Source
9ff87c6 / public v0.2.101, local 13/13 and both Windows CI runs PASS; downloaded
asset verified. Multi-SDR runtime remains separate T-0062, not delivered here.

T-0061 | done | Constellation selection and retune regression |
DEC-0134: reproduce saved-ID/manual-decoder mismatch, repair manual retune
selection, label actual channel, test every continuous Aero rate and isolation.
Source c313f06; local 13/13 PASS, CI release 36208705124/master 36208705250
PASS. Regular public v0.2.100 downloaded, verified and smoke-tested.

T-0060 | done | Inmarsat rate selector frequency presets |
DEC-0133. Apply satellite-specific surveyed centers on explicit decoder dropdown
activation; preserve restoration/manual tuning and report missing presets.
Source 9834492; local 13/13 suites PASS, release CI 36206766578 and master
36206766533 PASS. Public v0.2.99 downloaded and verified; see BUILD_NOTES.

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
