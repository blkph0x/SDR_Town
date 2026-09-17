# Task list (canonical)

T-0030 | pending_release | Release 0.2.58 | DEC-0093/0094 recorded SSTV GUI,
progressive previews, cancellation, independent pixel parity and transport
validation. Publish source/signed assets only after complete feature gates pass.
Local source/CLI/Qt/full+partial reference gates pass; packaging next.

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
Published 0.2.57 remains offline CLI only; next is bounded live/progressive input.
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

T-0023 | open | Public satellite catalogue/pass planner | Source-dated per-link
capability, hardware/coverage checks, orbital data, SGP4/Doppler and device-aware
scheduling. No automatic claim of all-satellite coverage. Depends on T-0021.

T-0024 | open | Weather satellite RX | Meteor LRPT first; NOAA APT archive
replay; expand validated public HRPT/AHRPT/LRIT/HRIT and higher-rate links per
hardware/coverage/mission. SatDump integration/license review before selection.
Depends on T-0021 and T-0023 for live scheduling; offline fixtures can precede it.

T-0025 | open | AX.25/APRS and public satellite telemetry | Packet validation
first, per-mission fields next; evaluate gr-satellites with pinned fixtures and
license review. Depends on T-0021; feeds satellite work without changing P25.

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
