# Build notes

Newest entry at the top. Record facts, not hopes.

## 2026-09-25 - Mandatory publication workflow preparation (DEC-0130)

Workflow YAML validator/self-tests PASS; P25 guard negative self-tests PASS.
Application source is the locally tested b2d4de8 RTL repair (13/13 suites),
with version bump to 0.2.97. CI will rebuild/test the exact new version.
Release branch checks version match and versioned notes, refuses asset
replacement, recognizes experimental tags as prereleases, and anonymously
downloads/verifies public hashes, run/source provenance and CLI execution.
CI execution/publication remains pending; no success claimed by this entry.

## 2026-09-25 - RTL bias-T local qualification (DEC-0129 / T-0056)

Windows/MSVC2022/Qt6.11.1, base 7d6d9ed. Release app, core, workspace and
Inmarsat GUI build-1/build-2 PASS; final lifecycle-test build-3 PASS.
CTest 13/13 PASS in 37.32 s. Focused adapter 32 assertions/3 cases, native GUI
20 assertions/1 case, manager lifecycle 51 assertions/1 case PASS. Lifecycle
covers saved intent, real-handle writes, stop OFF, restart ON, setup failure
after ON, RX exception after ON, unavailable driver capability and wrong saved
identity. Fake factory pointer is asserted before any stream opens.

Actual rebuilt executable: --cli --no-remote-diagnostics --cmd="devices probe"
--cmd="biastee 0 status" exits 0. Local generic RTL/R820T, serial 00000001:
probed=1 driverSupport=1 saved=OFF driverReported=OFF voltage=not-measured.
No ON issued to real hardware. Expected SDRplay enumeration reports missing
vendor service; no service or driver installation/change was attempted.
controls-final.png visually checked; P25 guard negative self-tests PASS.
No P25 algorithms, Receiver, Demod, AudioEngine, vocoder or RF sample/tune-loop
changes. The three shared-file patches are exact-digest reviewed under DEC-0129.
Evidence: build-audit-20260925/rtl-bias/. README/safety/CLI guide updated.
This is local development-build qualification, not electrical verification or
a new published release. Hardware acceptance remains ISS-0024.
Source commit b2d4de8; final repeated CTest 13/13 PASS in 36.51 s. Actual
committed P25 guard from 7d6d9ed to b2d4de8 PASS: only the three exact reviewed
shared-file digest pairs accepted; no other protected-path modifications.

## 2026-09-25 - 0.2.96 local release qualification

Source bb91aa6abd0b5a43b95a15bb43976e547695ef9b; signed asset metadata ade17a7.
Final versioned build-4.log PASS. Release helper rebuild/deploy/CTest PASS:
12/12 in 35.38 s, no exclusions. Installer/portable/control DLL, hashes, build
provenance and Ed25519 manifest verification PASS. P25 guard against ccaf6ae
PASS with exact DEC-0128 shared-file hashes; no P25/Receiver/Demod/AudioEngine
or vocoder differences. Focused final tests: 59 core assertions/4 cases and
50 GUI assertions/1 case PASS. Rendered controls-final.png inspected.

Extracted portable executable SHA256:
e71303265199c05e69d2422c6b431def571df29a959c28aac0b2132629259640.
Packaged CLI sdrplay status exits 0 and correctly reports registered module
0.3.0-206b241, absent service and no RSP. Expected vendor-open errors here are
not a successful hardware test. No services or drivers were installed/changed.
Packaged Aero GUI/CLI fast/paced replays PASS, byte-identical to 0.2.95:
4179528 samples, 163 valid units, 1375 voice frames, 220000 PCM samples;
WAV SHA256 295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.
This reference has many muted frames and is a parity check, not speech proof.
Evidence: build-audit-20260925/release-0.2.96.log, sdrplay-controls/,
portable-0.2.96/ and sdrplay-package-reference/.
Independent Windows CI 36102563157 PASS in 18m59s, including new control
lifecycle, core, workspace, loader, Inmarsat, SSTV and packaging gates. YAML
validation 36102563203 PASS. CI master does not publish releases by design;
verified signed local packages are published separately.
Published Latest v0.2.96 at 2026-09-25T06:42:50Z, release ID 396370935.
All eight downloaded assets match local size/SHA256; downloaded installer
passes signed manifest/package verification. Anonymous public Latest confirms
v0.2.96, not draft, eight assets. Diagnostics remain opt-in with the unchanged
HTTPS collector. Evidence: published-0.2.96/ and sdrplay-controls/windows-ci.log.

## 2026-09-25 - RSPdx complete control and lifecycle regression (DEC-0128)

Windows/MSVC2022/Qt6.11.1, base ccaf6ae. build-2.log and build-3.log PASS for
app/core/workspace/Inmarsat GUI targets. Full CTest runs PASS 12/12 in 36.84 s
and 35.16 s respectively. Added focused numeric-edit rejection rollback and
PPM/sample-rate/AGC restart coverage to the initial contract/widget cases.
P25 guard negative self-tests PASS; release-verifier negative tests 16/16 PASS.
No build/test failure in these three iterations. Final versioned build and
public packaging/CI remain the next gate. No physical RSP is present locally.

## 2026-09-25 - SDRplay controls initial regression checks (DEC-0128)

Base ccaf6ae, Windows/MSVC2022/Qt6.11.1. Initial app/core/workspace build PASS.
Core driver-contract tests 59 assertions/4 cases PASS; native widget clicks
49 assertions/1 case PASS; actual DeviceManager light discovery/open/control/
restart/persistence against registered Soapy fixture PASS (1.08 s). Fixture
returns no RF samples and deliberately resets antenna at activation; no real
RSP or electrical test claimed. Screenshot controls.png inspected: antenna and
capability-gated controls fit. CLI/API error propagation and final regression
qualification are still in progress. Evidence build-audit-20260925/sdrplay-controls/
build-1.log, core-1.log, gui-1.log, lifecycle-1.log. No compile/assertion failure.

## 2026-09-25 - 0.2.95 Aero watch release qualification and publication

Source e46f43719180e17cbf6547e88b11338af17d93ba; signed metadata/tag d7979ee.
Release helper completes with full CTest 11/11 PASS in 34.05 s and signed
manifest/runtime/standalone DLL verification PASS. Only update.json,
update.json.sig and SHA256SUMS.txt differ from the independently tested source.
Original P25 freeze guard and additive Satcom host integration checks PASS.

Extracted portable EXE SHA256:
485f4966abeba964b61cd96c72551a8630113f443ee04a487c713d5f183ade7d.
Packaged real GUI/RTL hot-watch test PASS: four workers, invalid-empty rejection,
edit to two, return to manual, settings restore and clean exit. Four packaged
reference GUI/CLI fast/paced replays all retain 4179528 IQ samples, 163 valid
units, 1375 voice frames, 220000 PCM samples, 11 CRC failures, 106 corrections,
8 repeats, 1244 codec mutes and 3 rejected C-frames. Every WAV SHA256 remains
295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.
These mostly-silence reference frames are not field speech acceptance.

Windows CI 36095455129 PASS in 16m52s: clean MSVC/Qt6.7.3 build, release
verifier negatives, frozen-P25 gate, core, loader/lifecycle, Qt workspace,
GUI/CLI replay, SSTV, staging/ZIP and uploaded artifact. YAML 36095455147 PASS.
Master intentionally skips CI's release-branch-only publishing step; tested
signed local assets are published separately with no production source delta.

Published Latest v0.2.95 (experimental), release ID 396316236, at
2026-09-25T05:02:50Z. All eight downloaded assets match local sizes and SHA256.
Downloaded installer passes the signed manifest/package verifier. Anonymous
public Latest returns v0.2.95, not draft, exactly eight assets. Diagnostics
remain opt-in at https://gearsqueens.online/sdr-town-diag/ingest; constellation
point arrays remain local. No physical RSP or clear Aero conversation claim.
Evidence: build-audit-20260925/release-0.2.95.log, ci-0.2.95-watch.log,
portable-0.2.95/, watch-package-hot/, watch-package-reference/, published-0.2.95/.

## 2026-09-25 - Aero display and parallel-worker qualification (DEC-0127)

Windows 11/MSVC2022/Qt6.11.1, base 58530ae. Initial app/core/native GUI builds
PASS. Watch tests: 1002 assertions/11 cases PASS, including per-block serial vs
parallel report equality, NaN error drain/retry and source identity. Hidden real
reference test: 131 assertions PASS for two separate workers vs two serial
pipelines across the entire JAERO-derived recording. No codec/math change.
Four reference CLI/GUI fast/paced runs PASS with the unchanged WAV hash
295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.

Same fixed Release benchmark, 2.048 s of 2.048 MS/s input in 64 blocks:
before 1/2/4 serial decoders = 0.613/1.230/2.463 s (load .299/.601/1.203).
After persistent parallel workers = .620/.629/.639 s (.303/.307/.312).
Max block with four channels 28.80 ms, before 104.13 ms. Local measurement,
not universal CPU headroom or satellite speech proof; default remains two.

Native Qt display tests PASS: 66 assertions/7 cases; host lifecycle 64/2 PASS.
Two hundred 4096-bin updates/renders at 1000x280 take 171 ms. Duplicate frames
do not advance history; wrap/retune/empty clearing and multi-click dedup pass.
1050x980 saved-window screenshot inspected: no overlaps; new plot/selector
remain beside the spectrum. This layout fixture is not a live RF-lock screenshot.
Final 0.2.95 app/core/workspace/native-GUI build PASS (watch-visual-build-4.log).
Full CTest 11/11 PASS in 34.70 s, no exclusions. Real GUI/RTL live hot-watch test
PASS: authenticated P25 handover, four live workers with one pending block each,
empty-list rejection without losing the active group, edit to two workers,
return to manual with increasing IQ, Stop/no P25 restart and saved-watch restore.
First measured live four-worker block load=.878 (includes startup; not a steady
throughput number). Test-owned application exits normally. Existing guards and
whitespace check PASS. Independent CI and public release gates still pending.
Evidence: build-audit-20260925/watch-visual-build-1/2/3.log,
watch-visual-gui-tests.log, watch-visual-layout.png, watch-parallel-reference/,
watch-visual-ctest.log, watch-live-hot/result.json and gui.log.

## 2026-09-25 - 0.2.94 handover release qualification and publication

Source 295253558e4201a86bf2d189e6231a86cda8cf80; signed metadata/tag 5fc5fc4.
Release helper completes with full CTest 11/11 PASS in 35.80 s and signed
manifest/runtime/standalone DLL verifier PASS. Frozen-P25 comparison against
9ea475b, guard self-test and host integration contract pass without guard edits.
Only three release metadata files differ between CI source and release tag.

Extracted portable EXE SHA256:
70c5e82cdc70125ad13f664c00719c7da101110ce8f5836afdff66d3150a2db8.
The packaged real GUI/RTL handover test PASS, including armed auto-follow,
unconfirmed refusal, forced confirmation, growing IQ, no P25 restart and clean
process exit. Four packaged reference replays (GUI/CLI fast/paced) retain the
0.2.93 metrics: 163 valid units, 1375 voice words, 220000 samples, 11 CRC
failures, 106 corrections, 8 repeats, 1244 codec mutes, 3 rejected C-frames.
Every WAV SHA256 remains
295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.
Reference silence markers are not clear-speech acceptance.

Windows CI 36079172152 PASS in 15m35s: MSVC/Qt6.7.3 clean build, core, loader/
lifecycle, Qt workspace, GUI/CLI replay, SSTV, clean staging/ZIP and uploaded
artifact. YAML validation 36079172173 PASS. Master intentionally skips CI's
release-branch-only publishing step; tested signed local assets are published
separately. No production code differs from the independently tested source.

Published Latest v0.2.94 (experimental), release ID 396196715, at
2026-09-25T01:05:05Z. All eight downloaded assets match local SHA256 and sizes.
Downloaded installer passes the signed-manifest/package verifier. Anonymous
public Latest returns v0.2.94, not draft, exactly eight assets. Diagnostics
remain explicit opt-in at https://gearsqueens.online/sdr-town-diag/ingest.
Evidence: build-audit-20260925/release-0.2.94.log, ci-0.2.94-watch.log,
handover-packaged/, handover-reference/, portable-0.2.94/, published-0.2.94/.
No matched live Aero speech, second physical SDR or RSP test is claimed.

## 2026-09-25 - Inmarsat P25 handover regression work (DEC-0126)

Windows 11/MSVC2022/Qt6.11.1, base 9ea475b. Initial app build passed; core test
compile failed C4430 in tests/test_satcom.cpp because the new TEST_CASE preceded
the Catch header. Corrected include ordering; no assertion or production failure
was hidden. Rebuilding all affected targets as 0.2.94. The unmodified P25 guard
self-test, additive host-diff gate and Satcom integration contracts pass.
Evidence: build-audit-20260925/handover-build.log, handover-build-2.log.
Final app/core/workspace/GUI builds PASS. First dialog harness run failed because
QMessageBox::done(Yes) did not click the standard button; corrected the harness
to click the actual Yes/No button. Native dialog/lifecycle groups pass, including
default No, cancellation, busy recheck, failed hardware start and empty watch.
Final full CTest 11/11 PASS in 36.84 s, no exclusions (handover-ctest-release.log).

Real compiled GUI/API with attached RTL R820T: three passing runs cover live
P25 CC 420.350 MHz, configured-only P25 and auto-follow armed. Probe and
unconfirmed Start preserve P25; stopP25 without force is rejected. Confirmed
Start reaches Inmarsat live hardware at 1542.935 MHz with fresh IQ samples.
Live run samples grow 2097152 -> 3080192; configured-only 2031616 -> 3145728.
P25 CC/follow/traffic/standby are cleared and remain off after Inmarsat Stop.
All test-owned GUI processes exit normally. Automated script:
scripts/test_inmarsat_handover_gui.py --allow-hardware --output <evidence-dir>;
--dry-p25 covers configured-only. It uses an ephemeral authenticated loopback
API, disables remote diagnostics and requires explicit RF permission.

An earlier live test incorrectly asserted rawBlocks > 0. That is decoded
protocol output, not IQ; local JSONL proved 2097152 incoming samples and no
satellite lock. Corrected the test to require increasing diagnostics.samples.
No receiver code was changed to satisfy that assertion. Evidence:
build-audit-20260925/handover-real-live[-2], handover-real-dry, handover-real-auto.
Desktop menu automation hit a geometry timeout; actual dialog behavior is covered
by native Qt tests and real host/hardware by the API, not a claimed full visual
click-through. No satellite speech or physical RSP acceptance is inferred.
CI, packaging and publication gates remain pending below.

## 2026-09-25 - 0.2.93 final reference, CI and published asset gates

Source ca6354e72fe5912305eb96cb3e0624c5944fb104; signed release metadata/tag
9fef480. Final local MSVC/Qt6.11.1 release helper completed with all CTest
11/11 PASS in 33.12 s, without exclusions. Final watch suite: 10 cases,
929 assertions PASS. Frozen-P25 comparison 825dae2..ca6354e and guard self-tests
PASS; only the exact reviewed SDRplay DeviceManager loader delta was accepted.

`scripts/verify_aero_reference.py` completed all four real-reference GUI/CLI
fast/paced runs: 163 validated units, 1375 voice words, 220000 PCM samples,
11 CRC failures, 106 codec corrections, 8 repeats, 1244 codec mutes and 3 rejected
C-frames. All WAV SHA256 values remain
295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.
Mostly silence-marker content is not clear-conversation acceptance.

Extracted final portable EXE SHA256:
45ea99e82af1f2c02df47116a5ce032d3bf508ee3ce6baee91d381f55b024133.
The packaged voice replay preserves the same WAV. Packaged 10500-burst replay
completes with 8 valid units, 1 CRC failure, no PCM and the two expected ADS-C
positions: D-AIHV (51.002140045, -30.045547485) and G-CIVG
(62.997665405, -40.003108978). Both reports have empty error/logError fields.
Evidence: build-audit-20260925/watch-reference/, package-voice.json/.wav,
package-data.json, release-0.2.93.log and portable-0.2.93/.

Independent Windows CI 36073284286 on source ca6354e PASS in 18m0s:
MSVC/Qt6.7.3 build, core, loader/lifecycle, workspace, GUI/CLI replay, SSTV,
portable staging/ZIP and artifact upload all pass. YAML validation 36073284259
also passes. The release-branch-only publication step is intentionally skipped
on master; the separately verified local release helper builds the signed
installer/portable/DLL assets. No source code differs between this CI commit
and tag 9fef480 (only the three signed-release metadata files).

Published v0.2.93, release ID 396165844, at 2026-09-24T23:52:38Z. Downloaded all
eight GitHub assets to build-audit-20260925/published-0.2.93/: every size and SHA256
matches the verified local release, including manifest/signature/checksums.
Release verifier re-run with the downloaded installer PASS (Ed25519 signature,
runtime contents/provenance and standalone control DLL). Unauthenticated public
Latest returns v0.2.93, not a draft, with exactly eight assets. Packaged remote
URL remains https://gearsqueens.online/sdr-town-diag/ingest, explicit opt-in.
No real RSP hardware or matched tester clear-speech capture was available.

## 2026-09-25 - Aero watch / 0.2.93 local gates (DEC-0124 / DEC-0125)

Host Windows 11, MSVC 2022, Qt6.11.1, original Desktop/maulaudio_pro tree.
Full app/core/workspace/Inmarsat GUI/deploy build passed. First full CTest after
SSTV repair: 11/11, 33.31 s. Scheduler/group/focus/privacy plus hidden performance
probe: 10 cases, 919 assertions pass. Actual widget saved-list add/remove/enable,
disk reload and no-auto-start checks pass; screenshot inspected at 1050x980.
Native spectrum and waterfall click coordinates share the RF axis; empty input
does not generate demonstration RF.

Unchanged SSTV hot-producer test before fix: 13.797 s (previously >246 s).
After control admission fix, 10 separate runs: 0.063, 0.078, 0.078, 0.063, 0.078,
0.078, 0.062, 0.078, 0.079, 0.062 s, all PASS. No test sleeps or exclusions.

Release watch throughput probe at 2.048 MS/s, 64 x 65536 sample blocks:
one decoder 0.600 s, two 1.210 s, four 2.410 s for 2.048 s RF. Ratios 0.293,
0.591, 1.177 respectively. Default changed to TWO; user limit 1-4 and UI/report
load telemetry retained. This noise-input measurement is local headroom, not
proof of performance on every PC or proof of decoded speech.

InmarScope pinned 26ae80af4bcfa4c86ed55f4383d1c95481d3450b: both 96-entry
Aero interleave vectors exactly match. Reference voice/data/filter/follow
differences documented in INMARSAT_WATCH.md. Release verifier 16 negative tests
and frozen-P25 guard self-test pass. Normal Git whitespace check passes.
Final reference pacing variants, updated full suite and publication recorded below.

## 2026-09-25 - Inmarsat tone routing/selection (DEC-0123)

Windows/MSVC2022/Qt6.11.1. Initial Release build of SDR_Town, core, workspace
and isolated live GUI tests PASS. First GUI CTest timed out at 60 s before
QApplication construction completed: non-invasive CDB stack shows Qt platform
integration error MessageBoxW. Test requested offscreen but package contains
only platforms/qwindows.dll. Corrected test environment to the actual deployed
Windows plugin. No production decoder assertion or live signal was involved.

The isolated settings guard initially exited 3: Windows generic data is Local,
while AppDataLocation is Roaming. The final harness verifies its unique test
directory beneath SDR_Town_Tests and removes only that directory. CTest uses
the imported Qt Windows plugin path so clean CI does not require prior deploy.

Final Release app/core/workspace/live-GUI/deploy builds PASS. Focused native/
replay suite: 20 cases, 858 assertions. Real live-widget controls: 3 cases,
33 assertions (34 with saved screenshot). Dedicated stub-only hardware-backend
test: 20 assertions proving host refusal, parking before startup, hardware
failure restore, tuner-lease release and idempotent Stop. This is not RF proof.
Final CTest excluding UnitTests: 10/10 PASS in 11.36 s. Core separately run with
120 s process deadline and only known ISS-0019 SSTV detach case excluded:
388 PASS / 2 optional skips, 206317 assertions. That SSTV issue remains open;
no test was removed from CI and no complete all-tests pass is claimed.

Four actual reference runs (GUI/CLI, fast/paced) PASS: 163 valid signalling
units, 1375 voice words, 220000 PCM samples, all WAV hashes unchanged at
295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.
The recording is mostly codec silence markers, not clear-conversation proof.
Final staged EXE, with PATH restricted to Windows and Qt overrides removed,
also produces the same WAV. Its metrics correctly report 220000 received,
19403 nonzero samples, peak 3562, RMS 307.634, speakerRequested=false and
speakerConsumed=0. This differentiates decoder output from actual playback.
Staged and main Release EXE SHA256:
3645d0bb28976e7317a25ab0d09789453cbc3f21c46c713e290808b67eb5744e.

P25 guard self-test and protected-source comparison PASS; DeviceManager is
exactly the preceding DEC-0122 loader repair. Other protected files and native
Aero codec/DSP remain unchanged. git diff --check PASS. Evidence is under
build-audit-20260925: inmarsat-*-build.log, inmarsat-core.log,
inmarsat-core-remainder.log, inmarsat-gui-host-final.log, inmarsat-other-final.log,
inmarsat-live.png, inmarsat-staged-smoke.log, inmarsat-tone-parity/.
No GitHub publication or matched live-tone/speech qualification in this pass.

## 2026-09-25 - SDRplay runtime repair (DEC-0122)

Windows/MSVC2022/Qt6.11.1, base 825dae2, local unpublished 0.2.92 repair.
Four Release builds PASS; final targets SDR_Town, sdr_town_tests,
sdr_town_workspace_tests and deploy. No compiler errors/warnings in final build log.
Full CTest 9/9 PASS in 42.14 s: core 388 passed / 2 optional skips,
206299 assertions; workspace 34 passed / 6 optional skips, 411 assertions;
five isolated SDRplay loader cases and both SSTV backend gates pass.
Legacy ABI fixture proves loadModule returns empty even though the factory
was rejected. New loader refuses it and recovers with a valid module. Tests
also cover empty registration, non-API DLL, eight concurrent calls, Unicode
API loading and explicit/conda/user/nested portable paths. Final loader test
adds an invalid PE image: failure is reported without a modal Windows popup.

Actual application CLI `sdrplay status` exits 0, registers installed Pothos
sdrPlaySupport.dll 0.3.0-206b241 and identifies service=not installed. stderr
still records sdrplay_api_Open failure, consistent with no service on this
PC; no physical RSP attached. Existing RTL device still enumerates. This is
loader evidence only, not reception or the affected tester's exact diagnosis.
Package negatives 16/16 PASS; P25 guard self-tests and whole-file comparison
against 825dae2 PASS; an added RF setFrequency mutation is rejected.
Other protected files are unchanged. git diff --check PASS.

Evidence: build-audit-20260925/sdrplay-build-{1,2,3}.log,
sdrplay-ctest-all.log and sdrplay-status-after.log;
build/Testing/Temporary/LastTest.log contains individual assertions/skips.
No service installed/restarted, no vendor DLL redistribution, no P25/RF/audio
algorithm edits, no GitHub release publication in this pass.

Final clean staging smoke PASS with PATH reduced to Windows/System32 and Qt
developer overrides removed. Actual app registers installed SDRplay module,
reports missing service and still enumerates RTL. Staged EXE equals main Release
and no test/vendor fixtures occur in staging. EXE SHA256:
c0f2dcf7220dbc103a899745c4f8c7ccdd5fbf8a39826bf91d7f483610405f79.
Evidence: sdrplay-build-final.log and sdrplay-staging-final.log in the audit folder.

Final regression repeat is NOT a full pass: UnitTests stalled for 246.34 s
and was explicitly stopped. Remaining eight CTest targets pass, including all
five final loader cases (invalid PE test included). A duration/seed-123 rerun
and a standalone 20 s deadline isolate the existing SSTV active-producer detach
test; ISS-0019 records debugger evidence and unchanged source comparison.
With only that named case excluded, remaining core 387 passed / 2 optional skips,
206298 assertions PASS. This does not erase the initial full pass or the later
stall. No full-release qualification claimed. All test/debug processes exited.

## 2026-09-24 - 0.2.92 release and extracted-package gates (DEC-0121)

Release source 7e6eed7e532161ce6a0918d6ed6b616dbe320647. MSVC2022/Qt6.11.1
release helper with SkipPush/SkipAssets PASS; signed metadata commit 08c59d0.
Final CTest 4/4 PASS in 115.99 s: core 388 passed / 2 optional skips,
206295 assertions; workspace 34 passed / 6 optional skips, 411 assertions;
Rust 8/8 and SSTV backend self-test PASS. The optional Aero recording was
separately supplied to the focused GUI test recorded below; it was not silently
counted as covered by the default suite. Packaging negative tests 15/15 PASS.

Extracted portable tested with developer Qt paths removed and Windows-only PATH.
Actual GUI/CLI fast/paced public voice replay: four equal WAV hashes, matching
the source-build reference 295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.
Default speaker: zero drops/errors, queue drained; 443440 zero-fill samples
across idle/missing intervals. Public burst IQ: both packaged GUI/CLI produce
8 CRC-valid frames and the same two ADS-C aircraft positions. Synthetic transport
four-way parity and malformed-input rejection PASS. These are not a claim of
clear conversation or an antenna test.

Installer: 26566683 bytes, SHA256
f105662048555a84d02d4995eb5bd13052ced0215cefbb29c42c5df4e0e32c72.
Portable: 35579217 bytes, SHA256
1e2492f3c169878c3f4dadb1db35b60d66a54914df637ae63d319c593d3462bb.
Required Aero codec DLL and all seven notices present; only four sdr_aero_*
symbols exported, no P25 mbe_* exports. Packaged diagnostics HTTPS/default-off,
64 KiB/minute, ingest token only. Explicit P25 guard from 485268b to HEAD:
128 changed paths, zero protected. git diff --check PASS.
Evidence: build-audit-20260924/release-0292.txt, aero-portable-parity.log,
aero-portable-burst.log, inmarsat-portable-0292.log and corresponding directories.

Independent Windows CI 36001921711 PASS (Qt6.7.3, RDS DSP off): core 385 passed /
2 optional skips, 183234 assertions; workspace 34 passed / 6 optional skips,
411 assertions; actual GUI/CLI synthetic parity/failure tests, Rust backend,
clean portable staging and artifact upload pass. Public reference IQ was tested
locally, not supplied to CI. CI guard baseline limitation ISS-0015 remains;
the explicit local baseline comparison above is the P25-preservation evidence.
Full remote log retained in build-audit-20260924/aero-ci-full.log.

Published https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.92, release
395703721, tag at 08c59d0. All eight uploaded SHA256 digests and sizes match;
public unauthenticated /releases/latest returns v0.2.92 (draft=false), and the
downloaded update.json matches the signed local bytes. Documentation-only
follow-ups do not change the packaged binary's recorded source commit.

## 2026-09-24 - Native Aero implementation and reference gates (DEC-0121)

Windows/MSVC2022/Qt6.11.1. Initial vendor compile failed on Qt6 QString::sprintf,
QByteArray/QString mixing and missing FFT wrapper; mechanical compatibility
fixes applied. Native target configuration initially preceded nlohmann find;
moved after dependency discovery. Full app/core/workspace then compiled.
New map findChild test exposed missing Q_OBJECT; added explicit AUTOMOC header.
Final focused core: 19 cases / 839 assertions PASS. GUI: 3 cases / 28 assertions
PASS including independent burst IQ mapped to two aircraft; screenshots inspected.
One offscreen GUI attempt stopped at Qt's missing-platform dialog (only windows
plugin deployed); stopped that own process and repeated with Windows plugin PASS.

Public JAERO 8400bps_ambe_sample.ogg at pinned commit: valid 87.0735 s prefix,
SHA256 0ce7bd72c89e5d5dae9128093b9d4c960a7bc53e6b5b600ef87760c93a76df24.
Upstream Ogg tail is invalid; preparation records that error and excludes it.
Native IQ: 163 valid SUs, 11 failed CRCs, 3 rejected C frames, 1375 voice words,
220000 PCM samples, 106 codec corrections, 8 repeats, 1244 silence/mute/tone flags.
GUI/CLI fast/paced four-pass equality PASS including WAV SHA256
295ee11a0edc4e341ab66455ce283f7a0201e2f35a880eb555e47a19af6e176f.
Paced GUI default speaker: zero drops, no device error, 443360 zero-fill samples
across idle/missing intervals. STT of earlier direct-IF output: only repeated
"No"; NOT clear continuous speech acceptance. Dad's matching reference remains open.

Public 10.5k_burst_sample.mp3: 33.623 s, direct IF 9 CRC-valid packets/9 messages/
4 position reports; filtered IQ 8 packets and two unique aircraft on GUI map:
D-AIHV / DLH424, 51.002140/-30.045547, 36000 ft; G-CIVG,
62.997665/-40.003109, 34004 ft. These are historical reference reports, not live.
Public 1200bps_burst_sample1.wav: measured IF spectral centroid 1816.55354 Hz,
translated explicitly to synthetic-centered IQ; two CRC-valid packets, no PCM.
Public 10.5k_sample.ogg valid 240.775 s prefix: 12350 valid SUs, 117 messages;
decoder argument error at damaged tail recorded, not hidden.

Earlier full CTest 4/4 PASS in 192.53 s (before final burst/map test additions).
Package-negative tests 15/15 PASS with required Aero DLL/license checks added.
Independent examples, noise/zero input, separate codec state, codec concurrency,
channelizer chunk parity and exact WAV tests are separate from RF acceptance.
Evidence: build-audit-20260924/aero-*.log, aero-reference, aero-parity-1,
aero-burst-reference, aero-msk-reference and aero-data-reference.

## 2026-09-24 - 0.2.91 package, CI and publication (DEC-0120)

MSVC2022/Qt6.11.1 Release: release.ps1 -Version 0.2.91 -Channel experimental
with the public HTTPS diagnostics URL, -SkipPush -SkipAssets PASS. Negative
release tests 15/15 PASS. Final local CTest 4/4 in 42.59 s: core 382 cases,
205726 assertions (2 optional skips); workspace 33 cases, 406 assertions
(5 optional skips); Rust 8/8 and SSTV Robot36 helper self-test PASS.

Extracted portable ZIP build-info identifies source 698dcd832badfdf737bd05ca5e932cd3a1530a11.
With developer Qt paths removed, the packaged GUI/CLI both consumed all 48000
synthetic samples in paced and fast modes; malformed raw input exited 2.
No actual Aero voice was decoded or claimed. Package configuration inspection:
HTTPS endpoint https://gearsqueens.online/sdr-town-diag/ingest, enabled=false,
65536-byte/minute budget, ingest token matching the tested client configuration.
No admin token shipped. A fresh-profile runtime default-off check was NOT proven:
APPDATA environment changes do not isolate Qt KnownFolder storage on Windows;
the separate launch attempt was blocked. Per-session consent unit tests passed.

Independent Windows CI 35993275475 (Qt6.7.3, RDS DSP off) PASS: core 379 cases,
182665 assertions; workspace 33 cases, 406 assertions; new actual GUI/CLI replay
parity test, Rust/SSTV and artifact packaging all pass. Optional fixture skips
remain. Existing CI diff-baseline gap ISS-0015 remains open; manual diff review,
not that guard, establishes no P25/analog DSP edits.

Source pushed, tag v0.2.91 points to metadata commit dac8778. Release 395613170
published after all eight uploaded asset SHA256 digests/sizes matched local.
GitHub /releases/latest returns v0.2.91, draft=false. Experimental manifest,
verified Ed25519 signature. Installer 26399006 bytes, SHA256
a5c388f9152245757b4c4d65031b2848898d31e953c02fa6e447dcd847cf188d;
portable 36456382 bytes, SHA256
fabb2925ba2d89b0fcc8c6310b7daaaa255544fd34fd5b543df25368168aad2d.
Evidence: build-audit-20260924/release-0291.txt, ci-0291.log,
package-iq-0291 and inmarsat-0291. Four actual replay session remote summaries
were matched to server JSONL; off-LAN reachability remains unqualified.

## 2026-09-24 - Inmarsat replay first verification (DEC-0120)

Follow-up: focused tests 13/13, 270 assertions PASS. v0.2.91 actual GUI/CLI,
paced/fast black-box PASS, four collector summaries verified by matching replay
session UUIDs with server JSONL (processedSampleCount=48000, pcmSamples=0 in all).
No redundant startup events or lost counters. Release negative tests 15/15 PASS.
Manual diff audit: P25/analog DSP files unchanged; main/CliApp add only separate
Inmarsat replay entry, AppBootstrap changes help text. CI guard gap ISS-0015 remains.

MSVC2022/Qt6.11.1 Release app/core/workspace build PASS. Focused core 12 cases,
264 assertions; GUI 1 case/17 assertions (including screenshot). Full CTest 4/4
PASS in 76.66 s. Black-box actual SDR_Town.exe CLI/GUI paced/fast each consume
48000 synthetic samples identically; malformed raw file exits 2 and writes error
report. This is NOT RF/voice acceptance. Evidence: build-audit-20260924/inmarsat-blackbox.
Offscreen screenshot lacked fonts; repeated with real Windows Qt plugin and
inspected readable 820x650 screenshot. A subsequent remote end-to-end check found
the shared sanitizer removes the key `samples` and the startup event can consume
the 1500ms shutdown queue drain. Rename the remote counter and omit redundant
remote open events; re-test actual collector receipt before publication.

Proxy: original SHA256 5c773fe9c3ad89c95270794cba1ad798f550e849b3644d2d7fe49122db4a3fc0.
Only two added vhost lines (Include + blank); separate exact-route config. Apache
configtest PASS, graceful reload, HTTPS synthetic POST accepted and saved. /,
/uow-map/, /ereader/ remain 200; /fubar/, /fubar-net/, /psk/ timed out both BEFORE
and AFTER, unchanged. Public admin path 404; unauthenticated client-status 401.
Backup retained on VM. Public DNS matches WAN; external web fetch unavailable,
so an off-LAN tester still needs to confirm reachability from their network.

## 2026-09-24 - 0.2.90 packaged SSTV verification

Windows/MSVC 2022 x64 / Qt 6.11.1. Committed source
401e2e386b29b8d68dd3f0d557ac99a02cf45c5a; signed asset metadata b8a27dd.
release.ps1 -Version 0.2.90 -Channel experimental -SkipPush -SkipAssets: PASS.
Negative packaging tests 15/15; rebuilt app/core/workspace; CTest 4/4 in 76.93 s:
373 core pass / 2 optional fixture skips (205479 assertions); 32 workspace pass /
5 optional fixture/import skips (390 assertions); Rust 8/8; helper selftest PASS.
NSIS installer, clean portable ZIP and control DLL verified with signed manifest,
runtime/license payload, source provenance and hashes. Log: release-0290.txt.

Extracted the actual ZIP to build-audit-20260924/package-smoke-0290. With developer
Qt/plugin paths removed and PATH restricted to Windows directories, --version
reports 0.2.90; bundled helper completes Robot36 selftest; GUI dry-run exits 0,
report ok=true, no errors/warnings and no RF streaming. Inspected GUI screenshot.
Packaged token-authenticated loopback API lists all five RF choices, rejects WFM
with HTTP 400, normalizes manual lsb to LSB, then correctly rejects reception
without an active non-P25 receiver. Own test process exits cleanly. Evidence:
package-gui-0290.json/png, package-api-0290.json; no live radio test claimed.

Repeated recorded RF test after rebuild: full Robot36 240 rows on USB/LSB/NFM/AM,
same RGB errors as preflight. All eight assets uploaded to a draft v0.2.90 release;
GitHub sizes and SHA-256 digests match every local file. Source/tag pushed.
Windows CI run 35986929161 subsequently PASSED in 14m9s: native app/tests,
370 core cases (182418 assertions, 2 optional skips; CI RDS DSP is disabled),
32 Qt cases (390 assertions, 5 optional skips), Rust tests, packaging and upload.
Artifact 10802883190 is 15442643 bytes. Workflow validation 35986928995 also PASS.
The pre-existing P25 guard compared origin/master to HEAD with zero changed paths
on this push (ISS-0015); this is not evidence of DSP non-regression. Local diff
inspection against 2705a9e confirms no P25/receiver/device/demod/audio-engine core
changes; shared MainWindow changes are limited to the SSTV includes, status,
live-session wiring and request handling.

Published at 2026-09-24T10:38:14Z. GitHub /releases/latest returns public v0.2.90,
all eight asset digests/sizes match local files, and the public update.json
download reports 0.2.90 experimental with the exact uploaded installer hash/size.
https://github.com/blkph0x/SDR_Town/actions/runs/35986929161

## 2026-09-24 - DEC-0119 SSTV 0.2.90 release preflight

Repeated scripts/test_sstv_rf.py in the isolated SSTV venv: all four complete
Robot36 images PASS, 240 rows; RGB errors USB 4.142, LSB 4.144, NFM 2.622,
AM 1.614 out of 255, unchanged from DEC-0117. Noise-tail partials remain visible.
Repeated scripts/test_sstv_worker.py: all 16 recording/worker/GUI combinations
PASS (build-audit-20260924/sstv-worker-0290.txt). No new DSP edits since those
tests. Version/docs are prepared for 0.2.90; rebuild, package and publication
verification follow from the committed source. Git diff whitespace check PASS.

## 2026-09-24 - DEC-0118 Windows CI fixture repair

Failed run 35979813492 / source 3aa5ce4 reproduced locally: release-verifier suite
11 tests, 2 failures + 2 errors; first error Missing runtime: build-info.json.
The app/native/Qt/Rust steps on that runner had already passed. Failure prevented
portable staging/upload, not compilation or RF tests.

Isolated repair worktree, Windows host, Python 3.12 (CI version):
`py -3.12 scripts/test_verify_release.py -v`: 15/15 PASS (1.03 s), including
2 missing-notice subcases and 4 provenance-tamper subcases. Valid/BOM fixtures
reach mocked signature verification exactly once; invalid provenance never does.
`python scripts/validate_github_workflows.py --self-test`: selftest and 3 YAML
files PASS. `python scripts/test_no_p25_guard.py`: PASS.
`scripts/test_release_commands.ps1`: PASS native failure propagation, real
temporary-manifest signing, unchanged trust anchor and wrong-key rejection.
No runtime/C++/DSP changes. Existing real 0.2.89 assets also passed the unchanged
production verifier with OpenSSL signature verification.

Remote confirmation: commit 2b615bab75d694bf7df9568deed78aaeab4c2fc1,
https://github.com/blkph0x/SDR_Town/actions/runs/35984373766, SUCCESS in 10m56s.
All required stages passed: negative release tests, P25 guard, MSVC Release
app/native builds, core/Qt/SSTV tests, clean staging, ZIP/checksum and upload.
Workflow YAML validation run 35984373768 also passed. CI artifact 10802101137
is unexpired, 15429138 bytes, digest
`sha256:a40a4ff10a0747db529f67dec212d952750d2ec11926bf6b9c78abad64208bbd`.
Release publication was correctly skipped on master; no release asset replaced.

## 2026-09-24 - DEC-0117 SSTV RF route verification

Host/toolchain: existing Windows MSVC 2022 x64 Release / Qt 6.11.1.
`cmake --build build --config Release --target SDR_Town sdr_town_tests
sdr_town_workspace_tests -j 4` (targets also built in separate invocations): PASS.
Initial new test compile failed C3861 CHECK_THROWS_WITH; fixed missing Catch
matcher include. Initial USB/AM VIS tests failed: traced to the HfDemod 0.5*BW
cutoff, not detector thresholds. SSTV BW correction made the same tests pass.

- `[sstv-rf]`: 6 cases / 36 assertions PASS. USB, LSB, NFM auto; manual AM;
  full retained VIS; corrupt parity rejection; ambiguous AM sidebands; rate,
  out-of-capture and IQ epoch/position/gap rejection; 3 s acquisition wait and
  irregular partitions. At 2.048 MS/s with a +12 kHz channel offset, processing
  2.21 s IQ took 0.553 s USB / 0.549 s LSB / 0.556 s NFM on this host.
- `ctest --test-dir build -C Release --output-on-failure`: 4/4 PASS, 59.14 s.
  Core 373 pass / 2 optional recording skips, 205479 assertions; workspace
  32 pass / 5 optional fixture/import skips, 390 assertions; Rust 8 pass;
  Robot36 helper selftest pass. Optional skips are not RF reception proof.
- `[sstv-live-gui]`: 2 cases / 25 assertions PASS, actual window rendered and
  inspected at build-audit-20260924/sstv-rf-ui.png. Route/image selections stay
  separate; selection disabled during decode; queued route status reaches GUI.
- `scripts/test_sstv_worker.py`: all 16 full/partial Robot36/Martin1 worker/GUI
  cases PASS, including forced and Auto image format. Evidence log:
  build-audit-20260924/sstv-worker-rf-regression.txt.
- `scripts/test_sstv_rf.py`: independent real_recording.wav.gz remodulated to
  96 kHz CF32, production RF router, then production helper. Full Robot36 240
  rows recovered on Auto USB/LSB/NFM and manual AM. Mean RGB absolute difference
  vs direct audio (out of 255): 4.142 / 4.144 / 2.622 / 1.614. All below the
  declared 5% image budget. The recording's noise tail can create different
  provisional partials (ISS-0013); complete-picture success does not close that.
  Initial harness used WAV instead of helper's documented PCM+rate interface;
  corrected the harness. Main radioconda SciPy has a NumPy ABI mismatch; used
  an isolated build-audit-20260924/sstv-venv (numpy 2.5.3, scipy 1.18.1,
  soundfile 0.14.0), without changing existing Python environments.
- Actual SDR_Town.exe `--gui-dry-run --no-control-server --no-remote-diagnostics
  --gui-self-test build-audit-20260924/sstv-rf-app-smoke.json
  --gui-exit-after-ms 3000`: exit 0; no hardware RX opened.

No real on-air SSTV signal or satellite pass was available for qualification.
No P25 algorithm changes. No release assets or GitHub push in this pass.

## 2026-09-24 - 0.2.89 published package verification

`scripts/release.ps1 -Version 0.2.89 -Channel experimental` rebuilt the committed
source, repeated CTest 4/4 PASS (53.51 s), staged a clean runtime, built NSIS and
portable assets, signed the updater manifest and passed verify_release.py.
Source `560cf852c9ee0efc838268ac96defd2b97393c57`; asset-metadata tag `3820791`.
GitHub Latest v0.2.89 has all eight expected assets in uploaded state. Remote
SHA-256 digests and byte lengths match local files for every asset.

Extracted the actual ZIP into build-audit-20260924/package-smoke-0289, removed
development Qt/plugin paths and restricted PATH to Windows directories. Packaged
--version reports 0.2.89, SSTV --selftest completes Robot36, and actual GUI dry-run
starts/exits without errors/warnings and without opening RX. This is a local
runtime smoke check, not a clean-OS install or live hardware acceptance test.
Evidence: release-0289.txt, package-gui-0289.json and packaged build-info.json.

## 2026-09-24 - 0.2.89 candidate and additional repair gates

Same Windows/MSVC/Qt host as below. Release app and both test targets built.
`ctest --test-dir build -C Release --output-on-failure`: 4/4 PASS in 41.32 s.
UnitTests: 367 passed / 1 optional fixture skipped, 205443 assertions.
WorkspaceTests: 31 passed / 5 optional fixture/import skips, 381 assertions.
Rust transport: 8 passed; helper selftest passed. No skipped test is live-RF proof.

- New Doppler regressions: 3 cases / 669 assertions pass. Positive/negative/
  zero updates preserve phase; split and whole IQ match; invalid targets do not
  mutate state. USB PCM matches an unshifted reference through small blocks and
  changing offsets; decoder epoch/sample clock stays continuous.
- Actual sorted talkgroup renderer: selection survives alias edits/reorder;
  deletion leaves no unrelated selected identity. Add/edit selection is by key.
- Actual GUI dry-run: no startup errors/warnings, receiver stopped, clean exit;
  native screenshot inspected (`build-audit-20260924/gui-0289.png`).
- Repeated `python scripts/test_sstv_worker.py` on native Windows: all 16
  recorded Robot36/Martin, complete/partial, forced/auto worker/live-GUI cases
  pass pixel parity. Static HF integration guard and first-party whitespace
  checks pass; upstream SGP4 source/vector whitespace is retained verbatim.

Build failures recorded: new table test first lacked populateP25TalkgroupTable;
linking the whole registry then required decoder functions and a speaker global.
DEC-0116 separates unchanged presentation code; rebuilt app/workspace targets
and complete CTest pass. No test renderer stubs substituted for production code.

Evidence: build-audit-20260924/build-0289-presentation.txt, ctest-0289.txt,
doppler-0289.txt, sstv-0289-recordings.txt and gui-0289.json. These build outputs
are local/ignored, not shipped. Package/hash/signature verification runs as the
release gate. Live pass, RSP/direct-sampling hardware matrix and long soak remain
open. No P25 DSP/vocoder/security or WFM/NFM algorithm change in this batch.

## 2026-09-24 - Receive-chain repair branch, Windows Release

Host/compiler: Ryzen 9 3900X, MSVC 2022 x64, Qt 6.11.1. Built SDR_Town,
sdr_town_tests and sdr_town_workspace_tests from canonical Desktop folder.
Commands: cmake --build build --config Release --target SDR_Town
sdr_town_tests sdr_town_workspace_tests --parallel 3 (several incremental runs).
Optional Vulkan warning and upstream mbelib CMake deprecation remain nonfatal.

Evidence under ignored build-audit-20260924/:
- repair-targeted.txt: HF/SGP4/scan/input validation, 18 cases / 5,439 assertions PASS.
- core-full.txt: all default core cases excluding network/device-manager,
  359 PASS, one fixture skip, 204,728 assertions. Includes P25 and analog units.
- device-contracts.txt: stub lease + input validation, 2 cases / 13 assertions PASS.
- workspace-final.txt: native Windows Qt suite, 30 PASS, 5 optional fixture skips,
  371 assertions. Recording skips separately exercised by scripts below.
- sstv-worker-recordings.txt and sstv-worker-windows.txt: test_sstv_worker.py,
  16 scenarios each, full/partial Robot36/Martin1, auto/forced, worker/live GUI,
  all output images/rows/completion match reference. Native screenshots inspected.
- sstv-gui-recordings.txt: test_sstv_gui.py, 4 direct-file GUI cases PASS.
- sstv-independent.txt: independent helper images complete; Robot36 RGB MAE
  10.406 < 15 gate; Martin1 MAE 20.779 reported (no numeric gate in that script).
- CTest SstvTransportRust + SstvBackendSelftest 2/2 PASS; digital selftest MAE 0
  (private 32x32 STWN roundtrip only). verify_hf_integration.py PASS (static guard).
- hf-audit-final.txt: 2.048/2.4/10 MS/s one-second inputs in 0.076/0.088/0.262 s;
  strong-carrier recovery matches fresh decoder 0.164843 RMS. Folding-band
  rejection gates >80 dB pass without the PR #32 coarse-decimator regression.

Failures investigated, not hidden: HF test fixture initially quantized large
phases to float before sin/cos; corrected generator to double precision. Corrected
AM acquisition dependence on callback size; whole/split tests now cover all HF
modes at two output rates. Official SGP4 33334 is an intentional invalid case,
not a vector to emulate. Qt offscreen initially lacked its platform plugin path;
supplied installed Qt plugin path, then repeated GUI tests on native Windows.
SSTV record tests exposed extra file filtering; removed it and compared all image
records after fixing stale single-image-only test assumptions.

No real RSP/V3/V4 acceptance. Enumeration found a generic R820T; the installed
SDRplay module returned sdrplay_api_Open failure and no SDRplay service was listed.
No claim this diagnoses the remote father's computer. No P25 RF/audio retest,
long soak, sanitizer or independent Inmarsat/HamDRM/complete APT qualification.

## 2026-09-24 - Audit at a2ac437 (0.2.88); no product rebuild

Windows Ryzen 9 3900X, MSVC x64 /std:c++20 /O2 /EHsc, Qt 6.11.1 offscreen.
Existing HF and non-P25 smoke executables compiled and PASS. Non-P25 harness
initial link omitted Advapi32; adding that required link library resolved it.
Scratch Qt build initially lacked miniaudio include path; harness corrected.
Qt aliases/SSTV tests: 24 passed, 3 optional-fixture skips, 8,901 assertions PASS.
Installed 2,237-TG alias JSON parsed repeatedly at 7.10 ms/iteration (no disk or
widget cost). Real CSV parsed read-only. Separate temporary test settings used.
Packaged helper --selftest-hamdrm PASS (32x32 private-format roundtrip only).

Diagnostic failures: HF weak-to-strong tone goes silent on master and PR #32;
master processes about 1 second IQ in 1.05 seconds at 2.4 MS/s and 2.71 seconds
at 10 MS/s. PR #32 improves speed but gives only about 3.4 dB rejection of a
10 MS/s coarse-decimation alias test. SGP4 official case 00005 position errors
1,633.895 km at epoch and 821.384 km at +360 min. Details and scratch artifact
locations: AUDIT_20260924.md. No live RF or full application acceptance claimed.

## 2026-09-20 - Live CelesTrak TLE

PowerShell and WinHTTP GET of gp.php GROUP=stations/weather/amateur return 200
and parse ISS (ZARYA) 25544. UI busy flag so Refresh TLE cannot be clobbered.

## 2026-09-20 - v0.2.74 published; package inspection

`scripts/release.ps1 -Version 0.2.74` CTest 3/3 PASS. Tag v0.2.74 = 4f26f2e.
GitHub Latest experimental. Portable ZIP listing: `SoapySDR.dll`,
`SoapyRTLSDR.dll`, `rtlsdr.dll`; no `sdrplay_api.dll` / `sdrPlaySupport.dll`
(by design). ZIP has `SdrTownControl.dll` and leftover
`SdrTownControl-0.2.71-win64.dll`. No `data/inmarsat` tree in staging/ZIP.
T-0041 opened from that listing.

## 2026-09-20 - Lease / map / Doppler / CLI (0.2.74 tree)

DeviceManager lease + Dual Tuner Soapy live mutex; diversity sets
preferredListenDeviceIndex; TLE via HttpGet/WinINet; ObserverMapWidget;
SGP4 lookAnglesTeme. Tests: [sdrplay],[satcom],[adsb],[inmarsat],[devicemanager][lease].

## 2026-09-20 - Satcom hub dock + aircraft map audio fix (0.2.71)

Satcom / Inmarsat / Aircraft live in one main-window dock tab (View → Satcom /
Inmarsat), not floating windows. Aircraft map no longer runs Mode-S or Qt
network I/O on the wrong threads; local 1090 decode is opt-in and rate-limited
so WFM/demod no longer stalls or buzzes after opening the map.

## 2026-09-20 - Native Inmarsat Aero / EGC / AMBE (0.2.71)

Clean-room InmarsatEngine (DDC + PMSK/OQPSK/EGC BPSK), ACARS/C-assign/ADS-C
parse, mbelib Aero AMBE voice follow/record, public band-plan JSON under
`data/inmarsat/`, Tools Inmarsat widget, `/v1/inmarsat/*`, capability
`inmarsatAero`. ADS-C merges into AdsBTrackStore (`fromAdsc`). FUBAR 1.1.38
Inmarsat website tab. No GPL InmarScope/jaero_dsp copy.

## 2026-09-20 - Sat catalogue polish + Aircraft map (0.2.70)

Catalogue Voice/Data/SSTV/APT badges; NOAA retired-TX honesty; bookmark-only Arm
guard. AdsBTrackStore (Mode-S DF17 + OpenSky), AircraftMapWidget OSM tiles +
popout, `/v1/aircraft/*`, FUBAR 1.1.37 Aircraft tab. No Inmarsat decrypt.

## 2026-09-20 - Satcom pass planner + Doppler (0.2.69)

Observer lat/lon, CelesTrak TLE store, compact SGP4 passes, Auto-track arm in
SatcomScannerEngine, SatCatalogueDialog + ISS SSTV handoff, control API
`/v1/satcom/observer|passes|catalogue|arm|tle/refresh`, FUBAR 1.1.36 website
mirror. Unit tests `[satcom][pass]`. Ship matching SdrTownControl.dll.

## 2026-09-20 - Satcom Scanner v1 (0.2.68)

Added SatcomScannerEngine + neon SatcomScannerWidget, SatcomAsyncLog (drop-oldest
SPSC), Ax25AprsDecoder, AptImageDecoder. Local control `GET/POST /v1/satcom/*`,
capability `satcomScanner`. FUBAR 1.1.35 website Satcom tab proxies via
`SdrTownControl_Request`. Unit tests in `tests/test_satcom.cpp`. Ship matching
`SdrTownControl.dll` with FUBAR. Out of scope: commercial decrypt, LRPT/SatDump,
full SGP4 Doppler.

## 2026-09-19 - P25 audio stall from uncached alias reparse

`p25EventLogText` (0.2.61 site Alpha Tag) called `loadP25AliasDatabase(readP25AliasFile)`
on every RFSS-stamped control event. With ~515 KiB AppData `p25_aliases.json`
that full read/parse on the GUI control path starved DSP/UI after CSV import.
Fix: `resolveCachedP25SiteAlias` + mtime/size short-circuit in alias cache. No
decode/follow changes. Workspace `[aliases]` **6/6** / **119** assertions PASS.
Release target **0.2.62**.

## 2026-09-18 - 0.2.61 alias CSV + status labels

AppData Roaming path confirmed for aliases; Local→Roaming migrate on read.
`[aliases]` workspace tests PASS after CSV/sites/status label work. Release
packaging follows via scripts/release.ps1. Windows CI still red on T-0029
Phase 2 string verifiers (133 pass / 14 fail), unrelated to aliases.

## 2026-09-18 - DEC-0101/0102 RadioReference CSV aliases

VS2022 MSVC/x64 Release `SDR_Town` + `sdr_town_workspace_tests` PASS.
`[aliases]` **6/6** cases, **111** assertions (TG+sites CSV, destination GUI).
No P25 follow/decode changes. Import CSV accepts TG-SITES-style `trs_tg_*` and
`trs_sites_*` with explicit destination WACN/System ID.

## 2026-09-18 - 0.2.60 publication

Annotated tagv0.2.60 at3ef2383 and branch pushed. Eight draft release assets
downloaded to build/release-download-0260; all SHA256s equal local files.
Published Latest (experimental manifest channel); public Latest update.json
matches signed local bytes. Installer/portable/control DLL available to testers.

## 2026-09-18 - DEC-0100 system-scoped alias lists

VS2022 MSVC/x64 Release app and workspace targets build PASS. Initial alias
suite46 assertions/3 cases PASS; expanded validation and actual modal GUI
New/Add/Edit/Reimport/Save workflow70 assertions/4 cases PASS. Full CTest3/3
PASS in42.25s. Four actual application workspace layouts PASS, no startup
errors or hardware RX; Trunking screenshot confirms Aliases button fits.
Compact560x380 alias dialog screenshot reviewed: labels/actions fit.
Final plain-text status assertion and packaged QA follow below. No user alias
database modified; tests use temporary directories and fictional system IDs.
Final incremental app/workspace build PASS; alias suite70 assertions/4 cases
PASS with plain-text status enforced. No new compiler/test failures this pass.

Release0.2.60 source ee92a51; metadata0122d75. release.ps1 PASS, CTest3/3
PASS in61.14s; signed manifest/runtime provenance/hash verifier PASS. Fresh
portable-qa-0260 extraction runs Qt25 cases/259 assertions (three optional SSTV
fixture skips), four main-window layouts and four recorded SSTV GUI cases PASS.
Test executable is QA-only, not added to published ZIP. Existing T-0029 P25
static-verifier/acceptance gaps are unchanged; this is not all-hosted-CI proof.

## 2026-09-18 - 0.2.59 publication verification

Pushed branch and annotated v0.2.59 tag at aa5e766. All eight assets uploaded
to a draft, downloaded into build/release-download-0259, SHA256 equality with
local assets PASS. Published non-prerelease Latest with experimental manifest
channel (updater uses releases/latest). Public Latest/download/update.json
matches signed local manifest bytes. Portable Qt lifecycle suite also PASS:
21 cases/189 assertions, three fixture cases covered by separate harnesses.

## 2026-09-18 - 0.2.59 release package QA

release.ps1 -Version0.2.59 -Channel experimental -SkipPush -SkipAssets PASS.
Release CTest3/3 PASS in23.50s: core288 passed/one fixture skip,198438
assertions; Qt21 passed/three fixture skips,189 assertions; Rust5 passed.
Fixture harnesses supply those optional recordings separately, not count skips
as passes. Signed manifest, hashes, portable DLL provenance and required helper
verification PASS. Source287ff35; generated asset metadata65a00e2.
Fresh build/portable-qa-0259 extraction: combined worker/live GUI16 cases PASS;
recorded GUI4 cases PASS; CLI forced/auto/full/partial/Unicode/no-overwrite,
silence and resource rejection PASS; actual GUI four layouts PASS with no RX
or startup errors. Test executables copied into QA extraction only, not ZIP.
Known-image live RF and HF input are still not qualified. Existing T-0029 P25
static-verifier gap is unchanged and is not represented as green hosted CI.

## 2026-09-18 - DEC-0099 live SSTV receiver and GUI

Windows x64 / VS2022 MSVC Release. Initial app build failed because Qt's slots
macro collided with SstvLiveInput::slots in the newly included header (lines
44/45). Renamed the private constant slotCount; subsequent app/core/workspace
build passed. CTest: 3/3 passed, including five Rust transport tests.
After graceful-finish queue draining was added, test_sstv_worker.py passed all
16 worker/live-GUI full/partial forced/auto Robot36/Martin1 cases (11 or14
assertions each). Recorded GUI regression: four cases,13 assertions each PASS.
Actual saved GUI previews reviewed at820x600 and560x420, no clipped controls.
Native application Tools menu opens SSTV Images and exposes Live NFM source.
This is recording-driven live-session validation, not known-image RF acceptance.
Package/version0.2.59 build and extracted-runtime QA are recorded separately.

## 2026-09-18 - DEC-0098 combined SSTV stream worker

Release app/workspace tests build PASS. First integration run failed QImage
equality between in-memory RGB888 and loaded PNG; normalized reference format
to RGB888 in the test, with no decoder change. Exact pixel comparison then
passes all eight full/partial forced/auto Robot36/Martin1 cases (11 assertions
each), through actual input queue, converter and pipe helper. Provisional
callbacks confirmed off the GUI thread; final metadata and pixels validated.
Native worker tests: three cases/19 assertions PASS, including empty EOF,
GUI-thread rejection, idle cancellation and nine faults with exact expected
error categories. No sdrtown_sstv process remained after the fault tests.
Full CTest 3/3 PASS in 16.77 seconds; stricter error-category assertions rebuilt
and rerun afterward. Recorded GUI regression four cases PASS (13 assertions
each). Optional-helper-disabled builds skip helper-dependent tests; enabled
workspace target stages its helper and requires it to exist.
Live RF/paced overrun acceptance and controller/UI integration remain open.

## 2026-09-18 - DEC-0097 isolated SSTV rate converter

Release core tests/application build PASS. New rate tests: five cases /89
assertions PASS, plus independently supplied recording export case. Exact float
output equality for chunk sizes 1,137,4096,8192 at six rates including
2048000/43 Hz. Six-minute zero-input count/drift checks pass; in-band tone,
bypass, reset and invalid-state tests pass. Counts allow the native resampler's
one-input-interval startup, not fabricated tail samples.
scripts/test_sstv_rate.py: eight full/partial forced/auto image gates PASS.
Reference RGB MAE before -> after: Robot36 forced 10.406471 ->10.391415;
Robot36 auto 14.302396 ->14.318051; Martin1 forced/auto 20.779395 ->20.730420.
All partial row counts unchanged. The +1 RGB-level regression budget in
DEC-0097 was set before this run, not fitted afterwards.
scripts/test_sstv_stream.py eight parity cases/negative lifecycle tests PASS;
scripts/test_sstv_gui.py four recording cases PASS. Full CTest 3/3 PASS in
15.49 seconds. No receiver/P25 processing path changed. No live RF gate claimed.

## 2026-09-18 - DEC-0096 streaming SSTV transport

Release SDR_Town/helper build passes on existing Windows/MSVC/Rust toolchain.
Five Rust PCM-reader units PASS; now included in SSTV-enabled CTest with offline,
locked dependencies. Final CTest 3/3 PASS in 14.13 seconds.
scripts/test_sstv_stream.py PASS: eight independent Robot36/Martin1 cases,
forced/auto and full/15-second partial. Fragmented pipe writes (including odd
byte boundaries) produce identical JSON rows, metadata and RGB to file input.
Every case observes progressive rows before stdin EOF. Odd byte count and
over-budget input rejected; existing output rejected; idle child killed/reaped.
scripts/test_sstv_images_cli.py PASS including Unicode, partial, silence,
resource validation and reference parity. Robot36 forced MAE 10.406471 and auto
14.302396 unchanged. scripts/test_sstv_gui.py four cases PASS (13 assertions
each). No live receiver or rate-converter qualification is claimed.

## 2026-09-18 - DEC-0095 isolated live SSTV input queue

Release SDR_Town, core tests and Qt tests build successfully. Full CTest:
2/2 targets PASS, 13.84 seconds. Added six ingress cases (8606 assertions),
repeated 20 times successfully, including 20000-block concurrent producer /
consumer per run. Checks verify generation boundaries, ordering, exact samples,
discard accounting, malformed input, source changes and fixed resource limits.
Stress testing is not a deterministic proof of every thread interleaving or
an RF throughput qualification. No live path is attached yet.
test_sstv_gui.py: full/partial Robot36 and Martin1 PASS, 13 assertions each.
P25 code unchanged; existing static-verifier gap T-0029 remains open.

## 2026-09-18 - DEC-0094 progressive SSTV / 0.2.58

Windows/MSVC 14.44 /Qt 6.11.1 /Rust 1.88.0. Release app/helper/tests build
passes. No backend DSP/pixel math changed: optional row JSONL is generated
from the same RGB canvas. New C++ parser validates row transport and checks
assembled image equals final file before publishing. UI holds latest snapshot.

CTest PASS: 275 core cases/189730 assertions +17 Qt cases/155 assertions;
one reference-dependent case is explicitly skipped in default CTest. Separate
test_sstv_gui.py runs it four times (Robot36/Martin1 full/15s partial), all 13
assertions pass each, multiple preview callbacks and exact direct/GUI image
parity. Preview screenshots at 820x600 and 560x420 generated; Martin1 partial
small view inspected (30/256 rows, missing rows black, no overlap).
Native tests cover fragmented records, out-of-order row positions, duplicate/
bad row/RGB/index, incomplete lines/images, zero-row partials, missing completion
rows, 4096-byte line/4-MiB transport/four-image limits, 1000 latest-only preview
updates, cancellation clearing provisional image, teardown and worker isolation.
CLI SSTV image/negative tests, RDS/CTCSS/DCS/registry regressions pass. Four
main GUI workspaces pass without startup errors. Package/remote checks pending.

Packaging passes: release.ps1 -Version 0.2.58 -Channel experimental -SkipPush
-SkipAssets builds NSIS/ZIP/control DLL, reruns CTest, signs with existing key
and verifies hashes/required runtime/notices. Eleven verifier tests and signing/
failure tests pass. Extracted portable SSTV image and RDS CLI tests pass, plus
four GUI workspace layouts. Test-only workspace executable copied into QA
extraction (NOT the shipped ZIP) runs all four SSTV GUI full/partial cases using
packaged helper/DLLs: 13 assertions each pass. test_sstv_gui.py now accepts --exe
for this repeatable package-runtime gate. No clean-host installer upgrade claim.
GitHub draft upload/download hash check remains before publication.

Publication PASS: source and v0.2.58 tag (4006b94) pushed. All eight draft
downloads SHA-256 identical to local assets; released as GitHub Latest in the
experimental updater channel. Public latest/download/update.json byte-identical
to signed local manifest. No P25 source/DSP changes versus 0.2.57. Hosted CI is
separate from these local/package gates; T-0029 string-check failures remain open.

## 2026-09-18 - DEC-0093 recorded SSTV GUI

Windows/MSVC 14.44 /Qt 6.11.1. Added nonmodal window and worker cancellation
to shared file decoder. Initial application build passes; Qt test build fails
C3861 CHECK_THROWS_WITH because Catch2 matchers header was missing. Explicit
header added, rebuild passes. Review also fixed cramped list labels and parent
ownership of finished thread objects. No protocol/backend timing changes.

Final Release build passes. CTest: 275 core cases /189730 assertions, 13 Qt
cases /109 assertions pass; one independent-recording case is skipped unless
its fixture environment is supplied. scripts/test_sstv_gui.py supplies it for
Robot36 and Martin1 separately: both pass 10 assertions, GUI output pixels
equal direct decoder, UI timer advances, image preview nonempty. Screenshots
reviewed at 820x600 and 560x420. Worker cancellation/close, parent teardown,
one-job gating, invalid input, failed/empty result states covered separately.
Actual CLI SSTV images/negative/resource tests still pass; RDS and registry
CLI pass. Main GUI four-workspace automation passes with no startup errors.
Version remains 0.2.57 plus unreleased GUI source; published assets unchanged.
No live SSTV/progressive acquisition, clean-machine qualification or additional
P25 acceptance claimed. T-0029 existing P25 verifier failures remain open.

## 2026-09-17 - DEC-0092 offline SSTV images / release 0.2.57 preparation

Windows/MSVC 14.44, Qt 6.11.1; repo-local Rust 1.88.0 installed from official
SHA-256-checked rustup bootstrap without changing global PATH. Initial helper
compile rejected u32->usize dimensions; explicit checked conversions fixed it.
Cargo --release --locked build then passes; pinned backend plus libm 0.2.16 only.
First fixture harness rejected stereo M1 source; first-channel extraction now
matches existing independent VIS test. No app stereo acceptance was relaxed.

Independent Robot36 off-air recording gives complete 320x240 and RGB MAE
10.406471 against upstream patch.png (<15 gate). M1 independent OGG gives
complete 320x256 BBC test card, visually inspected against reference; RGB MAE
20.779395 is recorded, not treated as exact/reference colour equivalence.
Both helper images viewed. No reference captures are packaged.

cmake -S . -B build -DSDR_TOWN_ENABLE_SSTV_IMAGES=ON and Release app/core/Qt
build pass. Actual CLI test initially incorrectly required forced/auto Robot36
pixels identical. Auto acquisition differs; corrected gate compares each path
to matching helper options, then independently checks reference MAE. Forced
10.406471 and auto 14.302396 both pass <15. Exact app/helper RGB parity passes
for both modes/options. Partial truncation, Unicode, no-overwrite, silence,
bad mode/rate/channel/format/duration/size rejection all pass.

CTest passes 2/2 executables (284 core/Qt cases). SSTV VIS independent header,
RDS, CTCSS, DCS and registry actual CLI suites all pass. Eleven updated release
verifier tests pass, plus native-command/signing/trust-anchor tests. GUI and
packaged-runtime checks are next; no live SSTV or P25 acceptance claimed.

Follow-up: actual GUI Listening 960x720, Trunking 1280x900, HF 800x700 and
Analysis 1600x900 automation passes with no RX/startup errors. Listening screenshot
reviewed. CTest totals: 275 core/189730 assertions +9 Qt/87 assertions. P25 DSP
and GUI receive routing are unchanged.

Packaging PASS: release.ps1 -Version 0.2.57 -Channel experimental -SkipPush
-SkipAssets reconfigures SSTV ON, rebuilds/stages runtime, reruns CTest, builds
NSIS/ZIP, signs and verifies manifest with existing trust anchor. App/helper,
RTL runtime and all license files match staging. Extracted portable image/VIS
and RDS suites pass; four GUI workspace sizes pass with no startup errors.
Five concatenated off-air Robot36 recordings explicitly fail at fifth image
(exit 1, image/session limit exceeded, exactly four prior records), as designed.
git diff --check passes for authored files; original upstream Rust copyright
HTML retains its whitespace unchanged. No installer upgrade on a clean host
or live SSTV reception claimed. Ready for draft upload/download hash validation.

Draft asset verification PASS: eight GitHub downloads byte-identical to local
assets. Hosted previous checkpoint run 35225600073 failed P25 source verifiers;
local full sweep confirms PASS=133 FAIL=14. P25 source/header/DSP/verifier diff
between v0.2.56 and v0.2.57 is empty. Record existing QA gap as T-0029; do not
claim hosted CI green. New SSTV and native/CLI/GUI runtime gates remain passing.

## 2026-09-17 - DEC-0091 SSTV VIS development

Initial Release compile succeeds with int/float fill and int/bool parity test
warnings; explicit literal/type corrections made. First SSTV subset: 4/5 cases
pass, consecutive-header/rate test reports one instead of two events. Added
rate/chunk context and fractional-rate fixture tail before rerunning; no
production tolerance changed. Reference check and final gates pending.

Rebuild passes; five SSTV cases / 642 assertions pass before adding the noise
negative. Fractional fixture concatenation at 11025 Hz rounded both 910 ms
headers down, ending one sample before the second header's rational deadline;
one-ms trailing silence corrects the fixture, detector timing unchanged. Actual
CLI independent m1.ogg first three seconds: VIS 44/Martin M1, start sample 36691
(~0.832 s), end 76822, one candidate, zero framing/parity rejects. File SHA-256
recorded in SSTV.md and enforced in CLI test. Silence, stereo, low rate, >120 s,
malformed/missing files and truncated independent header rejected as expected.
Reordered identical header predicates to reject flat leader tones before the
full interior scan (no new thresholds); final build/full regression pending.

Full regression PASS (275 core + 9 Qt cases); RDS/CTCSS/DCS CLI checks PASS.
Additional non-English filename test then failed to produce a JSON result.
Confirmed narrow miniaudio fopen_s/ACP and filesystem path usage; switched the
new SSTV loader to UTF-8 filesystem conversion and Windows wide-file API.
Final rerun including that negative-to-positive test pending below.

Wide-file change alone still failed: CLI batch echo contained recording-??.wav
before opening the file. Moved batch string construction after QCoreApplication
and read Qt's Unicode arguments; existing numeric/startup flags remain untouched.
This is a CLI argument fix, not any P25 processing change.

Final build PASS. Unicode-path and >64 MiB tests now PASS; independent M1 and
all SSTV file rejection gates PASS. Existing RDS, CTCSS, DCS and registry CLI
gates re-run after the argument change: all PASS. CTest final core/library build:
275 cases / 189730 assertions plus 9 Qt cases / 87 assertions (284 total).
SSTV subset contributes 5 cases / 643 assertions. No P25 DSP, security, voice
scheduling or GUI/live receive code changed. No image/live SSTV acceptance claimed.

## 2026-09-17 - DEC-0090 release hardening

Initial verifier tests: 11 errors because host Python lacks hashlib.file_digest
(introduced after this interpreter). Replaced with bounded streaming SHA-256;
no Python environment replacement. PowerShell parser passes all three release/
signing scripts. Full release/asset gates recorded below after execution.

0.2.56 Release build PASS. CTest: 270 core cases / 189087 assertions and nine
Qt cases / 87 assertions PASS. RDS, CTCSS, DCS and registry CLI tests PASS;
four workspace GUI sizes/layouts PASS (build/release_0256_workspace). Live
30-second GUI RDS under CDB PASS: 262 groups, PI 0x2981, PS i98FM, zero adapter
differences across 960 blocks, no AV; this run's final radiotext was empty, not
a text-acceptance claim. Evidence: build/release_0256_live. Verifier 11 tests
PASS after Python compatibility fix. test_release_commands.ps1 PASS: actual
native exit failure, real signing, unchanged embedded key, mismatch rejection.
Initial diff check caught Markdown trailing spaces and signing EOF blank line;
removed before commit. Packages/upload verification still pending at this entry.

Packaging PASS: checked helper -SkipPush -SkipAssets rebuilt both test targets,
reran CTest, Qt deployment, CPack NSIS, ZIP and standalone DLL. OpenSSL verified
the detached manifest against the embedded key; all asset hashes and configured
RTL runtime match. Installer 21069901 bytes, ZIP 28943502 bytes, DLL 49664 bytes.
Extracted ZIP RDS/registry CLI and four workspace GUI tests PASS. PCM comparator
five tests and independent IQ diagnostic tests PASS. Local asset hashes are in
SHA256SUMS.txt; upload/download gate remains separate.

Publication PASS: source branch and annotated v0.2.56 tag pushed; draft release
uploaded eight assets. Downloaded all eight and compared each SHA-256 with its
local original: all match. Published as experimental Latest; public latest
update.json reports 0.2.56/experimental. Hosted CI was still running, not counted
as a passing gate. No installer upgrade on a clean remote machine was performed.

## 2026-09-17 - DEC-0088/0089 reproduced native fault and deployment fix

Baseline CDB runs build/shutdown_probe_01..05.log: first four clean; fifth
captures first-chance AV in libusb add_to_flying_list -> submit/control transfer
-> RTL driver -> Soapy Device::unmake -> GUI shutdown. Export-only RTL offsets
do not identify exact RTL source lines. Caught exception is still a failed gate.
Standalone probe_rtlsdr_lifecycle.py with shipped DLL failed cycle 2: async
read returned -5 after cancellation; rtlsdr_close raised access violation
writing 0x24. No Qt/Soapy/application DSP involved. Legacy DLL retained under
build/rtl_runtime_evidence/rtlsdr-legacy.dll (Windows resource v0.7.0-190-gdfd8).

Configured vcpkg rtlsdr package 2.0.2 passed 10 native lifecycle cycles. Its
libusb file is byte-identical to the executable folder's existing libusb
(SHA256 b8a4895ad50645ad5757931ccf97d9e2b3873f7906aeaf42305f8a633d7cf3c1).
CMake stage_rtl_runtime now installs the configured imported shared RTL target
on executable builds. Deployed DLL passes another 10 native cycles. No
application teardown, P25, gain or timing changes used to hide the fault.

Release build and deploy PASS, MSVC 14.44.35207 x64. Configured/executable/
deploy_staging rtlsdr hashes all match:
b0a46ed5ed803764e42e37d9a3eb3ba6af003a1d4b27ccf1f55e37ff9fa7cec5.
licenses/rtlsdr-COPYRIGHT.txt present in deploy staging. No installer/release
publication performed. Deployed copy only replaced by declared CMake dependency.

build/rds_runtime_fixed_cdb: 30-second actual GUI under CDB PASS: 257 groups
in GUI final report, 258 in final parity snapshot, PI 0x2981, PS i98FM, RT,
zero differences over 960 blocks, no AV, normal exit. Different report moments
explain one additional group at teardown; all per-block results match.
build/shutdown_fixed_qa: five 12-second real-hardware GUI starts/stops PASS,
zero first-chance AVs, no native teardown warnings, audio destruction verified.
Full CTest PASS (279 core/Qt cases); actual RDS CLI tests PASS. Local R820T only;
other devices/driver failure modes and physical V4/HF tests remain unverified.

## 2026-09-17 - DEC-0087 gain isolation and successful RDS acceptance

Independent rtl_sdr (radioconda) five-second captures at 98.1 MHz, 2.048 MS/s:
requested gain 40 -> actual 40.2 dB, zero groups; gain 20 -> actual 19.7 dB,
53 groups, zero corrected/rejected groups, PI 0x2981, PS i98FM and full RT.
High-gain repeat: zero groups again. Raw byte rail incidence (0 or 255) is
32.1695%, 0%, 31.8193% respectively. Artifacts build/rds_direct_gain*.cu8/.wav.
Thus high gain demonstrably damages this strong-station capture. No universal
gain recommendation or automatic AGC/DSP change is inferred. Earlier cf32
non-clipping observation was insufficient to rule out input overload.
Widening the earlier saved IQ analysis to 240 kHz still produced zero groups.

Actual GUI with temporary requested 20 dB through authenticated loopback API:
- build/rds_gain20_gui_01: 399 groups in final GUI JSON, correct metadata;
  parity summary 400 groups, zero mismatches/failures. Overall FAIL: process
  exit 3221225477 (0xC0000005) after logging normal application exit intent.
  Windows Event 1000 records ntdll.dll offset 0x3fbb8. Previous 40 dB restored.
- build/rds_gain20_gui_02: PASS 396 groups, PI 0x2981, PS i98FM, RT, normal
  process exit; parity zero mismatches over 1,434 blocks. CDB attached near
  shutdown; saw a first-chance AV but no unhandled fault stack. Not crash proof.
- build/rds_gain20_gui_03: FAIL hardware enumeration (USB strings failed,
  safe stub only), no reception claim. Added explicit runtimeState=='live hardware'
  assertion; streaming alone is not sufficient. Direct RTL recheck succeeded.
- build/rds_gain20_gui_04: PASS 261 groups in 30 seconds, correct PI/PS/RT,
  real hardware, normal exit; previous gain restored. CTest ran during this
  last probe; not a performance benchmark.

CDB 8-second hardware shutdown probe exited normally without reproducing the
unhandled fault. Logs build/rds_shutdown_firstchance.log and
build/rds_shutdown_gain_stack.log retained; no speculative teardown edit.
No C++ changes this pass; existing Release build used. Full CTest PASS (279
core/Qt cases). Python diagnostic tests PASS including exact cu8/cf32 parity,
MPX units, short/nonfinite input and incomplete complex-byte rejection.
No packages installed, no P25 changes, no release/push. T-0021 gate met;
T-0026 records intermittent shutdown/driver follow-up separately.

## 2026-09-17 - DEC-0086 same-input RDS isolation

Release app/core build PASS, configured MSVC 14.44.35207 x64. Full CTest PASS
(279 core/Qt cases). test_rds_cli.py PASS including new JSONL diagnostic.
Instrumented `[decoder]` PASS: 5 cases / 24,909 assertions; three recorded
partition runs with explicit source/cursor/rate/epoch changes have zero parity
or reset mismatches. test_rds_iq_diagnostic.py PASS normalization/sample counts
and invalid IQ rejection. No production DSP or P25 change.

Live `--parity` result build/rds_live_parity_01/parity.jsonl: 1,446 blocks,
7,372,596 total input samples, zero adapter/native mismatches, zero reset
mismatches or adapter failures. Both produce 41,978 bits and zero valid groups;
live station gate still FAIL. Rate 204,800 Hz, target 98.1 MHz, 3 resets.
Thus the new adapter does not explain this run's acquisition failure.
CTest briefly ran during this probe; this is not a CPU/performance measurement.

Independent bounded GUI IQ capture:
build/rds_rf_capture/20260917_122423_080_rds-parity_98.10000MHz_startstop.
5.024 seconds, 82,313,216 bytes, cf32_le at 2,048,000 Hz, no gaps/overruns/
epoch resets, gain 40 dB, PPM 0. Sample extrema -0.97969..0.96563; no components
>=0.99 magnitude. This does not exclude RF front-end compression/interference.
Independent FFT-filter/angle FM diagnostic at 180 kHz bandwidth produces
build/rds_rf_mpx.wav: replay has 5,936 bits, zero groups. Its relative spectrum
has a pilot-band peak but does not by itself establish RDS presence or quality.
No antenna, gain, filters or thresholds changed to force a pass.

Rate check: known 192 kHz fixture Fourier-interpolated to 204.8 kHz and replayed
through actual CLI still produces 2 valid groups (143,360 samples, 831 bits).
Initial SciPy analysis attempt failed because installed SciPy is incompatible
with NumPy 2; used NumPy FFT instead. No global package modifications.
Root RF/shared-decoder cause remains unproven. User asked whether antenna/cabling
changed; preserve evidence and do not advertise a reception fix.

## 2026-09-17 - DEC-0085 receive decoder contract

Initial Release core build passed. Five adapter tests / 24,909 assertions pass,
including direct-versus-adapter RDS recorded MPX comparison at every block for
137/1000/8192-sample partitions and source/gap/rate/target/epoch changes. DCS
adapter parity and schema/domain/metadata rejection pass. RDS GUI and file replay
adoption follows this gate; full application/live regression still to run.

Final Release app/core/Qt build PASS (Windows, configured MSVC toolchain).
`ctest --test-dir build -C Release --output-on-failure`: PASS, 270 core cases /
189,087 assertions and 9 Qt cases / 87 assertions. Actual CLI registry, RDS,
CTCSS and DCS Python tests PASS. `test_workspace_gui.py --output
build/workspace_contract_qa`: PASS all four presets/sizes; Listening screenshot
visually reviewed. `git diff --check`: PASS (line-ending notices only).

Live acceptance FAIL, not waived: `test_rds_live_gui.py --frequency-mhz 98.1
--expect-pi 0x2981 --expect-ps i98FM`, two 45-second GUI/hardware runs.
`build/rds_contract_live_qa/result.json`: 7,244,800 samples, 41,988 bits,
1 group, 46 rejected groups, 3 resets, no identified station.
`build/rds_contract_live_repeat/result.json`: 7,265,280 samples, 42,121 bits,
0 groups, 11 rejected groups, 3 resets, no identified station.
Both report the new raw-fm-multiplex v1 contract and correct 98.1 MHz target.
First failing assertion: identified && groups >= 3. Prior DEC-0084 run received
355 groups at this frequency. No cause established: neither RF variation nor
an interface regression is proven. Recorded-input parity is not live acceptance.
T-0021 remains in progress; next diagnostic needs identical live MPX through
native and adapted backends plus raw input evidence, without DSP retuning.

## 2026-09-17 - DEC-0084 DCS

First core build failed C1075 in test_dcs.cpp: unclosed helper namespace.
Fixed the test namespace; decoder library itself compiled successfully.
First six DCS tests: five passed; catalogue size assertion failed (105 actual
versus 104 assumed from upstream comment). Direct extraction/comparison verified
105 values in both lists, no differences. Corrected count, not protocol data.
Remaining 72,720 assertions passed including shaped/noisy waveform cases.
Initial actual-CLI harness incorrectly assumed aliases was the first JSON key;
output correctly detected 023N/047I but harness failed to select it. Fixed harness
to parse JSON and select decoder field. Existing CTCSS and RDS CLI tests pass.
Release app/core/Qt build succeeded. Eight DCS cases / 74,727 assertions pass:
all 105 codes x two polarities x 23 rotations, repeated-word gates, malformed
bits, chunking, rates, synthetic shaping/DC/speech/baud error, two-minute noise,
tone rejection and bit-identical speaker PCM with raw FM data decoding.
Actual DCS CLI tests pass for all eight independent WAVs and bitstreams, alias
equivalence, input limits and malformed/missing files. Full/live gates underway.

Full CTest PASS: 265 core cases / 164,178 assertions; 9 Qt cases / 87 assertions.
Actual GUI matrix PASS at 960x720, 1280x900, 800x700 and 1600x900. Listening
screenshot visually inspected: status strip/controls readable. 45-second live
NFM GUI PASS at 476.4625 MHz: both decoders consumed exactly 1,687,219 samples
and had three startup resets; CTCSS completed 35 windows, neither reported a
confirmed tone/code. This proves integration, not known-code RF sensitivity.
No RX audio logic changed. CTCSS/RDS CLI regressions pass. Live 98.1 MHz WFM
regression PASS: 355 RDS groups, PI 0x2981, PS " i98FM  ", RadioText
"Feel Good - i98FM", 12 rejected groups and three startup resets. GUI closed
normally. Known-code DCS RF and adaptive-clock/fading qualification remain open.

## 2026-09-17 - CTCSS implementation and live failure investigation

Release application/core/Qt builds pass. Full CTest: 257 core cases / 89,451
assertions; 8 Qt cases / 83 assertions. CTCSS CLI generated WAV/error tests,
RDS CLI regression and four GUI workspace launches pass. Seven CTCSS cases
cover 38 frequencies, rates, gain, interference, two-minute noise, source gaps
and audio parity. Live 476.4625 MHz checks failed three times: initial reset
count 22, first fix 15, instrumented 15-second run 5. Actual radio connected;
no false claim of tone verification. Changing BW synthetic regression failed
before the fix, passes afterward. Instrumented source identified additional
speech AFC reset propagation (reason 2); NFM mixer isolation is under retest.

Mixer-isolation retest passed: build/ctcss_live_qa_isolated, 385358 samples and
eight complete windows in final stream, zero confirmed tones (no known live
tone reference). Logs exposed three further bandwidth-only GUI cursor resets;
their fix is under retest. CTest still passes 257 core/8 Qt cases.
DCS reference script passes four independent standard vectors, all 512 payload
parities and single-bit error checks; generated eight bounded ideal WAVs.

Final 45-second GUI NFM run (build/ctcss_live_qa_final): PASS, 1,688,410
discriminator samples, 35 complete windows, three startup/hardware-handoff
resets and no subsequent data reset logged. No known tone present/verified.
Full CTest passes again (257 core + 8 Qt). Actual CTCSS/RDS CLI scripts pass
after the GUI closes. Attempts during GUI RX exited 2 due to the single-instance
guard; these were test scheduling errors, not decoder failures. Live WFM/RDS
regression passed separately after the NFM test: 363 groups, PI 0x2981,
PS " i98FM  ", RadioText identifying Roxette / Listen to Your Heart, three
startup resets, 39 rejected groups. Hardware teardown again emitted its existing
recoverable Soapy warning and exited 0; not claimed fixed by this work.

## 2026-09-17 - DEC-0080 live RDS / waterfall

Release application and test targets build. Core: 250 cases / 81,460 assertions;
Qt: seven cases / 79 assertions, including actual mouse-event drag/squelch tests,
RDS stale/retune/plain-text presentation and clipped priority band sections.
RDS data-only reset preserves exact speech output in regression tests.
CLI bit/MPX fixture/error checks pass. Four actual GUI screenshot/layout runs
pass at 800x700, 960x720, 1280x900 and 1600x900; screenshots inspected.

Actual RTL hardware GUI at 98.1 MHz displayed i98FM / PI 2981 / PTY 10 and
"On Air Now - Ned and Josh at Night". Official https://i98fm.com.au/shows
independently lists that programme (not independent RF/PI validation).
Native drag from waterfall retuned to 98.178614 MHz, cleared station metadata
and left squelch unchanged. Revealed stale frequency field, fixed and rechecked
visually at 98.10000 MHz. Preview axis now snapshots under the spectrum mutex.

Repeat automated 45-second live result: build/rds_live_qa/result.json, streaming
real hardware, zero startup errors, samples=7249920, bits=42038, groups=402,
rejectedGroups=1, resets=3, PI=10625 (0x2981), PS=" i98FM  ", radiotext
"i98FM - Calm Down - Rema , Selena Gomez". Run log has no ring-overrun warning.
Three initializations are reported cumulatively; do not claim zero resets.
Exit 0 with recoverable Soapy teardown warning, recorded as open in ISSUES.
CLI test initially attempted during live GUI was blocked (exit 2) by existing
single-instance guard; rerun after GUI shutdown passed. No P25 decoder or
audio scheduling changes, no release/push, no universal RF acceptance claim.

## 2026-09-17 - DEC-0079 recorded MPX and continuity

Release app, native tests and isolated Redsea/liquid-dsp DLL built successfully.
CTest: 250 core cases / 81,456 assertions and four Qt cases / 59 assertions.
Targeted RDS: nine cases / 210 assertions. Actual CLI script passes bit-reference,
recorded MPX, identity gate, malformed-input and missing-file cases. The recorded
fixture produces two complete groups; native checks match PI 0x6201 and PTY 14.
137/1000/4096-sample DSP partitions match counts and final group payload.
Optional WFM data tap preserves audio and survives irregular IQ partitions.
objdump DLL imports: KERNEL32.dll and msvcrt.dll only. git diff --check passes.

Failures retained: original tap test failed continuity at the second 137-sample
block; separate causal tap fixed it without changing speech DSP. Initial fixture
test incorrectly expected three groups; upstream test explicitly requires two,
so test now validates payload without weakening our three-observation PI gate.
Initial ExternalProject probe reused a differently named compiler cache and lost
make configuration; dedicated rds-dsp-runtime directory solved it. First app link
failed LNK1104 opening SDR_Town.exe. Process inspection found no running app;
retry linked successfully. Existing Vulkan-header and mbelib-CMake warnings remain.

Not claimed: live RF RDS reception, GUI RDS metadata, frequency-offset/noise
characterization, release packaging execution, or new P25 audio validation.

## 2026-09-17 - DEC-0078 RDS protocol foundation

Release SDR_Town and sdr_town_tests build successfully. CTest passes 247 core
cases / 81,295 assertions and four Qt cases / 59 assertions. Six new RDS/MPX
cases contribute 49 assertions: upstream reference group, independent encoder,
FEC correction/rejection, noise, PI/PS/text lifecycle and audio equivalence.
Optional tap preserves 57 kHz content before the 3 kHz audio LPF and gives
sample-grid/reset provenance; enabled/disabled audio vectors compare exactly.

First CLI smoke failed because std::quoted consumed Windows backslashes.
Fixed only the new command's path parser; rebuilt. test_rds_cli.py now passes
with an absolute Windows path, missing file and malformed bits. Offline CLI
log confirms device enumeration skipped. All nine vendored source/license
files hash-match pinned redsea revision. git diff --check passes.
Initial bool/int comparison warning removed; existing mbelib CMake and LTCG
notices remain. No RF RDS or GUI station-metadata claim; no new P25 tuning.

## 2026-09-17 - DEC-0077 jitter repair and replay verification

The new PCM partition test failed before the repair (maximum discrepancy
0.179083526 full scale, 160-sample chunks versus one batch). With the causal
cubic stencil it passes all 18 assertions, including single-sample chunks
and 44.1/48 kHz output. Release build passed. CTest: 241 core cases / 81,246
assertions and 4 Qt cases / 59 assertions, both pass. Four actual GUI layout
launches pass in build/workspace_jitter_qa; Listening screenshot inspected.
Spectrum dB labels now follow the spectrum coordinate transform.

Captured 89.968 seconds live at 420.350 MHz (20260917_084229): no IQ overruns,
no producer drops, but 19 underrun rises and missing/rejected voice frames.
The TG30003 slot-1 47.5-59.5-second replay retains exactly the pre-repair
456 decoded frames / 437760 output samples / duty 0.76, including 67
concealment frames. GUI replay also emits 437760 samples with zero discarded
tail. This does NOT prove subjective clarity or full live continuity.

Sample-level comparison finds GUI/CLI PCM is NOT equivalent on this fixture:
max absolute difference 0.301809931, RMS difference 0.010476168, 327/456
20-ms frames differ beyond 0.000062. First 18 frames agree within that bound.
Do not infer parity from equal counts. Artifacts: build/jitter_tg30003_after.*,
build/jitter_tg30003_gui_after.*, build/live_jitter_baseline_audit.json.
scripts/compare_pcm_wav.py compares RIFF PCM16/float32 without dependencies.
System Python SciPy/NumPy is ABI-incompatible; the project STT venv works,
but plausible transcripts are not known-reference intelligibility proof.

## 2026-09-17 - REQ-BP.1 receive profile verification

Release builds of SDR_Town, sdr_town_tests and sdr_town_workspace_tests pass.
Removed C++20-deprecated shared_ptr atomic free functions in favour of atomic
shared_ptr. Existing external mbelib CMake compatibility and /LTCG messages
remain. CTest: 240 core cases / 81,228 assertions, 4 Qt cases / 58 assertions.
AU data channel overrides, conflict resolution, import rejection, thread-safe
selection and GUI preview/Apply/cancel are exercised. GUI tests use temporary
settings and do not persist into the user's profile selection.

Actual GUI automation: `build/bandplan_qa/` contains four passing startup
reports/screenshots, 800x700 through 1600x900, AU/GB/US selected as requested,
no RF streaming. Compact waterfall banner inspected and readable.
CLI TG30003 reference replay: `p25_bandplan_regression.wav`, 506,924 bytes,
byte-identical to `p25_060515_audio_cursor.wav`. This verifies output stability
for that capture, not worldwide plan accuracy or all-call audio acceptance.

GUI TG10120 replay: `p25_bandplan_gui.wav`, 579,884 bytes, byte-identical to
`p25_103841_geometry_gui.wav`; startup/processing/exit completed successfully.
No live RF or new speech-intelligibility claim is made by these regression checks.

## 2026-09-17 - REQ-UI.1 workspace gate

Windows / existing MSVC Release toolchain. Built SDR_Town, sdr_town_tests and
sdr_town_workspace_tests. CTest: both targets pass, 235 core cases / 81,154
assertions plus 3 workspace cases / 44 assertions. Workspace tests exercise
preset transitions without losing edited fields, panel lifecycle, lock/reset,
state persistence and corrupt-state fallback.

Initial GUI test runner requested an undeployed offscreen Qt plugin and hung;
terminated only that test process, switched to installed Windows platform.
An intermediate hidden-panel tab-group assertion failed; fixed grouping to
tabify only visible panels and assert against the visible group. Final tests
pass. `scripts/test_workspace_gui.py`: actual GUI, no RF streaming, no startup
errors across Listening 960x720, Trunking 1280x900, HF 800x700 and Analysis
1600x900. Screenshots visually inspected in `build/workspace_qa`.

P25 CLI 060515 reference: `build/p25_workspace_regression.wav` byte-identical
to `build/p25_060515_audio_cursor.wav` (506924 bytes). Actual GUI 103841 replay:
`build/p25_workspace_gui.wav` byte-identical to `build/p25_103841_geometry_gui.wav`
(579884 bytes), exit 0. These establish fixture non-regression, not universal
P25 audio acceptance. No decoder or timing policy changed. No release published.

## 2026-09-17 - v0.2.55 release gate

Versioned Release rebuilt after gracefully closing the running GUI which had
blocked the linker. Full suite: 81,154 assertions / 235 cases, exit 0
(`build/tests_release_0.2.55.log`); capture-audit self-test passed. Clean deploy,
NSIS installer and portable ZIP generated. Staged --version reports 0.2.55.
Ed25519 manifest signature verified against the unchanged embedded public key;
installer SHA-256 and size match update.json. Portable executable matches the
built binary; Qt platform and control DLL present; no WAV/CF32/log/private-key
or test executable entries. Experimental channel retained for publication.

## 2026-09-17 - Audio cursor race and live callback audit

DEC-0074: Release SDR_Town and sdr_town_tests built successfully. Full suite:
81,154 assertions / 235 cases (`build/tests_audio_cursor.log`). Capture-audit
Python self-tests pass. Reference 060515 replay WAV is byte-identical to
`build/p25_060515_final_geometry.wav` after the downstream change.

Real GUI capture: `build/live_audio_cursor/20260917_072154_033_audio_cursor_420.35000MHz_startstop`.
90.016 seconds, three follows, zero IQ overruns/epoch resets. Callback counter
deltas: 1,741,440 consumed frames; 2,580,960 zero-fill frames; 5,377 empty
callbacks; zero partial callbacks, control-silence frames, or producer drops.
Empty callbacks include normal control-channel/idle silence, not measured lost
speech. TG10703 has a 7.117-second output-event span without an underrun rise.
This is queue continuity evidence, not acoustic intelligibility proof.

Same live IQ replay (TG10703 slot 0, skip 3500 ms, 10 s) yields 378 decoded/fed
frames, six concealment frames, two feed-gap events, 7.56 s PCM. STT contains
recognizable phrases and errors. Continuous clear speech across calls remains
unproven. Details: `docs/P25_DOWNSTREAM_AUDIT_20260917.md`.

## 2026-09-17 - Physical mapping and complete block tails

DEC-0071/72/73: Release rebuilt; full suite 81,149 assertions / 233 cases,
P25 subset 73,045 / 126. Block-tail regression failed at missing burst 12,
then duplicate burst 14, now passes all 24 exactly-once plus noise isolation.
Latest replay concealment 66 -> 3; final 132 decoded frames, 2.64 s submitted
PCM versus 4.96 s including false voice before. Final GUI reference replay
completes with no pending tail. Live 90 s mapping-only test: three follows,
102 outputs, zero IQ overruns, 21 output-underrun increases. STT recognizes
an exchange but is not acoustic continuity proof. See
`P25_MAPPING_AUDIT_20260917.md` for commands, limitations and artifacts.

## 2026-09-17 - Slot ownership and replay EOF drain

Release app/tests built. Full suite: 80,299 assertions, 231 cases pass
(`build/tests_slot_session_final.log`). Original slot helper failed the
explicit-start-IISCH fixture; fixed helper passes clear/encrypted companions.
Reference 103841 replay: 394 -> 398 decoded frames, 400 -> 404 fed frames,
384000 -> 387840 speaker samples; window 2 no longer rejects four stale-TG
frames. PCM changes after the inserted 80 ms (persistent vocoder state), so
do not claim byte-identical audio or verified improved intelligibility.
Latest 060515: still 248 decoded, 250 fed, six gaps, 66 concealment frames.
GUI reference before EOF fix: 382080 pushed, 5760 pending, drain timeout.
After fix: 387840 pushed, zero pending, one 5760-sample drain, normal finish.
CLI/GUI submitted sample counts match; their PCM is not byte-identical.
Artifacts: `build/p25_103841_slot_session*`,
`build/p25_060515_slot_session_final*`. No new live RF test in this follow-up.

## 2026-09-17 - RS recovery timing and audio regression checks

Windows x64 / MSBuild 17.14, Release: `cmake --build build --config Release
--target SDR_Town sdr_town_tests` passed. `sdr_town_tests.exe "[p25]"` passed
68,957 assertions in 122 cases. Capture 060515 eight-second replay:
11.265 s before, 7.562 s after, 4.92 -> 4.96 s submitted PCM, six gaps remain.
GUI replay: 238,080 pushed samples, zero pending/tail drops. Reference 103841:
19.688 -> 11.625 s for 12 s IQ; output WAV SHA256 unchanged. Source regression
checks are supplemental and are not an intelligibility verdict.

Final release: full suite passed 77,061 assertions in 229 cases. Staged CLI
launch exits 0. Installer/portable contents checked for excluded diagnostics
and old binaries; packaged executable matches the tested build. Installer
hash/size match update.json and its Ed25519 signature verifies against the
embedded public key. The build has a non-fatal Qt deploy warning about absent
Direct3D 12 dxcompiler/dxil; no related smoke-test failure was observed.

---

## BN-0057 — DEC-0066 dead-grant timeout (131458) (2026-09-15)

- **Evidence:** 0 emits; unknown-grant ACQ hangs ~45s with no VCW.
- **Fix:** unknown cold no-VCW 8s/6s; WaitingForClearGrant alone no longer
  keeps acquire for 30s; clear cold 10s/7s.
- **Gate:** DEC-0066 verifier PASS; `[p25]` **118/118**; Release rebuilt.

## BN-0056 — DEC-0065 RF-home return (125341) (2026-09-15)

- **Evidence:** return claimed CC without retune while cf still on voice low-IF.
- **Fix:** force retune/warm-standby when RF away from CC; latch RetunedPrimary
  on physical LO leave.
- **Gate:** `verify_p25_phase2_dec0065_rf_home_return.py` PASS; DEC-0064/0063 PASS;
  `[p25]` **118/118**; Release `SDR_Town.exe` rebuilt.
- **Audio note (same capture):** TG10120 had a short clear stretch then drop=A /
  ACQ watchdog; sparse emits are a separate duty track (absDup/feedRatio).

## BN-0055 — DEC-0064 warm-standby return-to-CC (2026-09-15)

- **Evidence:** after bridge follow, return claimed CC while RF on voice → validation
  disable → P25 log stopped.
- **Fix:** pause CC decode/validation in warm-standby; reset validation on real CC
  retune; idle arm requires RF on CC; expire uses returnControlFreqHz fallback.
- **Gate:** `verify_p25_phase2_dec0064_warm_standby_return_cc.py` PASS;
  DEC-0063/0055 PASS; `[p25]` **118/118**; Release `SDR_Town.exe` rebuilt.
- **Residual:** live bridge Monitor-CC → follow → return must show
  `validation armed` / continued CC lines (no `CC disabled` after warm-standby).

## BN-0054 — DEC-0063 idempotent control arm / refuse tune (2026-09-15)

- **Evidence:** FUBAR DLL follow → snap to Monitor CC 420.350 mid-call.
- **Fix:** same-CC arm keeps live follow; analog tune 409 unless force; FUBAR
  Tune status guard; control tune logging + voiceFrequencyHz status.
- **Gate:** `verify_p25_phase2_dec0063_idempotent_control_arm.py` PASS;
  DEC-0055 PASS; dual-slot garble PASS; `[p25]` **118/118**; Release
  `SDR_Town.exe` + `SdrTownControl.dll` rebuilt.
- **Residual:** live follow stick + RID audio need a post-0063 start/stop
  capture (no new keep-set IQ in this pass). FUBAR `sdr_town_bridge.cpp`
  edited; rebuild that app so Tune refuses client-side too (server 409 still
  protects with old FUBAR).

## BN-0053 — DEC-0062 talkspurt vocoder reset (225923) (2026-09-13)

- **Evidence:** same-grant multi-RID; uniqueFreshR≈1 on BAD; no mid-grant mbelib reset.
- **Fix:** MAC_PTT / post-END resets selected vocoder; abs-dedupe kept.
- **Gate:** DEC-0062 verifier; `[p25]`; Release rebuild.

## BN-0052 — DEC-0061 speaker backlog 240+280 (153932) (2026-09-13)

- **Evidence:** post-0060 jitter — WAV island p50=40 ms; bridge top-ups; ctx=80 ms.
- **Fix:** catch-up 240 ms fresh + 280 ms overlap; sustain 80+280 unchanged.
- **Gate:** DEC-0061/0060-supersede/0058/0059 verifiers; `[p25]`; Release rebuild.

## BN-0051 — DEC-0060 speaker backlog 280+80 (152348) (2026-09-13)

- **Evidence:** half-audio pcm_vs_wall≈0.5; dsp p50>160 ms fresh; absDup=context waste.
- **Fix:** catch-up 280 ms fresh + 80 ms overlap; sustain 80+280 unchanged.
- **Gate:** DEC-0060/0058/0059 verifiers PASS; `[p25]` **118/118**; Release rebuilt.

## BN-0050 — DEC-0059 companion ESS / ReturnEncrypted (145139) (2026-09-13)

- **Evidence:** Clear RID aborted mid-emit (`ReturnEncrypted ess=enc`) while
  follow still clear; companion enc opposite slot; pending targetVcw=0.
- **Fix:** this-burst ESS; recent clear clears sticky enc; target-only follow ESS;
  refuse opposite-only pending drain.
- **Gate:** verifier PASS; `[p25]` **118/118**; Release `SDR_Town.exe` rebuilt.

## BN-0049 — DEC-0058 speaker backlog / dual-slot (142104) (2026-09-13)

- **Evidence:** 80 ms fresh / 200–600 ms dsp; waiting-clear islands after Clear latch.
- **Fix:** backlog catch-up 160+280 on speaker path; latched selected-dominant keep.
- **Gate:** verifier erify_p25_phase2_dec0058_speaker_backlog_dual_slot.py; [p25]; Release.

## BN-0048 — DEC-0057 wrong-TDMA companion dwell (`135857`) (2026-09-13)

- **Evidence:** TG30003 CLEAR ~29s; wrong_tdma 47; companion oppVcw during silence.
  Catch: I-ISCH absolute index 10 → grantSlot 1 (final C); lock-rel-only was wrong.
- **Fix:** immutable grant → no wrong-slot brand; keep DEC-0055.3 absolute grantSlot.
- **Gate:** Catch I-ISCH absolute case; verifiers 0055+0057; Release rebuild.

## BN-0047 — DEC-0056 clear hang + WFM default BW (`134135`) (2026-09-12)

- **Evidence:** Clear hang ~57s vs Enc &lt;1s; P25 meta 12.5 kHz LPF off.
- **Fix:** speaker grace needs live traffic; post-speech no-VCW 12/6s;
  lastActive not structure-only after clear speech; WFM default 220 kHz.
- **Gate:** `[p25][follow]` Catch + Release rebuild.
- **Still open:** mid-call clear blocky (feed/budget/worker) — B-0001.

## BN-0046 — DEC-0055 epoch / dual-slot keep / I-ISCH origin (2026-09-12)

- **Evidence:** forensic code audit — DualSlot clear after Clear latch; soft
  epochTrusted garble arm; lock-relative grantSlot without I-ISCH origin.
- **Fix:** keep-selected PCM on dual-slot Clear; tight epochTrusted;
  absolute index rebase when A/B I-ISCH agree (no flip-only).
- **Gate:** Release rebuild; `[p25]` Catch; verifiers incl.
  `verify_p25_phase2_dec0055_epoch_dual_slot_origin.py`.
- **Not proven:** live listen CLEAR multi-second (B-0001).

## BN-0045 — DEC-0054 restore cold full-commit (`081416`) (2026-09-12)

- **Evidence:** post-0053 capture 0.36s SILENT / 1 emit; 061217 had 92s CLEAR.
- **Fix:** drop CQPSK headroom; cold full annotate +200ms; sticky cheap 120ms.
- **Gate:** budget verifier + Release rebuild.

## BN-0044 — DEC-0053 sticky cheap-commit not skip (`064509`) (2026-09-12)

- **Evidence:** post-0052 capture: 2 cold CLEAR emits then permanent no-vcw.
- **Fix:** remove sticky skip-commit; re-arm 50 ms cheap-commit allowance.
- **Gate:** `verify_p25_phase2_budget_skip_sticky_commit.py` + Release rebuild.

## BN-0043 — DEC-0052 sticky skip-commit after budget (`061217`) (2026-09-12)

- **Evidence:** emit p50≈223, busy 690, 0 DEC-0051 trip lines; listen CLEAR.
- **Fix:** skip commit on sticky sustain when deadline gone; cheap cold commit;
  CQPSK headroom; log `P25 budget trip:`.
- **Gate:** verifiers for DEC-0052 + rebuild Release.

## BN-0042 — DEC-0051 cooperative budget abort (2026-09-12)

- **Evidence:** `044651` emit dsp p50≈212 ms, worker-busy 135, rolling→15.9 s;
  wall-timeout 0 (DEC-0046 post-hoc insufficient).
- **Change:** `armRealtimeDecodeBudget` + mid-decode aborts in
  `P25LiveDecoder::processIq` / Phase-2 sync-lock-mask loops; Catch `<350 ms`.
- **Gate:** Release rebuilt; `[p25]` **114/114**; verifiers **133/133**
  (incl. `verify_p25_phase2_cooperative_budget_abort.py`)
- **Operator:** PPM≈−2; one start/stop capture; run listen-bar harvester.

## BN-0041 — DEC-0050 PCM listen classifier (2026-09-12)

- **Why:** Replay duty passes while live sounds bad/silent — need automated
  CLEAR vs GARBLED vs SILENT on speaker PCM, plus live WAV sidecar.
- **Tools:** `p25_pcm_listen_classify.py`, `run_p25_listen_bar_harvester.py`,
  `p25 listenclassify`, forensic wav= + live_listen.
- **Capture:** start/stop writes `*_live_speaker.wav` from speaker-push path.

## BN-0040 — DEC-0049 Auto PPM harden after `044651` (2026-09-12)

- **Evidence:** Auto PPM AFC=1250 conf=0.45 → device ppm −7.88; CC TSBK
  corrections climbed; TG10120 live ok≤0.909 vs TG20202 drop D
- **Change:** reject ±1250 rail; conf≥0.55; step≤1.5; cooldown 120s; trusted
  offset only; full forensic script
- **Result:** `[p25]` 113/113; verifiers 131/131
- **Operator:** set PPM near **−2.0** before next listen (undo −7.88)

## BN-0039 — DEC-0047/0048 logscan + eye-lost streak=1 (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Evidence:** `041612` live A-cliff vs file TG20202 duty 0.805
- **Change:** `p25 logscan`; `kP25LiveEyeLostReplayCandStreak=1`
- **Result:** `[p25]` 112/112; verifiers 131/131; Release rebuilt
- **CLI:** `SDR_Town.exe --cli --cmd "p25 logscan <capture_dir> --audit"`

## BN-0038 — DEC-0046 wall clamp rejected / pending guard (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Trigger:** post-0045 “really bad audio” regression
- **Change:** revert healthy/eye-lost wall clamp; never clear speaker pending
  on decode-wall stamps; keep DEC-0044 auto PPM
- **Result:** `[p25]` **112/112**; verifiers **131/131**; Release rebuilt
- **Not proven:** live CADENCE recovery (operator listen + startstop)

## BN-0037 — DEC-0044/0045 auto PPM + healthy wall (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Trigger:** `032907` — promising then lose-it; emit-gate dsp p50≈451 ms;
  ppm=0 with AFC≈884 Hz
- **Change:** auto PPM on return-to-control; healthy wall 105 / eye-lost 145
- **Result:** `[p25]` 109/109; verifiers **131/131** (new auto-ppm/wall
  verifier). Release `SDR_Town.exe` rebuilt.
- **Not proven:** live CADENCE / `Auto PPM:` log line (operator listen)

## BN-0036 — DEC-0043 twin rescue reverted after `024000` (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Trigger:** live `024000` clear TG30003 @421.975 file duty 0.705 vs live
  max 0.649 / wrong-TDMA / worker-busy
- **Change:** remove ±1 DUID lock-twin rescue; keep post-speak opp-dominant
  invalidate debounce ≥3
- **Result:** `[p25]` 109/109; verifiers 130/130
- **Not proven:** live CADENCE recovery (operator re-listen required)

---

## BN-0035 — DEC-0043 wrong-TDMA sticky debounce (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests "[p25]"` 109 cases; `verify_p25_phase2_*.py` 130/130
- **Voicetest (`020758`):** TG20201 clear slot1 skip=18439 8s
  `PASS_CONTINUOUS_AUDIO duty=0.715`; TG12069
  `PASS_ENCRYPTED_GATED` essEncrypted=yes
- **Missing on disk:** keep-set 060036 / 095846 (only `020758` present)
- **Not proven:** live CADENCE continuity on new GUI follow (B-0001 / T-0010)

---

## BN-0034 — DEC-0041 live eye-lost budget (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests "[p25]"` 109 cases; `verify_p25_phase2_*.py` 129/129
- **Voicetest:** 060036 TG10301 slot0 skip=261000 8s `PASS_CONTINUOUS duty=0.705`;
  095846 TG10301 slot1 skip=68700 `PASS_CONTINUOUS duty=0.8`
- **Not proven:** live CADENCE drop-D cut on new GUI follow (B-0004 / T-0010)

---

## BN-0033 — Add Receiver arms primary DSP (2026-09-11)

- **Host:** Windows 10.0.22631 x64
- **Change:** `MainWindow` Add Receiver now `syncMonitorVarsToReceiver(0)` +
  `setReceiverActive(0, true)` after `startStreaming` (same class as Apply/Scan
  `ecf9303`).
- **File bar:** 060036 TG 10301 skip=261000 block path
  `PASS_CONTINUOUS_AUDIO duty=0.705` (HEAD rebuild).
- **Not proven:** live CADENCE re-prove (T-0010 / B-0001).

---

## BN-0032 — GitHub Actions Windows CI added (2026-09-11)

- **Workflow:** `.github/workflows/windows-ci.yml`
- **Gate:** MSVC Release `SDR_Town` + `sdr_town_tests`, then all
  `verify_p25_phase2_*.py`
- **Deps:** jurplel Qt 6.7.3 + bootstrap vcpkg (manifest) + `external/miniaudio` +
  `external/mbelib` only (skip broken `_codex_refs` gitlinks)
- **CI fix notes:** missing `#include <set>` in `DeviceManager::getAvailableDrivers`
- **Backlog:** `docs/BACKLOG.md` (B-0020)

---

## BN-0031 — ISS-0008…0011 verifier/docs + live pipeline extract (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests.exe` 214 cases / 10194 assertions; `verify_p25_phase2_*.py` 129/129
- **Layout:** `MainWindowP25Orchestration.cpp` ~1.4k (`startP25LiveDecodePipeline`);
  `MainWindow.cpp` ~10.4k; 14 verifiers on `definition_body` anchors
- **Not proven:** live CADENCE re-prove (T-0010)

## BN-0030 — ISS-0004 MainWindowP25Voice TU split (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests.exe` 214 cases / 10194 assertions; `verify_p25_phase2_*.py` 129/129
- **Layout:** `MainWindowP25Voice.cpp` ~1.3k (worker/submit/backpressure/publish);
  `MainWindow.cpp` ~11.8k (ctor/UI remainder); ISS-0010 / ISS-0011 filed
- **Not proven:** live CADENCE re-prove (T-0010)

---

## BN-0029 — ISS-0004 Phase A–B MainWindow out-of-line (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `ctest` UnitTests PASS; `verify_p25_phase2_*.py` 129/129
- **Layout:** `main.cpp` ~200; `MainWindow.h` ~520 (decls); `MainWindow.cpp` ~13k;
  plus `P25VoiceSession` / `P25DecodeConfig` / `DemodModeUtils` / `SavedFrequencies`
- **Not proven:** live CADENCE re-prove (T-0010); further MainWindow ctor/worker TU split

---

## BN-0028 — DEC-0040 / ISS-0004 split `main.cpp` (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests.exe` 214 cases / 10194 assertions; `verify_p25_phase2_*.py` 129/129
- **Layout:** `P25VoiceTiming` / `P25TalkgroupRegistry` / `P25AppGlobals` / `P25RollingIq` /
  `P25VoiceDecode` / `P25VoiceTest` / `CliApp` / `AppBootstrap` / `MainWindow`;
  `main.cpp` ~2k leftovers + entry. Corpus: `src/tools/p25_orchestration_sources.py`
- **Not proven:** live CADENCE re-prove (T-0010); leftover helpers still in `main.cpp`

---

## BN-0027 — DEC-0038 streaming sticky Gardner (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_streaming_cqpsk_lock_create.py` PASS
- **Voicetest 060036 TG 10301 skip=261000 center=421.96375:**
  - Block unset: `PASS_CONTINUOUS duty=0.705`
  - Stream env=1 sticky Gardner: `PASS_PARTIAL duty=0.23`
  - Stream + discrete lock create (rejected): duty **0.12**
- **Not shipped:** default-on streaming (still ≪0.65)

---

## BN-0026 — DEC-0037 restore clear hold, no purge (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_rolling_hold_no_purge.py` PASS
- **Evidence:** 100909 chirp regression (duty 0.40) vs 095846 duty 0.947
- **Not proven:** live CADENCE after GUI reopen

---

## BN-0025 — DEC-0036 no rolling hold when unqueued (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_rolling_no_hold_unqueued.py` PASS
- **Evidence:** 095846 start TG 30302 cursor hold after targetVcw=14;
  later TG 10301 max duty 0.947
- **Not proven:** live start-unknown follow after GUI reopen

---

## BN-0024 — DEC-0035 live eye-lost uses replay CQPSK caps (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_live_eyelost_replay_caps.py` PASS
- **Evidence:**
  - Live 094846: drop A 60/62; max duty 0.338 (DEC-0034 exe)
  - Same IQ voicetest TG 30302: duty 0.43 targetVcw=652
  - Live/replay split = hot cand 8 vs 16 after speak
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0023 — DEC-0034 keep block CQPSK hint after emit (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_post_emit_keep_block_cqpsk_hint.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS (186 assertions / 49 cases)
- **Voicetest 060036 TG 10301 skip=261000 center=421.96375:**
  `PASS_CONTINUOUS_AUDIO duty=0.705` (held)
- **Evidence:** 092250 post-emit `clearBlockCqpskHint` cliff; companion-only
  gated to streaming
- **Not proven:** live CADENCE on new exe (T-0010). Default-on streaming still off.

---

## BN-0022 — DEC-0033 sticky HDQPSK / persistent framer (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_streaming_framer_commit.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS (186 assertions / 49 cases)
- **Voicetest 060036 TG 10301 skip=261000 center=421.96375:**
  - Block (env unset): `PASS_CONTINUOUS_AUDIO duty=0.705`
  - Stream `SDR_TOWN_P25_STREAMING_DDC=1`: `PASS_PARTIAL_AUDIO drop=D duty=0.25`
    (improved vs DEC-0014/0018 ~0.09–0.16 class; still ≪0.65)
- **Evidence:** 083254 extract cliff; framer Cold-gated + companion sticky
- **Not proven:** env=1 duty≥0.65; 105622 (IQ absent); live CADENCE (T-0010).
  Default-on still off (DEC-0014).

---

## BN-0021 — DEC-0032 post-emit sustain before catch-up (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_post_emit_sustain_before_catchup.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS
- **Voicetest:** 060036 TG 10301 skip≈261000: `PASS_CONTINUOUS_AUDIO duty=0.705` (held)
- **Evidence:** 081701 post-emit fresh=120 ms → no voice sync hang
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0020 — DEC-0031 backlog catch-up + once-clear continuation (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_backlog_catchup_before_speaker_sustain.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS
- **Voicetest:** 062006 TG 30003 slot1 skip=0: duty **0.46** (unchanged; DEC-0012
  companion-louder holes). 060036 TG 10301 skip≈261000: duty **0.705** (held).
- **Evidence:** planner ignored backlogCatchUp after speak; security
  requireFedAudio chicken-egg vs dual-slot mute
- **Not proven:** live CADENCE after GUI reopen (T-0010); soft PostEmitMixedMacDead

---

## BN-0019 — DEC-0030 active rolling 4s clamp (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_active_rolling_4s_clamp.py` PASS
- **Voicetest:** 060036 TG 10301 skip≈261000 center=421.96375:
  `PASS_CONTINUOUS_AUDIO duty=0.705` (proves live starve, not RF)
- **Evidence:** live rolling capped 4194304 after emit; file continuous
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0018 — DEC-0029 clear-trusted hold + structure cold-exit (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `sdr_town_tests "[p25][follow]"` 49 cases / 186 assertions;
  `verify_p25_phase2_clear_trusted_hold_and_structure_cold_exit.py` PASS;
  `verify_p25_phase2_no_post_emit_cold_escalate.py` PASS
- **Voicetest:** pending (re-run 105622 skip=97334 after GUI live prove)
- **Evidence:** 053448 quiet-return +5s after clear emit; structureNoVcw ~484 ms
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0017 — DEC-0028 no post-emit cold escalate (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_no_post_emit_cold_escalate.py` PASS
- **Voicetest:** 105622 skip=97334: duty **0.645** (unchanged; PASS_PARTIAL drop=D)
- **Evidence:** 115603 first emit 0.553 then dsp 470–605 ms; emptyStreakReacq removed
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0016 — DEC-0027 emptyEye-only cold escalate (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_empty_eye_only_cold_escalate.py` PASS;
  `verify_p25_phase2_opposite_slot_no_cold_escalate.py` PASS
- **Voicetest:**
  - 105622 skip=97334: duty **0.645** (unchanged; PASS_PARTIAL drop=D)
  - 112922 TG 30302 slot 0 skip=164000 center=421.21375: `PASS_CONTINUOUS duty=0.735`
- **Evidence:** 112922 structureNoVcw dsp med ~462 ms; CADENCE peak 0.639 drop D;
  file continuous proves live starvation not RF
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0015 — DEC-0026 opposite-slot no cold escalate (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_opposite_slot_no_cold_escalate.py` PASS
- **Voicetest:** 105622 skip=97334: duty **0.645** (unchanged)
- **Evidence:** 110146 wrong-slot dsp p90 ~434 ms; 20202 file 0.85 / live 5 ok
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0014 — DEC-0025 Clear→Encrypted MAC bar (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_clear_to_encrypted_mac_bar.py` PASS
- **Voicetest:**
  - 105622 skip=97334: duty **0.645** (unchanged)
  - 103955 RID 0x1F83FF skip=98700: duty **0.46** PARTIAL (RF-limited)
  - 103955 RID 0x1F95EB skip=119800: duty **0.83** CONTINUOUS
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0013 — DEC-0024 backlog catch-up overlap 280 ms (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_backlog_catchup_overlap.py` PASS
- **Voicetest:** 105622 skip=97334: duty **0.645** (unchanged)
- **Evidence:** 101644 first-call `context=81920` spiral; late 10330 280 ms
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0012 — DEC-0023 rolling protect 280 ms (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_rolling_protect_overlap.py` PASS
- **Voicetest:**
  - 105622 skip=97334: duty **0.645** (unchanged)
  - 095936 TG 30302 slot 0 skip=11000 center=421.21375:
    `PASS_CONTINUOUS_AUDIO duty=0.685`
- **Not proven:** live CADENCE after GUI reopen on new desktop exe (T-0010)

---

## BN-0011 — Live speed trials rejected; hard hint stop only (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile after reverts
- **Voicetest (streaming DDC unset, block 80+280):**
  - 105622 skip=97334: duty **0.645**, wall **~17 s** / 8 s span
- **Rejected (duty collapse):**
  - DEC-0020 80/0 after emit: wall 2.8 s, 105622 duty **0.055** drop=A
  - Hot cand=3 after speak (live proxy): wall ~7 s, duty **0.055** drop=A
  - Streaming env=1 @ 80 ms: duty ~0.01; @ 160 ms (DEC-0022): **0.125**
- **Kept:** DEC-0019 hard CQPSK hint early-stop; live hot cand=**8**
- **Not proven:** live CADENCE on desktop HEAD (operator still on 0.2.51)

---

## BN-0010 — DEC-0019 hard CQPSK hint stop (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_block_cqpsk_hint_early_stop.py` PASS
- **Voicetest (streaming DDC unset):**
  - Soft early-stop trial: 105622 duty=0.305 / 041716=0.5 — **rejected**
  - Hard-only (cand still 8/16): 105622 **0.645**, 073304 **0.795**, 041716
    **0.87**, 060221 peak **0.922** (parity with 0.2.51)
  - Later: cand=3 after speak **rejected** (see BN-0011)
- **Not proven:** live CADENCE drop D after GUI reopen (T-0010)

---

## BN-0009 — 053241 context-only DEC-0012 trial rejected (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Trial:** PostEmit skip only when `codewordEndsBeforeFresh` (fresh selected
  still feeds on companion-louder mixed MAC-dead).
- **Voicetest (file `--center`, streaming DDC unset):**
  - 053241 TG 30003 slot 0 skip=0 center=421.96375: duty=0.715 continuous;
    companion-louder `fed>0` returned.
  - 105622 TG 30003 slot 0 skip=97334: duty=**0.62** (two reruns; was 0.645).
  - 073304 TG 10330 slot 1 skip=107597: duty=**0.72** (was 0.795).
  - 041716 TG 10330 slot 1 skip=32111: duty=0.875 but **0**
    `unknown-waiting-clear`; 8 companion-louder fresh emits (isolation regress).
- **Action:** reverted to hop-wide DEC-0012 (v0.2.51). No release bump.

---

## BN-0008 — DEC-0012 companion-louder mixed MAC-dead skip (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile; dual-slot / session-release / sticky-ESS string
  verifiers PASS
- **Voicetest (file `--center`, streaming DDC unset):**
  - 041716 TG 10330 slot 1 skip=32111 center=421.21375:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.87`. Companion-louder mixed
    `p2mac=0` hops `unknown-waiting-clear` (seq=134 class).
  - 073304 TG 10330 slot 1 skip=107597 center=420.975:
    `PASS_CONTINUOUS_AUDIO duty=0.795`
  - 105622 TG 30003 slot 0 skip=97334 center=421.725:
    `PASS_PARTIAL_AUDIO drop=D duty=0.645` (was 0.685). Slot 1 duty=0.09.
- **Not proven:** live CADENCE on a new GUI follow (T-0010 drop D).

---

## BN-0007 — DEC-0017 revert + DEC-0018 streaming lattice (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile
- **Voicetest 105622 TG 30003 slot 0 skip=97334 center=421.725:**
  - Default block 80+280: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685` (DEC-0017 360/0 had 0.35)
  - `SDR_TOWN_P25_STREAMING_DDC=1`: duty 0.16 (80 ms), 0.09 (hopms=160), 0.045 (hopms=360)
- **Not proven:** live CADENCE vs 123525 duty 0.34. Streaming DDC stays opt-in.

---

## BN-0006 — DEC-0016 voice-park follow LO (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile; `sdr_town_tests.exe "[tune]"` 18/5; one-RTL verifier PASS
- **Voicetest:** 105622 TG 30003 slot 0 skip=97334 `PASS_CONTINUOUS_AUDIO duty=0.685`
- **Not proven:** live listen on 115315-style 1.5 MHz grant (T-0010). File IQ was
  recorded at 420.97773 so replay cannot un-do the 997 kHz edge.

## BN-0005 — DEC-0015 SDRTrunk tuner center (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile (`main.cpp` rebuilt); `sdr_town_tests.exe "[tune][dec0015]"` 14/4; one-RTL / force-retune / inband string verifiers PASS
- **Voicetest (file `--center` geometry, not live LO):**
  - 105622 TG 30003 slot 0 skip=97334: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - 073304 TG 10330 slot 1 skip=107597 center=420.975: `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`
- **Not proven:** live waterfall on a follow (close/reopen `build\bin\Release\SDR_Town.exe`). Expect log `P25 tuner center aligned (SDRTrunk CenterFrequencyCalculator)` and voice ~11 kHz right of center, not 250 kHz off.

## BN-0004 — DEC-0014 CADENCE + 80 ms stream slice; default DDC rejected (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile; streaming DSP verifier PASS; `[drop]` 14/8
- **Voicetest:**
  - Default-on streaming DDC: 105622 TG 30003 slot 0 skip=97334
    `PASS_PARTIAL_AUDIO drop=A duty=0.095` — reverted
  - After revert: 105622 `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - 073304 TG 10330 slot 1 skip=107597 center=420.975:
    `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`
  - 095450 TG 30013 slot 1 skip=3741: `PASS_PARTIAL_AUDIO duty=0.625`
    `oppAmbe=759/920 concealmentOk=no`; companion slot 0 duty=0.58
- **Not proven:** live CADENCE listen on 095450-style dual-TG (T-0010)

## BN-0003 — DEC-0013 lattice overlap de-dupe (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS
  - Verifiers: fresh/context, overlap de-dupe, dual-slot garble
  - Voicetest 105622 TG 30003 slot 0 skip=97334 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - Voicetest 073304 TG 10330 slot 1 skip=107597 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.76 timelineOk`
- **Not proven:** live CADENCE after this binary (T-0010). Mixed MAC-dead
  garble on 073304 first 30302 / last dual-TG concealment (T-0004).

## BN-0002 — DEC-0008/0009 Phase 2 extract (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target sdr_town_tests SDR_Town`
- **Result:** PASS
  - `sdr_town_tests.exe "[drop]"` — 14 assertions / 8 cases
  - Verifiers: dual-slot garble, block-channelize continuity, fresh/context
    gate, overlap decode window, speaker sustain overlap
  - Voicetest 105622 TG 30003 slot 0 skip=97334 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735`
  - Same IQ slot=1: `PASS_PARTIAL_AUDIO duty=0.11` (isolation)
- **Not proven:** live CADENCE on 161748 after this binary (T-0010)

## BN-0001 — Method desk + drop classifier (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` 19.44.35227 for x64 (VS 2022 Community), SDK 10.0.26100.0
- **CMake:** Visual Studio 17 2022 / x64, config Release, `BUILD_TESTS=ON`, `SDR_TOWN_ENABLE_MBELIB=ON`
- **Command:** `cmake --build build --config Release --target sdr_town_tests SDR_Town`
- **Result:** PASS
  - `sdr_town_tests.exe "[drop]"` — 14 assertions / 8 cases
  - `sdr_town_tests.exe` — 10166 assertions / 206 cases
  - `build/bin/Release/SDR_Town.exe` linked (main.cpp compiled with `P25AudioDropClass`)
- **Not proven:** live or IQ `drop=` on a clear Phase 2 call (ISS-0001 / ISS-0007)
