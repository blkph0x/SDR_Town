# Issues (canonical)

## ISS-0037 - FM chunk boundaries and sample lattice (2026-09-26, OPEN)

T-0073 / DEC-0145 repairs WFM speech FIR, decimator and PCM boundaries while
keeping RDS/NFM independent. Former hidden WFM fixture is promoted to mandatory
[wfm][pcm-stream], now passes first32 assertions. Final qualification pending.
RF blocker rejection, cost profiling and physical listening remain open.

DEC-0144 characterization: actual WFM PCM at2.048MS/s whole/split counts4800/4800
but max absolute error0.0494488; at2.4MS/s counts4800/4805 and error0.209476.
Input100ms,1100Hz tone,50kHz deviation,8192-sample chunks,48kHz PCM. Reproduce
with sdr_town_tests.exe "[.wfm-characterization]". This opt-in diagnostic fails
three of four assertions on the unchanged baseline; excluded from ordinary
suite intentionally, not a passing release gate. Repair must separate WFM
speech decimation/FIR/PCM from the already-independent RDS tap and preserve it.

T-0071 / DEC-0143: NFM PCM count and waveform partition failures reproduced
and repaired using a persistent cubic sample clock and sample-wise fade.
51 assertions cover eight rate pairs, resets, rate transitions and zero missing
lookahead/phase repair. Local full suite 15/15 PASS. WFM boundary behavior,
blocker rejection and physical listening qualification remain open.

T-0070 / DEC-0142: actual NFM tests reproduced seven failures. Causal FIR and
persistent decimator phase now satisfy discriminator partition equivalence at
four rates, with explicit reset/rate-transition tests. This closes those NFM
producer defects only. PCM resampling/rounding and WFM boundary behavior remain
open, now with numerical diagnostics. No claim of complete audio-chain repair.

DEC-0141 / DSP_AUDIT_20260926.md. NFM second-stage and WFM speech decimators
restart at index zero per call. Centred speech FIRs omit future block input;
short NFM blocks do not advance FIR history. Final fixed-count resampling can
mask sample-accounting errors. Source evidence and exact stage-count example
are in the audit, not yet an end-to-end audible reproduction. Existing 15/15
tests pass but FM coverage does not establish high-rate partition invariance.
Add actual pipeline characterization before isolated fixes. Preserve P25 and
existing WFM/RDS acceptance; do not change shared protected files casually.

## ISS-0036 - Rotator physical acceptance (2026-09-26, OPEN)

DEC-0140 uses the documented Hamlib ERP bridge for ten named controller
targets plus other supported backends. Software fixtures do not prove physical
movement/stop, axis support, serial setup, limit switches or hardware SWR
accuracy. Test with controller owners before claiming device certification.
No automatic TLE tracking or native serial/backend bundling in this milestone.
Network Stop is best-effort, not a hardware emergency interlock. App/process
termination or lost connectivity cannot guarantee a physical stop.

## ISS-0035 - Diagnostics delivery/configuration/security (2026-09-26, OPEN)

CI additionally exposed non-deterministic SQLite connection lifetime on
Python3.12 (WinError32 on cleanup). Transaction contexts did not close DB
handles. Explicit contextmanager/finally closure repairs it; rerun recorded
in BUILD_NOTES. Production deployment gate remains separate.

Recent CI ZIPs omitted the diagnostics defaults supplied by release.ps1;
later enabled:false configuration could not override enabled:true. Client had
unbounded network waits and accepted non-error replies without JSON receipt.
Collector accepted arbitrary JSON objects, lacked submission/concurrency
limits, and allowed client-key admin fallback when no admin key was supplied.
Local DEC-0139 repairs pass 14 CTest suites and collector/packaging tests.
Deployment restart was denied by environment policy; existing server is not
running the repair. Do not claim secure live delivery or publish this as fully
qualified until server deployment and public receipt have been verified.

Threat-model gap: distributed shared client credentials are extractable.
Schema/rate limits mitigate abuse but cannot establish truthful reports.
Per-install enrollment, revocation and durable retention quotas remain open;
no per-install authentication or anti-forgery guarantee is claimed.

## ISS-0034 - Inmarsat aircraft/status visibility gaps (2026-09-26, FIXED)

Non-position identities/counters were not retained outside the 500-message log.
There was no per-channel status/history table. Combined data+voice watch was
incorrectly described as data-only in speaker status. DEC-0138 / T-0066 adds
passive monitoring and bounded aircraft storage; no DSP/gating changes.
Full satellite field acceptance and unreceived identity enrichment stay open.
Fixed cb2ee37 / public v0.2.103, local 13/13 and both Windows CI runs PASS;
public ZIP independently verified and executable smoke-tested. Screenshot
review also caught/repaired default delegate MHz rounding. See BUILD_NOTES.

## ISS-0032 - Aero 10 MS/s realtime headroom remains insufficient (2026-09-26, OPEN)

Unloaded synthetic 64 x 65536 CF32 benchmark measures pre-optimization load
ratio 1.32 with one worker and 2.29 with sixteen. DEC-0136 contiguous FIR
history lowers these to 1.22 and 1.74, respectively; still slower than realtime.
2.048 MS/s ratios after optimization are 0.26-0.41 for 1-16 workers. Logs:
build/watch-102-idle-benchmark.log and build/watch-102-fir-benchmark.log.
This includes modem startup and synthetic quiet IQ, not valid multi-call speech.
Do not promise reliable 10 MS/s or increase buffering to hide sustained overload.
Next: profile NCO and shared coarse channelization/PFB, preserve frequency/phase
and channel isolation against reference IQ; qualify on RSPdx and slower PCs.

## ISS-0033 - In-band data/voice unnecessarily retuned (2026-09-26, FIXED)

Planner separated roles even when the entire list fit one passband/budget.
DEC-0136 combines only the all-fitting case, retaining independent workers and
one speaker focus; no dwell/refresh transition resets a combined group. Unit
and real-widget tests pass. Source 89a0067 / public v0.2.102: local and CI
gates plus independent public ZIP verification PASS. Live RF acceptance remains
separate; this repair does not claim multiple physical device support.

## ISS-0031 - Global receiver ownership blocks independent modes (2026-09-26, OPEN)

DeviceManager has one lease owner/index, MainWindow has one takeover record,
InmarsatEngine owns one cursor/worker/config. Independent P25/data/voice radios
need all three migrated, with generation-safe stop/restore and stable device
assignments. Merely removing the global rejection would permit ownership races.
Tracked by T-0062 and MULTI_SDR_SESSIONS.md; no working multi-SDR claim yet.

## ISS-0030 - Saved constellation selection hides manual decoder (2026-09-26, FIXED)

refreshVisuals matches a selected watch UUID, but manual snapshots use empty ID.
Manual Tune/Start and preset activation did not clear that UUID. Fix tracked in
DEC-0134. Inactive watch groups legitimately have no current symbols; do not
display another channel or fabricate clusters to conceal this distinction.
Fixed in c313f06 / v0.2.100. Native continuous-mode and concurrent-worker
scatter, GUI retune/identity/render tests PASS; public CI-built asset verified.
Live satellite reception and burst RF acceptance are not claimed by these tests.

## ISS-0029 - Rate dropdown did not select a frequency (2026-09-26, FIXED)

Changing Aero rate altered only the decoder combo; no signal handler selected
a corresponding preset. DEC-0133 adds explicit user-activation preview using
the current plan, preserves programmatic restoration and reports missing
survey entries. Tune/Start retain their existing retune/commit responsibility.
Fixed in 9834492 / v0.2.99. All 35 plan/rate combinations covered by the GUI
regression; local and CI gates PASS and public release asset verified.

## ISS-0028 - Incorrect Aero rate/frequency presets (2026-09-26, FIXED)

APAC JSON assigned 10500 bit/s to 1542.935 MHz (surveyed 8400 voice), interpolated
22 data channels without a source, and used an unsupported voice default. Other
regional tables similarly lacked actual survey entries. DEC-0132 replaces all
five using dated source facts, removes fake fallback, fixes first-run center and
four-place MHz display. Saved user watch lists are preserved, not silently retuned.
Offline survey tests and GUI exact-selection tests gate the new release.
Fixed in 6f0e3f3 / v0.2.98: local 13/13 suites and both CI builds PASS; all
five preset datasets in the public ZIP pass the same offline checks.

## ISS-0027 - SDRplay release prerequisite mismatch (2026-09-26, PACKAGE FIXED; RF ACCEPTANCE OPEN)

InmarScope bundles a matched SoapySDRPlay3 plugin; SDR Town 0.2.97 does not.
The vendor API alone is insufficient for the latter. See
SDRPLAY_RELEASE_AUDIT_20260926.md for confirmed package/log evidence and the
separate unresolved RTL-visibility and P25 field reports. No RF repair claimed.
DEC-0132 builds/ships the matched MIT plugin against Town's Soapy runtime.
Fixed in 6f0e3f3 / v0.2.98. Full local suites and clean CI PASS. Downloaded
public EXE loads its bundled 0.5.2-48bd8b4 plugin, with exact module hash checked.
Vendor service/RSP unavailable here: tester enumeration/open/receive acceptance
remains required. Bundling does not install the proprietary driver/service.

## ISS-0026 - CI portable missing RTL module (2026-09-25, FIXED)

Run 36108455138 passed build/tests but the added package gate failed on
SoapyRTLSDR.dll. Local builds had relied on a pre-existing binary; clean CI
never built it. Compile audited upstream 6ca357c against the configured
Soapy/RTL libraries and require bias-T feature detection, license and provenance.
Stage MSVC CRT DLLs explicitly too: windeployqt warned VCINSTALLDIR was unset.
Local source module build PASS. No release was published by the failed run.
Fixed in bf83d97: Actions 36110284353 PASS; public portable's bundled module
successfully probed the attached RTL. CRT files included in verified package.

## ISS-0025 - CI portable configuration omitted RDS (2026-09-25, FIXED)

Windows CI configured SDR_TOWN_BUILD_RDS_DSP=OFF, unlike normal local releases.
Publishing it as the new default would omit the existing RDS backend/tests.
Initial 0.2.97 runs cancelled before publication. DEC-0130 enables the existing
backend and gates staging/public downloads on required DLLs and recorded-MPX
CLI tests. Fixed by 4714570/bf83d97: Actions 36110284353 PASS; recorded-MPX
tests on the public downloaded executable PASS in CI and locally.

## ISS-0024 - RTL-SDR bias-T missing (2026-09-25, SOFTWARE FIXED; ELECTRICAL ACCEPTANCE OPEN)

Source search confirms only SDRplay biasT_ctrl is implemented. SoapyRTLSDR uses
the distinct biastee boolean, conditionally advertised when its library supports
rtlsdr_set_bias_tee. Its cached readback cannot detect a missing physical bias-T
circuit and upstream ignores the C library return value. Add the missing app
route without claiming electrical confirmation or writing arbitrary GPIOs.
DEC-0129 implements GUI/CLI/persistence, actual-handle startup, checked live
writes and cleanup OFF. Unit/widget/lifecycle tests PASS, including ignored
writes, failed ON rollback, failed stream setup, RX exception and unsupported
driver restart. Full CTest 13/13 PASS. Actual local RTL read-only probe reports
driver support and OFF. Physical model/circuit/voltage remain unverified; the
driver's cached state cannot certify them. Published in v0.2.97-experimental;
public executable read-only probe PASS, physical acceptance still open.

Never delete a row. Close with a commit hash and a sentence.

## ISS-0023 - RSPdx controls retain unprobed defaults (2026-09-25, SOFTWARE FIXED; RF ACCEPTANCE OPEN)

Confirmed: light enumeration assigns RX to every RSP; async open never enriches
capabilities. Device Manager skips probing while any receiver is live, leaving
RSPdx without A/B/C and disabling Bias-T for unknown RX. Panel also starts on
row zero regardless of selected receiver, and live setters persist before
checking hardware success. Static port table incorrectly permits RSPdx A and
RSPduo tuner 1 Bias-T. User identifies RSPdx; no physical local RSP available.
DEC-0128 discovers capabilities on the actual opened handle, checks driver
readback, restores AGC/antenna/bandwidth after activation and exposes errors.
Contract, native-widget and DeviceManager lifecycle tests PASS; full CTest
12/12 repeated PASS. Hardware checklist is docs/RELEASE_0.2.96.md. Cached Soapy
readback is not a physical RF/voltage measurement. RSPduo second-channel
unchannelled settings and physical dual-tuner operation remain unqualified.
Published v0.2.96, source bb91aa6/tag ade17a7; Windows CI 36102563157 PASS,
eight downloaded release assets verified. Software repair is available for
the reporting RSPdx owner to complete the physical checklist.

## ISS-0022 - Aero display stalls and serial multi-channel overload (2026-09-25, FIXED)

Confirmed 500 ms widget timer limits visuals to 2 Hz. Four serial watch channels
take 2.463 s per 2.048 s IQ (load 1.203). DEC-0127 separates visuals, adds bounded
workers and constellation monitoring. Source e46f437, tag d7979ee, published
v0.2.95: local full CTest 11/11 and Windows CI 36095455129 PASS. Four-channel
processing now .639 s per 2.048 s IQ on this PC; reference PCM unchanged.
Packaged real RTL hot-watch edits and eight downloaded asset checks PASS.
Shared DeviceManager FFT remains unchanged at >80 ms update spacing; the new
50 ms visual polling timer does not claim 20 Hz of independent RF frames.

## ISS-0021 - Inmarsat cannot take over a P25-configured receiver (2026-09-25, FIXED)

User reports the P25-active/another-SDR warning in 0.2.93. Confirmed source:
MainWindow's Satcom host rejects active receivers with P25 flags, while Inmarsat
Start only passes force to the device lease and has no P25 stop/confirmation
path. Stopping hardware does not clear those Receiver flags. Exact tester
startup sequence is not yet supplied; do not claim a specific RF failure.
DEC-0126 adds an explicit handover instead of bypassing the guard.
Local repair verified: full CTest 11/11; native confirmation/cancel/failure tests;
real RTL GUI host live, configured-only and auto-follow handovers PASS, including
fresh IQ and no P25 restart on Stop. Repair source 2952535 published as
v0.2.94 (tag 5fc5fc4). Independent Windows CI 36079172152, packaged real RTL
GUI handover and downloaded asset/signature verification PASS. P25 DSP intact.

## ISS-0020 - Inmarsat tone-only report (2026-09-25, OPEN)

DEC-0123: confirmed live selection and ordinary Listen ownership bugs. A local
old synthetic replay log has zero voice/PCM and cannot diagnose the remote
report. Need tester frequency, live vs replay, selected decoder, fresh Inmarsat
JSONL and preferably the same IQ producing voice in JAERO. Do not equate an
8 kHz PCM stream or public reference silence/tone with intelligible speech.

Local DEC-0123 repairs complete and published in v0.2.93 (ca6354e; T-0051): manual Tune/Start selection,
panel-open persistence, paired host takeover, default-output visibility and
bounded PCM diagnostics. GUI 33 assertions plus 20 hardware-free takeover
assertions pass; 20 focused native/replay tests pass. Reference PCM unchanged
in GUI/CLI fast/paced and staged execution. Await the actual tester IQ/log before
closing the field report or claiming audible speech is fixed.

## ISS-0019 - SSTV active-producer detach test can stall (2026-09-25, FIXED)

DEC-0125 repair: pending control admission quiesces new publishers before waiting
on the feed mutex; matching-session stop semantics remain unchanged. Ten unchanged
isolated stress runs pass in 0.062-0.079 s and full CTest passes 11/11. Earlier
evidence below remains the historical record; no physical RF claim is inferred.

During DEC-0122 final regression repetition, UnitTests stalled and was stopped
after 246.34 s (no assertion failure before termination). Non-invasive CDB
snapshot shows the main thread waiting in Mtx_lock while one worker continues
float processing. A fixed-seed duration run then stalls after the preceding
case; test listing identifies "SSTV receiver detach quiesces an active producer".
Running that case alone also exceeds a 20 s subprocess deadline and is killed.
Its test launches an unthrottled producer and performs 100 attach/detach cycles.
SstvReceiverFeed serializes lifecycle and publish using the same mutex; a
starvation/lifecycle audit is needed. Do not claim an exact production cause
from the Release stack, which lacks private function symbols.

The SSTV feed/input/test sources are byte-identical to base 825dae2 and the
isolated test does not call SDRplay. Initial full suite passed, so this is an
intermittent qualification gap, not a silently waived pass. Remaining 389 core
cases: 387 passed / 2 optional skips, 206298 assertions. All final SDRplay/GUI/
Rust gates pass. No SSTV or P25 change made in the SDRplay-only repair.
Evidence: build-audit-20260925/sdrplay-test-stack.log, sdrplay-ctest-final.log,
sdrplay-core-durations.log, sstv-detach-isolated.log, sdrplay-core-remainder.log.

## ISS-0018 - SDRplay runtime discovery/registration gaps (2026-09-25, OPEN)

DEC-0122 / T-0049. Missing candidate layouts, startup-only API dependency load,
false success on module registration errors and repeated alternate-module loads.
The local CLI's API-open failure is confirmed alongside an absent service/RSP;
the affected tester's failure is not yet confirmed without model/version/log.
Add executable DLL-loader regression tests and explicit runtime/service diagnostics.
Repair implemented locally: five loader tests plus full CTest pass, actual CLI
registers installed Pothos module and identifies absent vendor service. API/module
load and registration are separate stages; successful factory is retained,
failed discovery retries. Open pending the affected PC's RSP discovery/RX
acceptance; not claiming that the local missing service explains its failure.

## ISS-0016 - Inmarsat voice qualification and follow lifecycle (2026-09-24, OPEN)

DEC-0121 update: real pinned continuous/burst modem/FEC/CRC and isolated mini-m
codec now integrated. Public recordings produce voice-codeword PCM and real
ADS-C positions through IQ to GUI map. GUI/CLI fast/paced WAV bytes agree.
Found/fixed upstream shared ECC scratch and modem statics, strict-zero-fill
CRC rejection and convolutional unpack bounds. Existing regex/incorrect codec
fixtures are no longer wired to the live engine. Remaining: clear-conversation
RF acceptance using dad's same IQ/JAERO reference, security/call-end and auto
follow, dual-device and photos. No claim of full Inmarsat service support.

Original DEC-0120 finding, before the native DEC-0121 integration:
Live physical probe has no validated unique-word framing/deinterleave/FEC/CRC.
Raw bytes are correctly blocked from ACARS, assignment and map ingestion. The
unused InmarsatVoice wrapper incorrectly advertises HAVE_MBELIB as Aero support
and applies row-major AMBE3600x2400 to 96 bits. JAERO/libaeroambe uses the separate
mini-m AMBE4800x3600 codec and explicit 6x24 interleave. DEC-0120 disables the
incorrect path. Need a reference recording plus JAERO output, licensed/pinned
protocol and isolated Aero codec integration, CRC-valid assignments, explicit
call direction and validated PCM-to-aircraft association before auto follow or
green talking markers. Existing aircraft photoUrl points to JSON, not an image;
photo retrieval/attribution and position freshness also need separate tests.

## ISS-0017 - Tester diagnostics endpoint is not deployable (2026-09-24, OPEN)

Repair: isolated Apache route at gearsqueens.online/sdr-town-diag added after
backup/configtest, old app routes unchanged. Local opted-in config updated to
HTTPS; four actual GUI/CLI replay summaries saved with correct session/counts.
Package injects this endpoint disabled by default; tester consent control added.
External web-fetch tool could not verify off-LAN connectivity; dad's network is
the remaining reachability gate. Collector server-wide rate/retention hardening
is separate from the existing bounded client budget and is not claimed here.

Original finding, before the isolated HTTPS deployment:
AppData remote_diagnostics.json points at http://127.0.0.1:8787/ingest with a token.
Remote clients would send to themselves; current client correctly disallows bearer
tokens over HTTP. No packaged collector config in current release staging. Need
confirmed public HTTPS endpoint, external reachability, authenticated receipt and
preserved tester opt-in before packaging. User asked for endpoint; no token requested.

## ISS-0015 - Push CI P25 guard checks an empty diff (2026-09-24, OPEN)

2026-09-26 reconciliation: ca6354e already wires GITHUB_EVENT_BEFORE from
github.event.before. Normal pushes now use the actual prior commit; the next
release pass will record that guard's concrete comparison. First-branch pushes
still fall back to origin/master, so explicit first-push/PR base-selection
coverage remains an open follow-up, not an unfixed missing-environment claim.
Master CI 36231028781 concretely compared 47d414ac7b097abe387d6a71510e6bcc7011a52f
against cb2ee37 and reported 23 changed paths / 0 protected, confirming the
normal-push repair. No broad P25-path exception was introduced.

Observed in completed Windows run 35986929161: guard output compares origin/master
to HEAD and reports 0 changed paths, although the push contains SSTV changes.
The workflow reads GITHUB_EVENT_BEFORE but does not populate it from the push
event, then falls back to origin/master, already at the checked-out commit.
Build/native/Qt/Rust/package gates ran and passed; this guard is not independent
proof of P25 preservation. Local reviewed diff for 0.2.90 has no P25 or shared
receiver/device/demod/audio-engine core changes; MainWindow has SSTV-only wiring.
Follow-up: supply the real push base, test first-push/PR cases, and define narrowly
reviewed non-P25 integration exceptions without weakening protected DSP checks.
Do not silently add a broad MainWindow allowlist just to make the guard pass.

## ISS-0014 - Windows CI release fixture drift (2026-09-24, CLOSED)

Run 35979813492: native Release build, core, workspace and SSTV tests passed;
`scripts/test_verify_release.py` then failed 4/11 cases because its ZIP lacked
build-info.json (also SGP4 licence files). Reproduced locally. DEC-0118 updates
the fixture and adds provenance failure coverage. Fixed in 2b615ba; run
35984373766 passed build, all test gates, staging, packaging and artifact upload.

## ISS-0013 - SSTV RF Auto was image-format Auto only (2026-09-24)

Confirmed by MainWindow::ensureSstvWindow routing to only the selected demod tap.
DEC-0117 adds worker-isolated RF acquisition with manual fallback and explicit
UI/API identity. Synthetic IQ also exposed the SSTV SSB bandwidth mismatch:
3 kHz is halved by HfDemod, attenuating the 1900 Hz VIS leader and image tones.
SSTV-specific routes now request 6 kHz (3 kHz one-sided). No general analog
bandwidth semantics or P25 DSP changed. Hardware RF qualification, extended-VIS
RF auto and headerless RF auto remain open; manual RF still supports the image
helper's extended VIS/line-sync formats. See BUILD_NOTES for executable evidence.

Recorded RF round trips also reproduced existing image-helper line-sync false
partials after a valid Robot36 in trailing noise (USB: pd290 12 rows; NFM: sc148
74 rows; direct audio also reports a false partial). These are not complete
images or RF route confirmations. Further line-sync qualification is open; no
decoder confidence thresholds changed in this RF-routing patch.

## ISS-0012 - 0.2.88 receive-chain audit findings (2026-09-24, OPEN)

See [AUDIT_20260924.md](AUDIT_20260924.md) A01-A17 for exact locations, evidence,
impact, repair method and gates. Measured failures: HF stronger-signal blanker
latch, HF throughput at 2.4/10 MS/s, PR #32 high-rate alias leakage, and SGP4
reference-vector mismatch. Additional code-proven issues cover Inmarsat input
rate, tune failure acknowledgment, cross-system follow metadata, direct-sampling
capabilities, Doppler/SSTV continuity and scanner/data-tap/helper integration.
No claim that alias import caused the reported live follow failures. The repair
status table at the top of AUDIT_20260924.md records implemented and open items.
HF/orbit regressions and SSTV file/live mismatch are repaired with executable
tests. Doppler now uses fixed RF and continuous digital correction; live pass
acceptance remains open. Hardware lifecycle, APT profile and protocol acceptance
remain open; preserve P25 DSP. Implementation queue T-0047 is not complete.

2026-09-20 v0.2.74 published at 4f26f2e (GitHub Latest experimental). P25 DSP
unchanged. SDRplay vendor API/SoapySDRPlay3 are **not** in the installer by
design (`docs/SDRPLAY.md`); host install + exclusive RSP access required.
`SdrTownControl.dll` is the FUBAR companion bridge (FUBAR GitHub Latest is
**1.1.33**, still shipping the **0.2.66** DLL). Pair 0.2.74 by copying
`SdrTownControl-0.2.74-win64.dll` beside `FUBAR.exe` as `SdrTownControl.dll`.
OPEN T-0041: ZIP also has leftover `SdrTownControl-0.2.71-win64.dll` (glob);
missing `data/inmarsat/*.json`. See `docs/FUBAR_PAIRING.md`. Do not overwrite
tag v0.2.74.

2026-09-18 DEC-0100/0101/0102 scope: alias-list names resolve in the P25
talkgroup table and site labels in control-log/tooltips, only with known system
metadata. CSV talkgroup/site import is supported; SDRTrunk XML, RadioReference
API integration, radio-ID/log/transcript aliases remain deferred. No claim of
full SDRTrunk playlist compatibility or bundled regional names. Untrusted
source labels forced to plain text before release.

2026-09-18 DEC-0099 updates T-0031: receiver attachment, Finish drain, cancel,
GUI lifecycle and combined recording parity are implemented and tested.
Qt slots macro compile collision corrected by renaming the private constant.
Live known-image RF acceptance remains OPEN. On a gap the session fails closed
and discards provisional output; automatic restart is deliberately deferred,
not silently presented as implemented. Live HF/SSB and additional SSTV modes
remain unsupported. Fractional-rate RF quality is not inferred from fixtures.

2026-09-18 OPEN T-0031: NFM raw tap can produce fractional sample rates
(Demod.cpp uses input rate / integer decimation), whereas the pinned SSTV
backend takes an integer rate. Rounding the metadata is not a verified
conversion. Qualify a continuous resampler against independent recordings
before attaching live input. Queue lifecycle requires producer detachment and
quiescence before restart; validate that in receiver integration. These are
unfinished live-feature gates, not defects in the existing recorded decoder.
DEC-0096 closes helper stdin transport qualification on eight recording cases.
DEC-0097 now implements a separate converter using scaled integer rates rather
than the float API's quantized ratio. Partition/drift/tone and independent image
gates pass. Images cover native 32/44.1 kHz recordings; fractional-rate coverage
is synthetic, not off-air acceptance. Receiver gap/lifecycle wiring, bounded
pipe worker and live RF still remain open. Preserve true rate in ingress events.
DEC-0098 now qualifies the combined worker on recorded inputs and fault cases;
receiver attach/detach, automatic restart after a gap and GUI live controls are
still open. A live controller must discard provisional previews on exceptions.

2026-09-17 OPEN T-0029: hosted run 35225600073 at 3a30f1d (0.2.56 checkpoint)
fails 14 P25 string verifiers; 0.2.57 local sweep reproduces 133 pass/14 fail.
Failures: capture_fixes, clear_to_encrypted_mac_bar,
clear_trusted_hold_and_structure_cold_exit, dec0055_epoch_dual_slot_origin,
dec0062_talkspurt_vocoder_reset, dual_ambe_module, opposite_slot_no_probe_thrash,
playback_ring_target_fill, playout_bridge_real_pcm_priority, retune_and_streaming,
scheduler_optimizations, session_sustain, streaming_framer_commit,
sustain_robustness (all verify_p25_phase2_*.py). Native core/Qt cases pass.
git diff v0.2.56 v0.2.57 of main.cpp, P25 sources/headers, DSP and P25 verifiers
is empty. Do not change DSP to satisfy text markers or silently weaken tests;
review each against current definitions/DEC history and behavioural tests.
This is an existing QA gap, not SSTV proof or a claim of stale/harmless tests.

2026-09-17 DEC-0092 validation limits: Martin1 reconstructs the independent BBC
card but has visible noise/colour differences from colaclanth's output (RGB MAE
20.7794). Keep experimental, not pixel-identical quality. Robot36 manual and
auto have different acquisition alignment; both pass upstream reference gate
(MAE 10.4065/14.3024). Incorrect cross-option equality assertion corrected to
matching-option app/helper parity plus independent image gate, not a DSP tweak.
Live SSTV, GUI image workflow and unqualified modes remain T-0022; no global
decoder sensitivity or damaged-signal robustness guarantee follows from two files.

2026-09-17 DEC-0089 CLOSES reproduced T-0026 native teardown fault: legacy
rtlsdr.dll in the executable folder failed a standalone lifecycle probe on
cycle 2, independent of Qt/Soapy/DSP. Configured dependency passes 20 native
cycles across source/deployed locations and five CDB real-GUI shutdown cycles;
live RDS and parity pass too. CMake now stages the configured RTL shared target,
and deploy hash/license validation prevents carrying this stale DLL forward.
This closes the reproduced failure, not all possible native hardware crashes.
The duplicate Pothos/bundled module loading observation was NOT proven causal
and was not changed speculatively.

2026-09-17 DEC-0087 CLOSES live RDS acquisition issue for this regression:
independent gain 40/20/40 captures yielded 0/53/0 valid groups, with ~32%/0%/~32%
raw ADC-rail incidence. GUI requested 20 dB passes twice with 396/261 groups,
correct PI/PS/RT and normal exit; adapter/native parity passes. Do not fix RF
overload by altering RDS synchronization constants or changing all-mode defaults.

2026-09-17 T-0026 OPEN: first gain-controlled GUI run suffered unhandled AV
0xC0000005 in ntdll.dll at shutdown despite successfully receiving RDS. Windows
Event 1000 report 4d71ce74-6f8c-4922-8d3a-f80430aba21b. Subsequent CDB probes
and GUI exits did not reproduce that unhandled fault. Native Soapy teardown
warnings remain; a first-chance event is not enough to locate root cause.
Need exception stack/dump before changing teardown. One subsequent run could
not enumerate RTL hardware; standalone RTL recheck succeeded. Keep failures
visible and require real hardware, not merely a streaming stub, in live QA.

2026-09-17 DEC-0086 update to the RDS issue: identical live-input comparison
proves zero differences across 1,446 blocks between native and adapted paths,
including resets. Both fail RDS acquisition. Independent demod of a gapless
5.024-second IQ capture also yields zero groups; known reference decodes at
the actual 204.8 kHz live MPX rate. Adapter corruption is not supported by this
evidence; RF impairment versus shared backend limitation remains OPEN. See
BUILD_NOTES for exact artifacts. No DSP threshold/timing changes justified.

2026-09-17 DEC-0085 OPEN: live RDS acceptance failed twice after registry
adoption. On 98.1 MHz the 45-second runs recovered 1 and 0 groups; neither
identified PI/PS. Both had only 3 startup resets and over 7.2 million samples.
Prior DEC-0084 live test recovered 355 groups. Adapter/native parity passes
on recorded MPX at every block and three partitions, but this does not isolate
the live failure. Preserve build/rds_contract_live_qa and
build/rds_contract_live_repeat evidence. Next: compare native and adapter on
identical captured live MPX, then inspect acquisition/RF only as evidence
requires. Do not assume weak reception or change PLL/filter thresholds.
No P25 changes, release, or claim of live non-regression from this gate.

2026-09-17 DEC-0084 closes the DCS framing/polarity implementation gap below:
independent ETSI waveform/bit tests now validate physical inversion and report
cyclic equivalent labels together. 105 enumerated payloads tested, not the 104
claimed by the reference's comment. Live routing works; independent RF code
accuracy, broader fading/clock recovery and sensitivity remain unverified.
No audio squelch is controlled by this experimental detector.

2026-09-17 DEC-0081/0082: CTCSS live QA exposed repeated resets; first
45-second run ended with 22 resets and zero current-stream windows. Varying-BW
regression reproduced loss of tone (0 instead of 123 Hz); data-only history fix
passes it. Second live run still failed (15 resets); instrumentation then proved
speech-only AFC reset propagation with adjacent IQ and constant bandwidth.
Independent NFM data mixer fix passed live retest; final bandwidth-only GUI
fix leaves only three startup resets and 35 complete windows. Known-tone RF acceptance
is explicitly deferred by user until their radio is available. No tone squelch.

2026-09-17 DCS polarity reference ambiguity: SDRTrunk DCSCode.java describes
normal as bit-reversed ETSI words and inverted as unreversed words, whereas
ETSI 103236 section 4.2.3 defines polarity by positive/negative deviation.
Bit order reversal is not polarity inversion. Before reusing those labels,
require independently generated/recorded normal/inverted discriminator fixtures
and verify cyclic-code aliases. test_dcs_reference.py now proves the bit-reversal
versus polarity distinction against independent standard vectors; live symbol
timing/filtering and full cyclic-alias handling remain to implement. Do not present a table lookup as validated live
DCS. Physical RF tests can use the user's radio when available; independent
bitstream vectors are the next implementation gate, not guessed DSP constants.

2026-09-17 DEC-0080: live RDS routing/display proven on 98.1 MHz with 402
complete groups and validated PS/RT. Supersedes live-integration limitation below;
multi-station/weak-signal/character-set acceptance and general registry remain.
Actual UI test exposed stale monitor frequency input after startup/waterfall
tune. Fixed by signal-blocked field synchronization; preview marker now uses
frequency rather than stale pixel position after hardware recentering.
Live test shutdown logged "Soapy teardown reported a recoverable non-standard
native issue"; process exited 0 and reception passed. Teardown remains an open
device-driver investigation, not hidden or changed in this decoder feature pass.

2026-09-17 DEC-0079: reproduced MPX partition discontinuity with 137-sample
input blocks. Fixed in the data tap with separate causal FIR/decimator history;
whole-vs-chunk regression now passes. Speech output remains unchanged.
The short upstream MPX fixture yields two groups, not the three initially
assumed by our test: checked upstream's own two-group contract and retained
our stricter identity confirmation. No identity-threshold relaxation.
Recorded MPX support supersedes the bit-only limitation below. Live source-loss
propagation, GUI metadata, offset/noise characterization and RF validation remain
open; do not advertise live RDS or close T-0018 from file replay alone.

2026-09-17, T-0017 scope (open follow-up): RDS consumes validated bitstreams,
not RF/MPX yet. Basic-Latin station text only. Carrier/timing recovery, Unicode
RDS text conversion, RBDS labels, GUI stale metadata and real-station validation
are required before enabling live RDS. Existing WFM decimator restarts its grid
per chunk; optional tap reports discontinuity for non-divisible chunk lengths
instead of feeding a future timing loop a falsely continuous stream. This is
documented, not silently fixed by changing the shared analog audio path.

2026-09-17, DEC-0077 (fixed in working tree, not released): PCM interpolation
depended on producer partition because its stencil read future samples and
clamped block tails. Failing regression reproduced; causal stencil passes.
The associated dB label transform and Qt profile-test settings leak are fixed.

2026-09-17, T-0016 (open): live 084229 has underruns and missing/rejected
voice frames despite no IQ overruns/producer drops. Replay still conceals 67
frames. GUI/CLI match output counts, not PCM; 327/456 frames differ. Investigate
vocoder state/random-number lifetime and per-window inputs before attributing
this to any one cause. mbelib uses rand(); GUI replay launches worker threads,
but this is a lead, NOT proof of cause. Likewise budget-aborted burst parsing
needs a deterministic loss test before modifying commit/deadline behavior.
Do not change security, slot rules, grace periods or buffers speculatively.

2026-09-17, REQ-BP.1 coverage limitation (open): receive profile infrastructure
does not constitute complete worldwide/national band plans. Partial AU/GB/US
data is labelled; sourced HF sub-bands, more countries and local transmitter
inventories remain. Old generic HF priors are removed rather than asserted to
be correct for every country. Decoder hints do not instantiate unimplemented
decoders. See BAND_PLANS.md. Existing monitor tuning limits remain unchanged.

2026-09-17, REQ-UI.1: workspace foundation tested independently of ISS-0001;
CLI and GUI P25 reference output remains byte-identical. RDS and the decoder
registry are next work, not present/working decoders. Existing receiver-table
and experimental TX limitations remain; the layout does not complete them.

2026-09-17, ISS-0001 evidence update (still open): DEC-0074 repairs a
source-confirmed callback/clear/discard cursor race. The new live capture has
no producer drops or control-lease collisions, so this race is not established
as the cause of its remaining speech gaps. Three follows and a 7.117-second
output span without an underrun rise do not establish all-call intelligibility.
See `P25_DOWNSTREAM_AUDIT_20260917.md`; no timing/gating thresholds were relaxed.

Status: `open` | `closed`

---

## ISS-0001 — P25 Phase 2 speaker audio is partial, not continuous

DEC-0072/73 update: full-frame tracing proved nonphysical 0/2 swaps under an
apparently good S-ISCH and missing block-tail A/B bursts. Both corrected and
covered by tests. Latest replay concealment falls 66 -> 3; live mapping-only
run recognizes an exchange but still has 21 underrun increases. Lower output
duration includes removal of false signaling-as-voice and is not a blanket
quality verdict. See `P25_MAPPING_AUDIT_20260917.md`. Issue remains open.

Follow-up: DEC-0069 fixes a reproduced slot-state selection mismatch, retaining
four previously stale-TG-rejected frames on reference 103841. Latest 060515
still has six feed gaps and 66 concealment frames, so this issue stays open.
GUI replay also reproduced a separate short-tail deadlock: 5760 samples waited
for a 11520-sample prime after EOF. DEC-0070 fixes it: final replay drains all
5760 samples and completes without pending data or timeout. These results
do not prove all live speech is intelligible or continuous.

2026-09-17 update: 060515 has gapless recorded IQ but 24 playback-underrun
increases. Exact RS arithmetic caching removes measured recovery overhead;
same capture still has six feed gaps and 66 concealment frames. Determine
same-frame RF/FEC provenance and remaining talkspurt resets before closing.
Do not equate the improved processing time or a coverage PASS label with
audibly continuous speech. See DEC-0067 and the 20260917 forensic report.

- **Status:** open
- **Opened:** 2026-09-07
- **REQ:** REQ-P2.0 … P2.6
- **Measured 2026-09-11 live CLI `p25 clearaudio` (HEAD after PR #12):**
  - CC 420.475, RTL real Soapy. TG **10301** clear slot0 @ 417.675:
    target WAV ~3 s, companion ~4 s; decode islands then `no voice sync`.
  - TG **20202** clear slot0 @ 417.675: target WAV **0**; companion ~47 KB.
    Follow IQ `20260911_082310_…deadline…15.0s`. Voicetest: slot0
    **drop=B** `targetVcw=94 fed=0 emit=0` (feed starve); slot1 drop=A.
  - File bars still hold: 060036 duty **0.705**; 095846 TG10301 duty **0.84**.
  - Capture keep-set trimmed (~29 GB → ~9 GB) to avoid disk pressure.
- **Measured 2026-09-09 streaming DDC (DEC-0038):**
  - 060036 stream env=1 after sticky Gardner: duty **0.23** (lock-create
    trial 0.12). Block still **0.705**. Default-on still rejected.
- **Measured 2026-09-09 capture `20260909_100909` (desktop after DEC-0036):**
  - Operator: full flip — little chirps instead of ~95% continuous.
  - max duty **0.40**, 0× ≥0.65 (095846 was **0.947**). DEC-0036 always-advance.
  - **DEC-0037:** restore clear-eye hold; advance only waiting-clear; no purge on hold.
- **Measured 2026-09-09 capture `20260909_095846` (desktop after DEC-0035):**
  - Later clear TG 10301: max duty **0.947**, 10× ≥0.65; emit>0 26/78.
  - Start TG 30302 unknown: `targetVcw=14` then rolling cursor hold → silent.
  - **DEC-0036:** do not hold rolling cursor when VCWs were not queued/fed
    (**superseded in part by DEC-0037**).
- **Measured 2026-09-09 capture `20260909_094846` (desktop after DEC-0034):**
  - Live still one-emit cliff: drop **A=60**/62; emit>0 **5**; max duty **0.338**.
  - Same IQ voicetest TG 30302 skip=1300: **duty 0.43** targetVcw=652 (RF OK).
  - Root: live hot cand=8/120 vs replay cand=16/240 after speak (DEC-0035).
- **Measured 2026-09-09 capture `20260909_092250` (desktop after DEC-0033):**
  - Operator: one small emit then nothing (worse than near-continuous clear).
  - CADENCE drop **A=116**/131; emit>0 **9**/131; max duty **0.416**; 80+280 held.
  - TG 12014: ~10 s high `targetVcw` with `fed=0`, one emit, then permanent
    `p2bursts=0`. Root: DEC-0032 `clearBlockCqpskHint()` on empty-eye streak
    wiped block Costas continuity; DEC-0033 companion-only sticky was ungated.
  - **DEC-0034:** keep block CQPSK hint on empty-eye; gate companion-only to
    streaming. File 060036 still **duty=0.705**. Live re-prove on new exe.
- **Measured 2026-09-09 capture `20260909_083254` (desktop after DEC-0032):**
  - Post-emit **80+280** held (284/284 on first 30302 follow). Rolling **4 s**.
  - CADENCE drop **A=147**/152; emit>0 **5**/152; max duty **0.416**.
  - First TG 30302 emit then ~0.6 s `no voice sync`. ForceMask **0** log hits.
  - File same island `PASS_ENCRYPTED_GATED` duty≈0.067 (short clear then enc).
  - **DEC-0033:** stop hop CPR; sticky streaming HDQPSK / persistent framer path
    (SDRTrunk/OP25). env=1 on 060036 duty **0.25** (improved vs ~0.09–0.16
    class; still ≪0.65). Block 060036 still **0.705**. Default-on still off.
- **Measured 2026-09-09 capture `20260909_081701` (desktop after DEC-0031):**
  - Single emit then hang `no voice sync`. Drop A 234; emit>0 7/241.
  - Post-emit hops `fresh=120ms` (DEC-0031 backlogCatchUp before sustain).
  - DEC-0032: restore 80+280 after speak; soft empty-eye rehunt (no MaskEpoch
    steal); keep once-clear continuation.
- **Measured 2026-09-09 capture `20260909_062006` (desktop after DEC-0030):**
  - LO park correct (421.96375). Rolling 4 s OK. Drain ~70 ms DSP / 80 ms fresh.
  - Live: drop A dominant; one emit then `no voice sync`. File: duty **0.46**
    drop D; dual-slot companion-louder / `unknown-waiting-clear` (DEC-0012).
  - DEC-0031: backlogCatchUp before speaker-sustain; once-clear continuation
    without fed chicken-egg. Do not soften PostEmitMixedMacDead.
  - Open: live extract cliff vs file on same IQ (block-channelize eye sustain).
- **Measured 2026-09-09 capture `20260909_060036` (desktop after DEC-0029):**
  - Sparse still: 10 emit s / 215; max duty 0.639 then cliff. Drop A / worker-busy.
  - File voicetest same IQ duty **0.705**; live rolling stuck at 4194304 (2 s).
  - DEC-0030: honor DEC-0023 4.0 s active rolling clamp.
- **Measured 2026-09-09 capture `20260909_053448` (desktop after DEC-0028):**
  - Sparse islands: 16 emit seconds / 176; 0× duty≥0.65. Drop A dominant.
  - Clear emit then `ended or went quiet` +5s while clearTrusted → cold re-arm.
  - DEC-0029: clear-trusted follow hold + structure exits coldAcquire.
- **Measured 2026-09-08 capture `20260908_115603` (desktop after DEC-0027):**
  - First TG 30302 emit perfect (duty 0.553), then dsp 470–605 ms cold poison
    on next hops; rest of call unheard / drop D+B. DEC-0028 removes post-emit
    emptyStreak cold escalate.
- **Measured 2026-09-08 capture `20260908_112922` (desktop after DEC-0026):**
  - Eyes OK; 0 ReturnEncrypted. CADENCE peak **0.639** (0× ≥0.65); drop **D**.
    worker-busy **417**. structureNoVcw eyes cold-escalated CQPSK (med
    **462 ms**, 40× ≥400 ms) despite soft mask rehunt. DEC-0027.
- **Measured 2026-09-08 capture `20260908_110146` (desktop after DEC-0025):**
  - Eyes OK; 0 ReturnEncrypted. File continuous (20202 0.85, 30017 0.74) but
    live islands only (12 ok s). Wrong-slot hops cold-escalated CQPSK
    (dsp p90 ~434 ms). DEC-0026. Residual worker-busy drop D.
- **Measured 2026-09-08 capture `20260908_103955` (desktop after DEC-0024):**
  - Eyes fixed (0× 40 ms). Same TG 20202: RID 0x1F83FF file duty 0.46 (weak
    RF) vs RID 0x1F95EB file duty 0.83; live cut good call with ReturnEncrypted
    while ess=clear. DEC-0025 MAC bar. Residual drop D on live.
- **Measured 2026-09-08 capture `20260908_101644` (desktop after DEC-0023):**
  - Soft-trim fixed (573440 dominant). Start/middle BAD; last voice ~90%.
  - First TG 30302: ~8 s of catch-up `context=81920` (40 ms) → drop **A** /
    no voice sync. Late 10330 stayed on 280 ms eyes. DEC-0024.
- **Measured 2026-09-08 capture `20260908_095936` (desktop exe):**
  - TG 30302 slot 0 @ 421.225. Good start (dutySec≤0.60) then cliff. Drop **D**
    while talking; then drop **A**. Companion-louder 0. Voice SNR ~17 dB.
  - Root: rolling soft-trim protected 80 ms → live eyes became 160 ms (DEC-0009
    failure mode). File skip=11000 `PASS_CONTINUOUS duty=0.685`. DEC-0023.
- **Measured 2026-09-08 capture `20260908_082235` (stock v0.2.51):**
  - CC improved vs earlier same-day. Talk median dutySec **~0.24**; drop **D**;
    worker-busy **54**; eyes 80+280; `cqpskCand=32`. Companion-louder active.
  - File path of same decoder class still hits duty 0.645 only because CLI
    waits (~17 s wall / 8 s). Live cannot.
  - Speed trials DEC-0020 / cand=3 / streaming 80&160 ms all **rejected**
    (duty → 0.01–0.125). Hard CQPSK hint stop kept. See BN-0011 / LOG.
- **Measured 2026-09-08 capture `20260908_075858` (stock v0.2.51 GUI):**
  - SNR **10.9 dB**. CC worse (BCH/NID gaps; ~86 s to first follow). Voice
    still drop-D ~0.31 talk duty — not a new failure mode. File TG 30017
    duty=0.66; late 10330 file `PASS_ENCRYPTED_GATED` while grant said clear.
  - Not DEC-0019 (`cqpskCand=32`). Do not blame uncommitted desktop build.
- **Measured 2026-09-08 capture `20260908_060221`:**
  - 148.8 s gapless, SNR ~17 dB. Dual-TG stretches + drop D (worker-busy
    100, dsp med 161 ms). Companion-louder 14/14 `fed=0` (DEC-0012).
  - File peak TG 30302 slot 1 skip=128500 center=420.21375 duty=**0.922**.
  - DEC-0019 hard hint early-stop kept; cand=3 after speak later rejected
    (BN-0011). Live re-prove pending on desktop HEAD.
- **Measured 2026-09-08 capture `20260908_053241` (v0.2.51 live):**
  - Dual-TG same RF: 12068 slot 1 + 30003 slot 0 @ 421.975 (LO ok, DEC-0016).
  - Operator: less garble, short emit islands. CADENCE median talk duty ~0.30;
    12 companion-louder hops `fed=0` (DEC-0012). Also heavy drop **D**
    (worker-busy). Context-only DEC-0012 trial rejected (105622 duty 0.62;
    041716 isolation regression). Stays on hop-wide DEC-0012.
- **Measured 2026-09-08 capture `20260908_041716` + DEC-0012 voicetest:**
  - Live: 70.75 s gapless, SNR ~18 dB. Operator: one good emit then wrong-slot
    garble. Call 2 seq=131 `opp=0 p2mac=5/6`; seq=134 `target=6 opp=12
    p2mac=0/0 ess=clear gate=emit`.
  - File: TG 10330 slot 1 skip=32111 center=421.21375 8 s
    `PASS_CONTINUOUS_AUDIO duty=0.87`. Companion-louder mixed hops no longer
    emit. 073304 same gate `duty=0.795`. 105622 slot 0 `duty=0.645` (was 0.685).
- **Measured 2026-09-07 (AppData captures + HEAD voicetest):**
  - Logs live in `%APPDATA%\SDR_Town\SDR Town\iq_test_captures\` and `...\logs\`.
  - Capture `20260905_161748`: 68 s gapless IQ; **1** `gate=emit` (320 ms); **41** silence-bridge top-ups; CADENCE **30/33 s** `block=no-vcw-from-live-window` (drop **A** after the island). Still needs live re-prove on DEC-0008/0009 binary (T-0010).
  - Capture `20260905_105622` TG 30003 slot 0 skip=97334 8000 ms, after DEC-0008/0009:
    `result=PASS_CONTINUOUS_AUDIO drop=ok duty=0.735 audioSeconds=5.88 spanSeconds=8`
    `targetVcw=396 fed=294 emitPcm=294 p2macCrc=122`. Same IQ slot=1: duty=0.11
    (companion not mixed into selected). Before those DECs: `PASS_PARTIAL_AUDIO`
    drop=D duty=0.28 (sticky lattice on independent block eyes; 160 ms sustain).
- **Measured 2026-09-07 live capture `20260907_073304` (146 s gapless) + HEAD voicetest after DEC-0013:**
  - Live: some audio better; operator still heard garbled + repeats. CADENCE TG 10330 17:34:54–58 dups=78–100/s feedRatio=0.29; seq=389 ctxVcw=10 ctxDrop=0 after seq=386 emit=18 (overlap replay). TG 30302 seq=9 opp=16 p2mac=0/2 gate=emit (mixed MAC-dead garble). Later TG 30302 dutySec=1.28–1.36 with dups=0 (overfill).
  - Voicetest HEAD after DEC-0013 (ISCH lattice de-dupe, not lock-only):
    - 105622 TG 30003 slot 0 skip=97334 8 s: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685` (was 0.735 before lattice; still ≥0.65).
    - 073304 TG 10330 slot 1 skip=107597 8 s: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.76 timelineOk` `fed=304 dupSuppressed=512` (repeat slice; no overfill).
    - 073304 TG 30302 slot 1 skip=771 8 s: `PASS_PARTIAL_AUDIO duty=0.41` (short first call + hang).
    - 073304 TG 30302 slot 1 skip=128684 8 s dual-TG with 20202: duty=0.715 but `concealmentOk=no` (plc=76/286). Mixed-epoch garble remains T-0004. Post-emit companion-louder skip is DEC-0012.
- **Measured 2026-09-07 live capture `20260907_095450` (93 s gapless, SNR ~5.4 dB):**
  - Dual grant same RF: TG **30013 SLOT=1** + TG **30003 SLOT=0** at 420.225 MHz.
  - Operator: slot bleed, wrong cadence, timing issues.
  - First eye 720 ms `dsp=327 ms` then seq=2 `decode-wall-timeout`. Later hops
    160–360 ms `dsp=159–573 ms`. seq=21 `targetVcw=10 oppVcw=10 fed=10 ctxDrop=0 p2mac=0/2`.
  - First CADENCE `windows=593 dutySec=2.320` dumped 53 s as `1s` (`lastLogMs==0`).
  - Voicetest after DEC-0014 revert (block-channelize default): TG 30013 slot 1
    skip=3741 8 s `PASS_PARTIAL_AUDIO duty=0.625` `oppAmbe=759/920`
    `plc=180/290 concealmentOk=no`. Companion TG 30003 slot 0 same skip
    `duty=0.58 oppAmbe=719/872`. Isolation does not hold on this dual-TG RF
    (T-0004). Default-on streaming DDC was rejected by 105622 duty 0.095.
- **Measured 2026-09-07 live capture `20260907_115315` (162 s gapless, SNR ~17 dB):**
  - DEC-0015 two-channel LO: TG 30003 420.725 offset **261.3 kHz**; then 421.975
    `rfCenter=420.97773` offset **997.3 kHz** (voice at usable-half edge).
    CADENCE `drop=D dutySec=0.22–0.45` then `drop=A` no-vcw / no-sf-mask.
    TG 30302 421.225 reused that LO (`existing-wideband-source-low-if`) almost
    all `drop=A`. Operator: audio really bad. DEC-0016 parks voice, not the set.
- **Measured 2026-09-07 live `22:27:17`–`22:28:20` (no IQ file):**
  DEC-0016 LO ok (TG 10128 420.725, center 420.713751, offset 11249 Hz).
  Gaps are extract: 29 hops `iqRej=88/212`, emit 102, **3.8 s PCM / ~36 s**,
  gate `hard-soft-quality-low` / AMBE rejected. Drop **A**. Need start/stop
  capture to voicetest; do not retune a quality threshold from this listen.
- **Measured 2026-09-07 live capture `20260907_123525` (258 s gapless, SNR ~15 dB):**
  DEC-0016 LO ok on every follow (offset 11.2 kHz). Operator: all emits gappy,
  some clearer. Live CADENCE: 88 emit-seconds, median duty **0.34**, drop **D**
  on talk (`fed≈emit`, `dups` ≈ 0.58 of `target`). 80 ms fresh + 280 ms overlap
  hops; ring bridge + underruns. File voicetest TG 30003 slot 0 skip=20000 8 s
  `center=417.66375`: `PASS_PARTIAL_AUDIO drop=ok duty=0.64` `fed=emit=260`
  `gaps=15`; hops alternate selected emit vs `wrong TDMA slot`. SigMF meta
  still says 420.475 after retune — use the parked `--center` for replay.
- **Measured 2026-09-08 DEC-0017/0018:**
  - DEC-0017 360 ms fresh-only block hops rejected (105622 duty 0.35).
    Constants restored to DEC-0009. Re-prove 105622 `duty=0.685`.
  - DEC-0018 streaming lattice jump skipped; env=1 still duty 0.16.
    Default-on still rejected (DEC-0014). Live hole remains drop **D**.
- **Evidence already in tree (do not re-guess):**
  - README v0.2.50: clear Phase 2 ~50% of the time.
  - Baseline 20260810_134531: isolation PASS, continuity PARTIAL (`docs/P25_BASELINE_CLEAR_CONTINUOUS_20260810.md`).
  - Capture 20260808_034136: dual-slot MAC-dead + sticky ESS → blocky wrong-epoch PCM; mute raised silence.
  - Capture 20260809_004206: blanket MAC==0 mute dropped duty 0.155→0.055 on windows that had this-window ESS.
  - CADENCE/docs: high `dup`/`absDup`, `feedRatio` collapse, `no-vcw-from-live-window`, ring underruns between 80–160 ms islands.
  - `docs/P25_FULL_AUDIT_2026_07.md`: p2sf/p2mask high, p2mac=0, decoded=0 → mask epoch / ACCH, not “no grant”.
- **Must not invent:** a new grace/TTL/minFresh, PLC PCM, or “open the security gate a little” until REQ-P2.0 names the bucket on a current run.

## ISS-0002 — String-only `verify_p25_phase2_*.py` treated as continuity proof

- **Status:** closed
- **Opened:** 2026-09-07
- **Closed:** 2026-09-10 — Process locked in `DEVELOPMENT_RULES.md` §1/§8 and
  `docs/CODE_NOTES.md` (verify scripts = invariant locks only). SoT checkboxes
  require voicetest/CADENCE, never string presence. Definition anchors via
  `definition_body` (`084ab27` + follow-up).
- **REQ:** REQ-P2.0 / DEVELOPMENT_RULES §8

## ISS-0003 — Dead 180 ms speaker catch-up constants vs live planner

- **Status:** closed
- **Opened:** 2026-09-07
- **Closed:** 2026-09-10 — Confirmed unused by `p25Phase2PlanVoiceDecodeChunk`
  (speaker path = sustain 80+280; backlog = BacklogCatchUp*). Removed
  `kP25Phase2VoiceDecodeSpeakerCatchUp*` from `P25VoiceTiming.h`; verifiers
  updated to lock absence + sustain/backlog SoT.
- **REQ:** REQ-P2.4 (later)

## ISS-0004 — P25 orchestration lives in a ~34k-line `main.cpp`

- **Status:** closed
- **Opened:** 2026-09-07
- **Closed:** 2026-09-10 — DEC-0040 mechanical split on `refactor/iss-0004-split-main`
  (`P25VoiceTiming` / Registry / AppGlobals / RollingIq / VoiceDecode / VoiceTest /
  CliApp / AppBootstrap / MainWindow; `main.cpp` ~2k leftovers + `main()`).
- **REQ:** T-0009
- **Follow-up (2026-09-10):** Phase A leftovers extracted (`P25VoiceSession` /
  `P25DecodeConfig` / `DemodModeUtils` / `SavedFrequencies`; `main.cpp` ~200).
  MainWindow out-of-line done (`MainWindow.h` ~520 decls; bodies in
  `MainWindow.cpp` + `MainWindowP25Voice.cpp` + `MainWindowP25Orchestration.cpp`
  for live voice worker / submit / rolling-IQ pipeline). Mega-ctor DSP extract
  closed under **ISS-0010**.
- **Must not invent:** a rewrite in the same commit as a feed-gate change.

## ISS-0005 — Product docs claimed continuous audio done while field audio is partial

- **Status:** closed
- **Opened:** 2026-09-07
- **Closed:** 2026-09-07 — `docs/P25_AUDIO_PRODUCT_ROADMAP.md` item 1 marked open; SoT L3; README points at SoT.
- **REQ:** REQ-0.1

## ISS-0006 — TIA-102 Phase 2 specification not in-tree

- **Status:** open
- **Opened:** 2026-09-07
- **REQ:** SPEC_INDEX
- **Unknown:** we do not have a licensed TIA-102.BAHA/BAHB (etc.) PDF in this repo. Implementation cites SDRTrunk and OP25 under `_codex_refs/` plus field captures.
- **Must not invent:** bit offsets, LFSR taps, or MAC opcodes from memory. If SDRTrunk and OP25 disagree, open a DEC with both file paths; do not average them.
- **Unblock by:** operator-supplied spec excerpts, or a DEC that names the SDRTrunk class as the cited implementation for that field.

## ISS-0007 — No golden IQ fixture in this clone for voicetest on HEAD

- **Status:** closed
- **Opened:** 2026-09-07
- **Closed:** 2026-09-07 — used AppData `20260905_105622` TG 30003 slot 0
  skip=97334; `PASS_CONTINUOUS_AUDIO` on HEAD after DEC-0008/0009.
- **REQ:** REQ-P2.0 gate “at least one IQ or live run”
## ISS-0008 — Session cadence / tail-grace ownership spans multiple TUs

- **Status:** closed
- **Opened:** 2026-09-10
- **Closed:** 2026-09-10 — CODE_NOTES ownership table + explicit SoT sentence:
  adaptive cadence/tail/streaming-DDC helpers → `P25VoiceSession`; named
  constants → `P25VoiceTiming.h`; mirror atomics → `P25AppGlobals` (`084ab27`).
- **REQ:** maintainability / clear-audio diagnosis

## ISS-0009 — Verifier first-occurrence anchors break after out-of-line moves

- **Status:** closed
- **Opened:** 2026-09-10
- **Closed:** 2026-09-10 — `definition_body` / `require_definition` in
  `p25_orchestration_sources.py`; 14 high-risk verifiers migrated; full batch
  129/129 (`084ab27`).
- **REQ:** invariant locks

## ISS-0010 — MainWindow constructor still owns ~7k lines of timer/lambda DSP

- **Status:** closed
- **Opened:** 2026-09-10
- **Closed:** 2026-09-10 — Extracted `MainWindow::startP25LiveDecodePipeline()`
  into `src/MainWindowP25Orchestration.cpp` (~1.4k lines: rolling-IQ / chunk
  plan / submit / CADENCE). Ctor calls the named method; UI/diag timers remain
  in ctor (`084ab27`).
- **REQ:** maintainability / clear-audio diagnosis

## ISS-0011 — Dual live paths: GUI voice worker vs CLI/voicetest

- **Status:** closed
- **Opened:** 2026-09-10
- **Closed:** 2026-09-10 — CODE_NOTES "Live GUI vs CLI/voicetest ownership"
  map names policy/session/decode owners and requires both paths call the same
  helpers — no duplicated constants (`084ab27`).
- **REQ:** maintainability / clear-audio diagnosis
# Release hardening (DEC-0090 / T-0027)

Observed: release helper ignored native failure status, hard-coded master,
and signing rewrote the public trust anchor after building. Corrected with
checked commands, current-branch publication and matching-key enforcement.
Package/negative gates now PASS (BUILD_NOTES). Initial verifier used a Python
API absent on this host; replaced with streaming hashing before publication.
# SSTV VIS milestone (DEC-0091 / T-0022)

Initial fractional-rate consecutive-header fixture ended one sample too early
at 11025 Hz. Corrected fixture tail; all rate/partition cases pass with unchanged
detector timing. Independent M1 header passes. Image reconstruction, live routes,
GUI, weak/faded/headerless acquisition and narrow/extended VIS remain unimplemented.
QSSTV/Python reference licenses were reviewed; no backend code or audio is shipped.
Unicode SSTV paths initially failed in both narrow file opening and CRT batch
arguments. Qt argument ingestion plus wide-file opening passes the actual CLI
test; existing RDS/tone/registry CLI regressions pass. Other legacy file loaders'
Unicode behavior is not certified by this scoped repair.
