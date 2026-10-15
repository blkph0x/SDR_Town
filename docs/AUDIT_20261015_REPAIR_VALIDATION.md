# Audit Repair Validation - 2026-10-15

Integrated evidence ledger for [DEC-0223](DECISIONS.md) and the findings in
[the system and UX audit](AUDIT_20261015_SYSTEM_AND_UX.md).

**Status: repair validation and public-package checks completed within the limits below.**
The common native-cleanup repair is included in the identified 0.2.139 build.
All 23 CTest suites, the final P25 comparison and the 180-second Town/FUBAR pair
pass within the limits below. No concrete known compile/test defect remains in
the completed checks. Unmake exceptions were source-reviewed, not fault-injected.
Public-package evidence is recorded separately from the local build below.
This is not a claim that
every UI, physical workflow, or source branch has been tested. Canonical issue,
task, version and release trackers remain separately maintained by the parent.

## Integrated Acceptance

This table summarizes the identified final local candidate. Earlier native
workflow and CLI evidence remains explicitly labeled pre-close; it is not
silently attributed to a later executable. Physical qualification limits
remain separate from completed local tests.

| Gate | Final-Candidate Status | Latest Evidence and Remaining Gate |
|---|---|---|
| Integrated native build | **PASS** | Common-cleanup repair included; `build/audit_20261015_release_build.log`, exit 0. Version and hashes below. |
| Relevant native unit, lifecycle and GUI suites | **PASS, with explicit skips** | Final CTest **23/23 pass, 147.34 s**. Focused cleanup **142 assertions / eight scenarios**; integrated core also passes. Counts/skips below. |
| Exact-patch protection | **PASS** | Seven DEC-0223 files accepted by the exact-patch guard; guard self-test and negative mutations pass in `build/audit_20261015_guard_final.log`. |
| Control transport and snapshot routes | **PASS within tested routes** | DLL **33 fixtures pass** in the pre-close run. Final RemoteDiagnostics/AircraftGui suites include paging/range/catalog tests; the final authenticated paired application run passes. |
| RDS and DTMF CLI regressions | **PRE-CLOSE PASS** | Both CLI gates pass before common cleanup; relevant UI/DSP unchanged in the cleanup follow-up. Not represented as a post-link CLI rerun. |
| P25 baseline comparison | **PASS, bounded comparison** | Eight final runs: exact clear WAV, both encrypted fixtures muted, suspect fixture zero PCM. Suspect work counters differ; no blanket bit-exact claim. |
| Town/FUBAR paired workflow | **PASS, 180 seconds** | Town 0.2.139 / FUBAR 1.1.51; authenticated dry-run/API/browser checks pass, hashes unchanged, both normal exits 0 and exact PIDs confirmed stopped. No RF. |
| Native application workflow checks | **PRE-CLOSE PASS; final compiled GUI PASS** | Four workspace profiles and actual SSTV/Inmarsat/Satcom/Aircraft sessions pass on the pre-close app. Relevant UI/DSP unchanged by common cleanup. Earlier 1280 x 832 observations and their limits remain recorded. |
| Native FUBAR GUI controls | **UNQUALIFIED** | No safe same-profile preview: the original GUI can autostart capture, prune data and save settings. The qualified pair uses FUBAR CLI web-only mode, actual Town GUI and the production browser, not every native FUBAR button. |
| Release-verifier preflight | **LOCAL PASS** | All 15 CI-listed verifier scripts: **139 tests pass**; YAML/command checks pass. Python 3.10.10 locally versus 3.12 in Actions; hosted CI and actual artifact qualification remain separate. |
| Actions and distributed artifacts | **PASS for identified public packages** | Town 0.2.139 and FUBAR 1.1.52 source/release Actions, public hashes/provenance, signed Town updater manifest and downloaded package tests pass. See Public Package Evidence. |

## Public Package Evidence

Town release Actions38724109668 and master38724105582 pass for
a45b2006db79a17ceff1966caca5131806b793f6. FUBAR release38733569638 and main/tag
builds38733568815/38733569167 pass for2b419ec61c538b6693aa11b6efd178dff14df5f9.
Exact public hashes and paths are in BUILD_NOTES.md; the Town manifest uses the
existing Ed25519 key and experimental channel. No signing key was uploaded.

Public Town checks pass:144 inventory files/zero blockers, provenance and asset
hashes, version/help, RDS/DTMF, isolated RTL-loader,33 DLL transport fault cases
and four actual GUI profiles without RX. Public-to-public P25 eight-run matrix
matches the accepted clear WAV exactly; encrypted and suspect cases emit no PCM.
Evidence: D:/SDRTown-ReleaseQA/0.2.139-run38724109668-monitor.

Public FUBAR ZIP and84 corresponding-source files match their provenance. Its
DLL matches Town's public DLL exactly. Downloaded negative/browser tests and
180-second actual paired run pass with both normal exits0. Evidence:
D:/FUBAR-ReleaseQA/1.1.52/public-run38733569638. These are web-only FUBAR plus
actual Town GUI tests, not native FUBAR all-button acceptance or live RF.
The downloaded automatic-pairing run also passes with exits0; all exact test
processes have stopped. Both public Latest APIs and Town's signed Latest updater
are verified after promotion. No installed application was replaced by these tests.

The initial companion1.1.51 candidate failed hosted browser qualification before
binary publication. Old map results could replace newer errors; independent
probes also reproduced stale Inmarsat status/messages across A-B-A selection.
1.1.52 carries one request/session predicate through the complete pipeline and
guards errors and continuation pages. Reversed-response unit and real-DLL browser
tests pass. The unsuccessful source tag is retained, not overwritten. This later
repair supersedes local1.1.51 browser results for final delivery qualification.

## Evidence Provenance

Focused aircraft/store/map tests, updater execution, DLL execution, CI checks and
the 15-script release-verifier preflight were run by the report maintainer.
Audio/FM, diagnostics/consent and companion
results come from the respective repair agents' inspected execution reports. The
main-application UI, build exit status, 33-case DLL and CLI results come from the
parent. Pre-close and final build/CTest logs, current detailed test counts and
the guard log were inspected; the final executable/DLL hashes were independently
rechecked. Focused common-cleanup results are from Carver's final run via the parent.
Final P25, pair result and pair provenance JSON were inspected; both saved clear
WAV hashes were independently rechecked. These different sources are attributed here
once rather than repeated in each matrix row.

Counts describe individual runs, not aggregate line/branch coverage; overlapping
suites must not be added as unique coverage. Source review is not runtime proof,
and a passing fixture is not physical acceptance. No transmitter, rotor,
bias-power or remote-sharing authorization is implied by these checks.

## Final Build and Native Tests

Candidate: **SDR Town 0.2.139**, including the common cleanup helper for normal
stop, RX-fault and cancelled-opener retirement. Build exit 0 is recorded in
`build/audit_20261015_release_build.log`. Independently checked SHA256 identities:

- `SDR_Town.exe`: `7563FA005F0ACBE9BDA9DD9BEFE9F13886E96FB5E3A028FE7ED8AB6728DA23B9`.
- `SdrTownControl.dll`: `DF2A593AA8EE47889570DDD9184DA984E6241F7375BF7BAD54D049EC03520F14`.

`build/audit_20261015_release_ctest.log`: **23/23 suites PASS, 147.34 seconds**.
Detailed counts were read from the corresponding `build/Testing/Temporary/LastTest.log`:

| Suite | Passed Cases | Skipped Cases | Passed Assertions |
|---|---:|---:|---:|
| UnitTests | 584 of 586 | 2 | 251,712 |
| WorkspaceTests | 53 of 59 | 6 | 1,083 |
| SdrplayControlLifecycle | 1 | 0 | 72 |
| RtlBiasTLifecycle | 1 | 0 | 51 |
| WorkflowRadioLifecycle | 1 | 0 | 148 |
| InmarsatLiveGui | 30 | 0 | 591 |
| AircraftGui | 13 | 0 | 192 |
| SatelliteSstvGui | 2 | 0 | 26 |
| InmarsatHostLifecycle | 2 | 0 | 64 |
| FrontendStation | 25 | 0 | 471 |
| AntennaControl | 8 | 0 | 60 |
| RemoteDiagnostics | 31 | 0 | 493 |
| UpdateManager | 12 | 0 | 278 |

The other passing CTest entries are three HF/listen-selection/spectrum
regressions, five SDRplay loader cases and two SSTV transport/backend checks.
These have separate pass results, not additional Catch assertion counts.
The new message-range/catalog cases are compiled into `inmarsat_live_gui_tests`
and selected by the passing `AircraftGui` filter, despite their Inmarsat-message
coverage. Native control-server tests are included in RemoteDiagnostics.

The eight skipped cases are explicit prerequisites, not passes: two core SSTV
independent-recording adapters; workspace alias database/import probes, SSTV live
recording/stream-helper/GUI recording fixtures, and the optional independent JAERO
burst-IQ fixture. Separate actual-app session passes do not imply these optional
fixtures ran. Suite/case/assertion totals are not line or branch coverage.

Common-cleanup negative paths passed **142 assertions across eight scenarios** in
the isolated final check and are also covered by the passing integrated core run.
**Unmake exceptions were source-reviewed, not fault-injected.** Physical driver
failure/recovery remains outside this fixture qualification.

`build/audit_20261015_guard_final.log` records exact DEC-0223 acceptance for
`include/AudioEngine.h`, `include/Demod.h`, `include/DeviceManager.h`,
`src/AudioEngine.cpp`, `src/Demod.cpp`, `src/DeviceManager.cpp` and
`src/MainWindow.cpp`. Guard self-tests and negative mutations pass. Expected
`ERROR` lines from rejected mutation fixtures are not failures of the accepted
candidate. The separate final P25 replay matrix is recorded below.

## Focused Repair Evidence

Focused runs establish each repair's regression behavior; final integrated
results above confirm the combined candidate. Their counts overlap and are not
an aggregate coverage measurement.

| Finding / Surface | Implemented Repair or Current Work | Evidence | Qualification Limit |
|---|---|---|---|
| F01 / F02: RX lifetime and activation | Session/generation isolation, post-driver publication checks, activation failure handling, stable-device pending-open identity and common native cleanup/quarantine. | Final cleanup **142 assertions / eight scenarios** and integrated core/lifecycle suites pass, including the follow-up normal-stop/RX-fault/cancelled-opener paths. | Unmake exceptions are source-reviewed, not fault-injected. Fake-driver and stopped-state checks are not physical recovery acceptance. |
| F03: audio trim ownership | Emergency trim acquires the production callback's consumer lease; callback remains nonblocking. Test-only barriers exercise snapshot, trim, push and wraparound. | Combined isolated audio/FM run: **48 cases / 8,776 assertions**, plus **100 overlap repetitions**. Production audio object has no test-hook symbols. | Counts include F04. Physical playback, prolonged contention and sanitizer qualification remain untested. |
| F04: non-finite FM input | Reject damaged IQ/configuration, mark a multiplex discontinuity, reset history and recover on clean input; separate fault diagnostics. | Focused and final core runs pass. **16 valid PCM/multiplex signatures match baseline exactly.** Pre-close RDS/DTMF CLI gates pass; final P25 clear WAV matches baseline. | No new on-air FM or audio-device acceptance. Original failing probe retained in run history. |
| F05: truthful native start results | Failed Tune and Inmarsat Take Over show rejection; stopped state and lease cleanup are visible. | Actual application UI at **1280 x 832**, detailed below. Earlier integration run: WorkspaceTests, InmarsatLiveGui and UpdateManager **3/3 pass**, total 25.85 seconds. | No-RF failure-path evidence, not successful radio activation or complete workspace coverage. |
| F06: native control transport | Bounded framing/body handling, immutable snapshot pages, source-range validation, compact session catalogs and bounded mutation replies. | DLL **33 fixtures pass**, expanding the earlier 10-case run. Companion loopback: 512 Unicode-rich rows, fragmentation, oversize/truncation, paging and auth/privacy pass. Final message/catalog and server fixtures plus actual pair pass. | Deadline evidence is receive-only, not a DNS/connect/send end-to-end bound. Tested routes and row budgets do not establish arbitrary-load acceptance. |
| F07: companion admission | Bounded queue/status requests, random tickets, owner expiry, limited extensions and bounded/redacted errors. | Admission tests: 128 IDs and 80 concurrent admissions, expiry/promotion/two extensions and auth/privacy pass. Control CTest **2/2 pass**, including strict top-level `ok` and error codes; existing companion CTest **4/4 pass**. | Ticket bounds do not establish unique-human admission or resistance to every abusive workload. Fixture and final paired-binary tests remain distinct. |
| F08: aircraft field age and provenance | Independent presence, identity, position, altitude and motion observations; monotonic aging; freshest-field source selection; unknown values and network-off behavior preserved. | Actual magnitude/frame/store path with deterministic clock: **32 cases / 378 assertions pass**. Cold/missing/zero fields, identity-only traffic, network preference/off, ADS-C, independent aging, bad CRC/CPR and UTC jumps covered. | Local altitude decoding remains deliberately unimplemented without an independently verified reference fixture. Existing retention policy is preserved. |
| F08: aircraft native map and API values | Explicit unknown labels; measured zero preserved; finite/valid coordinate and heading checks; shared nullable serializer with field provenance. | Isolated Qt map **4 cases / 92 assertions pass**; final AircraftGui **13 cases / 192 assertions pass**, including control views. Popup clicks/screenshots check unknown/zero, heading/source and stale/network-off behavior. Actual app rendered OSM tiles and 33 aged network tracks; final paired named-aircraft views pass. | Live RF/network recovery remains unqualified. One tile-load observation is not all external-asset qualification. |
| F09: updater resource/lifetime bounds | Async bounded metadata, streamed temporary installer with mandatory Ed25519 and exact signed size/hash, deadlines, redirects, single flight and safe cancellation/teardown. | Isolated and final integrated updater: **12 cases / 278 assertions pass**. Fake replies cover signatures, oversize/truncation/hash/write/launch failures, redirects, stalls/trickles and cancellation/destruction. | No external installer download, execution, successful installation/rollback or final packaged updater acceptance. |
| F10: diagnostics consent | ThisRun consent overrides saved opt-out without changing persistent/recording consent; forced-off still wins. | Focused diagnostics **14 cases / 183 assertions pass**. Actual Yes/No/unavailable dialog with fake transport: **27 assertions pass**. Final WorkspaceTests and RemoteDiagnostics pass. | Consent/activation proof, not telemetry delivery. No external uploads; dry-run startup does not grant consent. |
| Companion browser workflows | Bounded session/map assembly and ticket recovery; drafts survive first poll/task switching; late status does not overwrite edits. | Final 180-second pair: eight tasks at **360, 390 and 1280 px**, drafts, sessions, ticket reload/release pass; both normal exits 0. | Specific surfaces/viewports only; external assets and physical radios are not universally qualified. |
| P25 shared-boundary replay | Accepted capture/profile comparison with public 0.2.138; no P25 DSP changes. | Final eight runs/four fixtures; exact clear WAV match, both encrypted fixtures gated, suspect fixture fail-closed. Identities/results below. | Suspect work counters differ, so the matrix is not wholly bit-exact. No new listening acceptance is inferred. |
| Windows CI | Explicit updater/DLL build targets, exact `UpdateManager` CTest with missing-test failure, and built-DLL fault harness. | YAML, target names, regex, DLL path and failure propagation pass. `--help` confirms `--dll DLL`. All **15 release-verifier scripts / 139 tests pass**, detailed below. | Local tests/configuration checks do not establish a green Actions run or releasable artifact. |

## Native UI Check

Pre-final-link main application, inspected by the parent at 1280 x 832:

- Main Tune in dry-run displayed **Radio could not start**; Device Manager reported
  **stopped**, identified the available RTL receiver and showed the absent SDRplay
  service. This is useful failure-path evidence, not RSP reception evidence.
- Navigated Listen, Satellite/Aircraft and Inmarsat workspaces.
- Aircraft tab rendered OSM tiles and reported **33 network tracks with age**.
  No RF input or 1090 MHz decoding was exercised by that observation.
- Inmarsat Start/Take Over rejected unavailable RF with a modal error. The log
  confirmed lease cleanup. A brief bandwidth display of 5 returned to 180 on the
  next update; the parent observed no persistent issue from that transition.
- SSTV menu opened a modeless dialog at **145.8 MHz / Auto**. Decode with an empty
  recording produced meaningful validation without hanging. The SSTV dialog and
  main application then closed normally. Parent confirmed **exit code 0** and log
  entries for **all streams stopped** and **audio engine destroyed**. This is a
  native-app normal-close check, not the interrupted paired-run shutdown result.
  Source/Mode controls and physical satellite reception were not manually tested.
- Dry-run used a temporary INI and Qt test-mode standard paths. No real diagnostics
  opt-in was made. Screen captures were inspected through the UI tool only and
  **were not saved**; do not cite an archival screenshot for this check.

This check exercises named controls and rejection behavior only. It does not prove
successful receive/start, every workspace action, all display sizes, accessibility,
long-duration responsiveness or physical device recovery.

## Aircraft Contract and Evidence

`AdsBTrackStore::trackJson` is the common list/detail serialization contract.
Invalid `lat`, `lon`, `altFt`, `gsKt`, `trackDeg` and `vrateFpm` are JSON null,
not zero, strings or non-finite JSON numbers. Validity flags describe their own
fields. `altitudeReference` distinguishes an observed barometric/geometric value.
`fields` contains presence, identity, position, altitude, groundSpeed, heading,
verticalRate and squawk observation metadata: source, unixTime, ageSec and
timeBasis; never-observed metadata is null. Monotonic implementation times are
not wire timestamps. MainWindow's detail route now uses this helper; final
core/GUI and paired named-view checks pass within the route scopes recorded here.

Local ADS-B, ADS-C and OpenSky are distinct sources. Legacy source flags describe
the selected position source, falling back to presence if there is no valid
position. OpenSky position uses `time_position`; fields without their own source
timestamp identify their approximate `last_contact` basis. Unchanged network
telemetry retains its earlier observation time instead of renewing indefinitely.
Missing local altitude is intentionally unknown, not a fabricated decoder result.

Local, non-release evidence artifacts (not guaranteed portable or permanent):

- Core executable: `D:/sdr-aircraft-test-93d2c4cb12004393a1d98bdfbacafccc/sdr-aircraft-freshness-82b8fccb7c0d4c9eb4b3c27bf39cf81b/aircraft_freshness.exe`.
- Qt executable: `D:/sdr-aircraft-ui-a713f426b2e94ddfab19a53af517d9f8/aircraft_ui.exe`.
- Its `visuals` folder contains `aircraft-missing-details.png`,
  `aircraft-zero-details.png`, heading north/east, fresh-network and 560/1100 px
  renders. These are isolated native widget fixtures, not screenshots of every
  main-application workflow. The separate main-application OSM/SSTV observations
  above have no saved screenshot artifacts.
- Audio/FM logs were recorded at `build/audio_fm_dec0223/focused.log` and
  `build/audio_fm_dec0223/broader.log`; isolated updater temporary compiler outputs
  were removed after their reported runs.

## P25 Paired Replay Evidence

Final results are in
`build/audit_20261015_p25_final_0.2.139_215443/results.json`. The JSON and both
saved clear WAV hashes were independently checked without rerunning playback
or launching another build. All eight runs completed with exit 0 against public
0.2.138 and final 0.2.139, using capture013124 and the accepted 720 ms window /
208 ms hop real-time profile. The final app hash matches the build and pair
provenance; offline radio enumeration was skipped in these replay runs.

| Fixture | Both Executables' Outcome | Comparison Limit |
|---|---|---|
| Clear TG10235, skip 24000 ms / duration 5000 ms | `PASS_CONTINUOUS_AUDIO`; 186 decoded frames, 178,560 audio/speaker samples, 3.72 seconds audio, duty 0.744. Saved WAVs are each 714,284 bytes with identical SHA256. | Exact saved-audio match on the final local build for this fixture; no new listening acceptance. |
| Encrypted TG12068, skip 233600 ms / duration 900 ms | `PASS_ENCRYPTED_GATED`; zero audio/speaker samples and no WAV. | Gating result and recorded summary/continuity counters match for this fixture. |
| Encrypted TG12068, skip 446200 ms / duration 900 ms | `PASS_ENCRYPTED_GATED`; zero audio/speaker samples and no WAV. | Gating result and recorded summary/continuity counters match for this fixture. |
| Suspect TG12068, skip 100800 ms / duration 12000 ms | `FAIL_NO_AUDIO`; zero audio/speaker samples, no WAV, zero trusted-clear/session windows. | Expected fail-closed outcome preserved. Public/final work counters differ: bursts 1206/1218, VCW 2924/2950, opposite pending 1352/1374 and mask 1138/1148. Target VCW is 1174 in both. This is not bit-exact behavior. |

Recorded identities:

- Public executable SHA256: `423C3FEDC658DB1326D52C70B29F04C1CD8BB9BB1D247AA5F7173C825C86F825`.
- Final 0.2.139 executable SHA256: `7563FA005F0ACBE9BDA9DD9BEFE9F13886E96FB5E3A028FE7ED8AB6728DA23B9`, modified `2026-10-15T10:51:48.7001233Z`.
- Both clear WAV SHA256 values: `4A890AFAA12FA54B8BDEB21EC8FCB9F943445DBDA186EA6E607BCB2A0984F170`.

The earlier `build/audit_20261015_p25_qualification_212245/results.json` run used
working executable `94D05A5F36557039A1E119F3F9588578490DDA5A0E3794C453A1AE895335C30B`.
It is retained as superseded pre-final-link evidence, not the candidate identity.
Its clear WAV was also identical, while its suspect work counters differed
(public/working bursts 1218/1206, VCW 2950/2910 and target VCW 1174/1154).
A later published artifact does not inherit local replay provenance automatically.
Private captures/audio were not uploaded by these checks.

## Final Paired Application Run

**PASS: Town 0.2.139 / FUBAR 1.1.51 for 180 seconds**, using authenticated
Town GUI dry-run, FUBAR CLI web-only mode and the production browser. No RF.
Evidence directory: `D:/FUBAR-qa/20261015-v1.1.51-town0.2.139-final-pair`.
The inspected `result.json` and `provenance.json` record:

- Start `2026-10-15T21:53:08.0619208+11:00`; verification
  `2026-10-15T21:56:24.7879164+11:00`; configured duration 180 seconds.
- Functional/API checks pass for both `pair_a` and `pair_b`: Satcom status,
  Inmarsat map/messages and Aircraft status, eight addressed views in total.
- Production browser checks pass for all eight tasks at 360/390/1280 px,
  including protected drafts, session lists and ticket reload/release.
  `browser.log` confirms no page errors or horizontal overflow.
- Town PID **51900** and FUBAR PID **13624** both exit normally with code **0**;
  exact process IDs are confirmed stopped. Binary hashes are unchanged after run.
- Town and control DLL hashes match the final build identities above. FUBAR
  executable SHA256: `8EFC55EA2CB8780FAEEACB44C82A0B046FA651D923284370045BB373D0A37BAD`.
- The directory retains result/provenance JSON, browser and application logs,
  sanitized application logs, and control/Inmarsat screenshots at each viewport.

This replaces the interrupted earlier pair as duration/shutdown evidence. It
does not qualify native FUBAR's full GUI: the original same-profile GUI can
autostart capture, prune data and save settings, and has no safe preview mode.
Actual native FUBAR all-button behavior therefore remains unqualified. The
completed path is CLI web-only FUBAR plus actual Town GUI/browser workflows.

Physical RTL smoke was **not run**: the existing `test_radio_live.py` persists
selection and provides neither full settings restoration nor isolated settings.
That would violate the no-permanent-settings-change condition. No physical-radio
pass is inferred from the successful pair.

## Earlier Failures and Superseded Runs

Retain unsuccessful attempts as history; later passes do not erase their scope
or cause. Original defect evidence and full audit findings remain in the linked
system/UX audit.

| Attempt | Observed Outcome | Disposition |
|---|---|---|
| Original RX and activation probes | Old reader published 1,024 stale samples into replacement state; generic activation return -2 was presented as live hardware. | Superseded by repaired regression and final integrated passes; retained as motivating evidence, not physical acceptance. |
| Original malformed-FM probe | NFM and WFM each produced 4,780 non-finite samples in the damaged block and 48,000 in the following clean input. | Focused repaired runs pass; retained as the motivating regression. |
| Initial aircraft popup fixture | Actual popup displayed `Alt nan ft` and `nan kt` for missing fields. | Popup repair and subsequent 4-case/92-assertion Qt run pass. |
| Early GUI/test-driver attempts | Marker pixel expectations sampled the wrong colored region; consent automation closed the dialog rather than clicking Yes. | Fixture corrections preceded the recorded passing runs; not additional product failures or independent coverage. |
| Intermediate compilation | Temporary disk exhaustion and an occupied Town executable blocked compilation/linking. | Environment/process-ownership failures retained; superseded by the successful final build in `audit_20261015_release_build.log`. |
| Successful pre-close integrated run | `build/audit_20261015_final_build.log` exit 0; `build/audit_20261015_repair_ctest_preclose.log` 23/23 pass in 138.48 s; DLL 33 fixtures, RDS/DTMF CLI and control views pass. Review then found normal-stop/RX-fault/cancelled-opener close failures bypassing quarantine. | Superseded for combined lifecycle acceptance by common cleanup, 142 assertions/eight scenarios and final 23/23 in 147.34 s. Earlier passes did not disprove the review finding. |
| Initial Town/FUBAR 180-second pair | Safe API/browser checks completed, then both processes were deliberately terminated for relinking; script exit 1. | This attempt remains interrupted, not a duration/shutdown pass. Evidence: `D:/FUBAR-qa/20261015-pair-180`. The separate final pair above supersedes it with 180 seconds and both normal exits 0. |
| Pre-final-link P25 comparison | Clear audio exact; encrypted fixtures gated; suspect case silent but work counters differed. | Superseded by final 0.2.139 eight-run results. Earlier artifact/identity and differing counters remain recorded above; neither matrix establishes blanket bit-exact behavior. |

## Integration Review Disposition

| Follow-Up Finding | Final Disposition | Retained Limit |
|---|---|---|
| Pending-open identity and native retirement/quarantine | Stable identity and common cleanup repairs included; focused close-failure paths and final integrated core/lifecycle suites pass. | Unmake exceptions source-reviewed only; physical recovery not fault-qualified. |
| Reentrant pager production and server stop/restart | Lifetime/generation fencing and regression fixtures included in passing final RemoteDiagnostics. | Fixture scope is not arbitrary reentrancy/load coverage. |
| Malformed upstream HTTP accepted by the earlier DLL | Expanded 33-fixture harness passes; final DLL identity retained in paired provenance. | Absolute receive deadline is not a whole DNS/connect/send deadline. |
| Named aircraft catalogs/views, source message ranges and mutation reply size | Compact catalogs, named paging, validated source ranges and bounded mutation acknowledgements have passing final server/GUI fixtures and addressed pair views. | Tested routes/budgets only; no exhaustive line/branch or every-record-size claim. |
| Final replay and paired duration/shutdown | Eight final P25 runs and completed 180-second pair pass with identified binaries. | Suspect counters differ; native FUBAR all-button behavior and physical RF remain unqualified. |

## Release-Verifier Preflight

Executed 2026-10-15 at approximately 21:49 Australia/Sydney, using the exact
`python scripts/test_*.py` commands in the workflow's **Release verifier negative
tests** step, in listed order. **15 scripts, 139 tests, zero failures; all exit 0.**
No test or expectation was weakened, and no native/shared build was started.

| Script in `scripts/` | Tests | Result |
|---|---:|---|
| `test_verify_release.py` | 19 | PASS |
| `test_package_inventory.py` | 20 | PASS |
| `test_stage_runtime.py` | 9 | PASS |
| `test_vcpkg_sources.py` | 12 | PASS |
| `test_project_sources.py` | 4 | PASS |
| `test_vcpkg_tooling.py` | 5 | PASS |
| `test_embedded_notices.py` | 11 | PASS |
| `test_qt_sources.py` | 8 | PASS |
| `test_msvc_materials.py` | 4 | PASS |
| `test_qt_replacement_harness.py` | 17 | PASS |
| `test_remote_diag_server.py` | 7 | PASS |
| `test_diagnostics_packaging.py` | 1 | PASS |
| `test_benchmark_fm.py` | 7 | PASS |
| `test_p25_timeline.py` | 5 | PASS |
| `test_distribution_materials.py` | 10 | PASS |

YAML parsing and structural assertions also pass: expected workflow triggers,
all 15 script paths, one failure-propagation check per command, explicit updater
and control-DLL build targets, exact updater CTest regex with `--no-tests=error`,
and the actual `--dll` harness argument. This is not a hosted Actions execution
or full GitHub workflow semantic validator.

Host Python was **3.10.10**; Actions specifies **3.12**. Tests used temporary
fixture directories and loopback diagnostics traffic. The diagnostics packaging
test invokes CMake script mode against temporary output, not a build; the Qt
replacement test exercises the harness without compiling Qt. Passing verifier
fixtures does not qualify the real staged release, source replacement rebuild,
signatures, public artifacts, telemetry service or physical hardware.

## Reproduction Gates

Run from the Town repository after the parent-controlled build has completed.
No concurrent shared build is part of report consolidation.

```powershell
ctest --test-dir build -C Release -R "^UpdateManager$" --output-on-failure --no-tests=error
python scripts/test_control_client_transport.py --help
python scripts/test_control_client_transport.py --dll build/bin/Release/SdrTownControl.dll
& build/bin/Release/update_manager_tests.exe --reporter compact
```

The new CI steps use the first and third commands; the direct updater executable
run here confirmed 12 cases / 278 assertions against the existing built binary.
The maintainer's initial transport run confirmed 10 cases; the later parent-run
pre-close gate confirmed 33. Neither automatically qualifies a future relink.
`tests/run_aircraft_freshness.ps1` builds its own temporary executable and
reads the existing unchanged SGP4/dependency libraries; its Qt/MSVC/SDK defaults
are host-specific and configurable, not a new shared-build CI requirement.

## Remaining Qualification

- ISS-0117/T-0143: one hosted documentation-CI paired Satcom open exceeded10s
  after applying the mutation. The unchanged retry passed; root cause remains
  unknown. Keep this distinct from the passing release and downloaded-pair gates.
- Test actual RTL and SDRplay receive/stop/restart, unplug/replug, delayed driver
  returns and shared-tuner cleanup while another radio remains active. The absent
  SDRplay service is a real prerequisite, not a passing hardware test. Physical
  RTL smoke was intentionally not run because its script persists settings and
  cannot guarantee full restoration. Unmake exceptions were not fault-injected.
- Exercise live audio-device playback and prolonged contention, valid FM/tone/RDS,
  and field/listening checks. The final local P25 clear/encrypted/suspect replay
  matrix is complete; it does not add new subjective listening or on-air evidence.
  Do not infer continuous audio from UI or static source checks.
- Check live aircraft ADS-B/ADS-C reception with real source outages/reconnects,
  stale fixes, clock corrections and network-off behavior. Qualify local altitude
  decoding separately if a trustworthy reference fixture is added.
- Exercise real satellite/SSTV/Inmarsat workflows and relevant ownership transfers
  on appropriate hardware and signals. No transmit, rotor or bias actions were
  authorized or qualified by these dry-run tests.
- The final 180-second pair, addressed route checks and normal shutdowns are
  complete. Broader native/browser controls, external assets and longer runs are
  not universally qualified. Native FUBAR needs a safe same-profile preview or
  isolated-settings workflow before all-button testing; its current autostart,
  pruning and settings persistence were not exercised under this permission scope.
- Qualify updater behavior with controlled real deployment/installation only when
  separately authorized. Consent UI tests do not authorize telemetry uploads.
- Public-package Actions, signing/provenance, hashes and smoke checks are complete
  in Public Package Evidence and BUILD_NOTES. Installer execution/rollback and
  the physical/native-UI items above remain unqualified. Evidence-only source
  updates do not replace the qualified binary tags or manufacture another version.
