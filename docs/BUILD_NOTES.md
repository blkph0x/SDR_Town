# Build notes

## 2026-10-03 - T-0102 empty-poll follow-up

After source push `90edc4a`, tracing the HF read/reset contract exposed a missed
edge: an empty post-overflow read set lastSeenStreamEpoch before any samples
arrived. Added fixture fails one assertion (epoch 4 acknowledged instead of
remaining at 3). Moved acknowledgement after the empty-return check; retains
existing producer epoch and retune anchoring. Rebuilt app, core tests and
Inmarsat GUI target. All **16/16 CTest suites PASS**, 71.21 s; core **240879
assertions PASS**; dedicated loss fixture has 34 assertions. Same optional
external-fixture skips as the previous run. Guard mutation tests pass again.
DTMF CLI and RDS CLI reference/parity/malformed-file smoke also passed after
the first full build. No RF transmission or new field-voice claim.

The first source run 37111242368 was still building when the additional
reproduced fix was prepared; qualification must use the final source commit,
not describe the superseded run as a successful final gate.

## 2026-10-03 - T-0102 local hardening regression

Host: Windows/MSVC 17.14, Qt 6.11.1, existing vcpkg/Soapy/mbelib toolchain.
Built Release SDR_Town, sdr_town_tests, sdr_town_workspace_tests,
inmarsat_live_gui_tests, remote_diagnostics_tests and antenna_control_tests.
`ctest --test-dir build -C Release --output-on-failure`: **16/16 PASS**, 73.04 s.
Core: 491 passed / 2 external-fixture skips; **240875 assertions PASS**.
Workspace: 45 passed / 6 optional recording/private-alias skips; 956 assertions.
Diagnostics/control: **184 assertions / 16 cases PASS**. Fake-driver lifecycle,
Inmarsat GUI, aircraft, rotor and SSTV helper suites pass. No on-air TX or new
live P25 acceptance is claimed; existing live receive sessions were not opened.

Hardware-loss fixture first reproduced five failures against the old path:
no epoch advance, old samples still exposed, 2048-sample mixed window and wrong
start index. Repair passes initial and repeated loss/two-cursor tests; timeouts
do not increment epoch. Tone mock rejects unauthorized and failed configuration
with no activation/writes and no leaked fake handles; accepted mock path writes.
`python scripts/test_no_p25_guard.py`: PASS, including DEC-0171 exact-patch
allowance and negative mutations/wrong path/reverse patch. Shared RX is changed
only at proven overflow; P25/FM/vocoder/audio engine implementations unchanged.

Binary publication remains blocked by ISS-0060 exact-artifact notice/source
coverage. Source Actions result will be recorded after push; no release claimed.

## 2026-10-03 - T-0102 regression-first infrastructure hardening

Baseline d47f000; Windows/MSVC17.14, Qt6.11.1, existing vcpkg build. Before edits,
all16 existing CTest suites PASS in71.13s. New production-control-server fixtures
first FAIL8 assertions: stop retains accepted socket, invalid/duplicate lengths
and chunked framing dispatch commands, JSON precedes auth, exception leaks.
After bounded control repair all of those plus client-cap/deadline/restart/
single-dispatch tests PASS. Status HTTP fixtures first FAIL3 assertions:
oversize JSON accepted, trickled response outlives deadline, callback after
opt-out. Bounded reply+epoch cancellation repairs them. RemoteDiagnostics now
PASS in1.57s including late-opt-in/session-reset health scheduler tests.
No real network service or RF transmitter was used. Full-build and release
gates remain pending; these results do not qualify hardware or P25 audio.

## 2026-10-03 - T-0101 reference attribution correction

Baseline d0443b4. Rechecked `git log --follow -- include/P25SdrtrunkTune.h`,
the added file at 3e9573f, and DEC-0015/0016. The comment/reference math are
evidence, but the previous confirmed-code-derivation conclusion was not proven.
Recorded maintainer clarification; no certification of the full source history.
WIPO and US Copyright Office primary guidance confirms the idea/expression
distinction. 146 local Markdown links resolve; only Markdown files changed;
LICENSE.txt and all application/upstream files are unchanged. P25 guard
self-tests PASS. CI is still required for publication; no binary/version bump.
Prior documentation commit d0443b4 passed Windows 37101326431 and YAML
37101326413. Those green builds did not prove its copyright attribution.

## 2026-10-03 - T-0100 licensing/documentation checks

Baseline 2ab78d7; no application, build, dependency, version or workflow edits.
LICENSE.txt grant body matches the retained SSTV standard MIT text after
whitespace normalization (holder/title independently checked); OSI primary
text also reviewed. 146 local Markdown links across the credits, scope,
README and release documentation resolve. `git diff --check` PASS. Existing
`python scripts/test_no_p25_guard.py` positive/negative self-tests PASS.
CI remains the source-publication gate; this entry does not predeclare its
result. No new binary or live RF test is warranted for documentation changes.

Compared source inventory with the previously verified anonymous v0.2.122 ZIP
(SHA256 4f83076f02398865eadde5babc05f96a533e1d27cfb24709898f415c67161549).
Confirmed incomplete notices (ISS-0060), and compared P25SdrtrunkTune against
upstream GPL CenterFrequencyCalculator (ISS-0059). Correction under DEC-0170:
that comparison did not prove copying of protected expression; the P25-specific
exception/block was withdrawn. Actual third-party terms and notice remediation
remain applicable. Existing assets/notices/submodules are unchanged.

## 2026-10-03 - T-0099 public release verified

Source 60258a7f63f16e48247f1b106e73f6ffc4dbf0d6. Windows master/release runs
37097604524 / 37097604489 PASS; YAML 37097604525 / 37097604488 PASS.
Public non-draft prerelease:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.122-experimental
Anonymous ZIP SHA256:
4f83076f02398865eadde5babc05f96a533e1d27cfb24709898f415c67161549
Verified source/version/workflow provenance, EXE/SDRplay hashes, Morse license
and authenticated HTTPS collector configuration with opt-in OFF (no token
printed). Downloaded EXE passed the DTMF short/repeated/polarity/inverted/
shifted/EOF/Unicode/invalid-option suite and RDS reference/parity/negative tests.
Actions independently verified the public asset and these same CLI gates.
Local final build and 16/16 CTest passed; field RF and speech talk-off remain
unqualified. No capture or decoded tone sequence was uploaded by this task.

## 2026-10-03 - T-0099 reproduction and development gates

Baseline ab21a4f; Windows/MSVC 17.14, existing Qt/vcpkg build. New executable
regression cases first FAILED: weak pair in white noise reported purity=1;
23 ms transient confirmed one digit. After DEC-0168 correction both PASS.
Initial expanded DTMF suite: 14 cases / 4691 assertions PASS, including five
sample rates, all 16 keys, fast/inverted/polarity and chunk/epoch tests.
Workspace DTMF tests: 2 cases / 20 assertions PASS (real generated mono WAV,
Unicode path, GUI/direct sample timestamp parity, EOF, cancellation/error,
profile application and inactive source). Further RF/negative/release gates
remain in progress; no physical transmitter or field acceptance claimed.

Expanded core gate now 18 cases / 5943 assertions PASS, including all-key
40 ms +/-1.5 percent fixtures, arbitrary tone phase, bounded overflow and dual
RF. Dual NFM simulation: 0.30 s at 2.4 Msps, 0.1185 s synthesis+DSP wall time;
"11" output and "55" input, one epoch each, no cross-channel digits. CLI
short/repeated/polarity/inverted/shifted/EOF/Unicode/invalid-option smoke PASS.
Incremental full rebuild hit LNK1104 on SDR_Town.exe while the CLI smoke was
using it; stop overlapping EXE tests with link, retry after tests finish. This
is a build coordination failure, not a decoder assertion. Full gate follows.

Final complete build PASS (`build/dtmf-build-verified.log`); final CTest 16/16
PASS in 72.97 s (`build/dtmf-ctest-final.log`). DTMF CLI and unchanged RDS smoke
PASS against rebuilt EXE. Exact P25 guard positive/negative mutation tests PASS.
GUI actual-file tests pass; rendered 820x520 and compact 540x480 windows checked,
minimum table height corrected after the first compact render clipped its row.
The native desktop capture helper timed out twice despite a responsive process;
do not represent the offscreen/widget renders as a successful native mouse test.
Publication/anonymous shipped-asset verification completed in the entry above.

## 2026-10-03 - T-0098 public release verified

Source 1d5b65a4b189bcb6ad039e1e183fbede8ed98264. Windows master/release runs
37089361295 / 37089361330 PASS; YAML 37089361178 / 37089361430 PASS.
Public non-draft prerelease:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.121-experimental
Anonymous ZIP SHA256:
395d4b0915b264ce618dbaf8afa233d2b8129561854700882801131849b0407a
Embedded version/source/workflow and EXE/SDRplay DLL hashes verified. Morse MIT
notice present. Authenticated HTTPS collector defaults present, opt-in OFF;
no credential printed. Shipped CLI help/bias-T and RDS reference/parity/negative
file tests PASS on this PC. Actions also passed core/GUI/device tests, replay
parity, SSTV, packaging and its independent public asset smoke.
Final local CTest rerun 16/16 PASS (71.50 s): core 476 passed/2 skipped, workspace
43 passed/6 skipped; hardware/fixture skips do not establish field acceptance.
Native default-size Morse window and exact WAV transcript visually checked.
Pointer resize did not change its size, so compact native layout is unverified.
No physical RF transmit, live CW, AM fading or P25 audio acceptance was claimed.

## 2026-10-03 - T-0098 full local qualification

Windows/MSVC 17.14 Release full build PASS; CTest 16/16 PASS (77.72 s).
After saturating the Morse startup counter and adding WFM RF coverage, rebuilt
SDR_Town/core/workspace targets PASS; `[cw]` 6 cases/34 assertions PASS.
`verify_hf_integration.py` PASS. Native Tools > CW window decoded the independent
20 WPM / 700 Hz PCM16 WAV to four exact `CQ DE VK2ABC` phrases, zero gaps,
one initial reset and zero rejects. During speech-free tone acquisition it
reported 703 Hz / 20.0 WPM; EOF silence naturally clears the pitch estimate.
Default window layout visually verified, no overlaps. Native RF/hand-keyed CW
and fading AM/SSB were not tested. Public release qualification is pending.

## 2026-10-03 - T-0098 reproduction and initial HF/CW gates

Windows/MSVC17.14, existing Qt6/vcpkg build. Baseline badcba4 was clean.
New `[hf][audit]` tests:2/2 FAIL before repair (accepted NaN block; reset on1 Hz
same-explicit-identity correction). After repair `[hf]`:17 cases/667 assertions
PASS. Initial compile failed because added state fields matched SincResampler
instead of State; corrected placement, rebuilt successfully.
Independent Morse fixtures plus RF NFM/AM/USB/LSB/CW,44100Hz recording input,
silence/gaps/invalids/cancellation: `[cw]`6 cases/32 assertions PASS.
CW Qt lifecycle/error tests:2 cases/7 assertions PASS. Guard positive/negative
mutations PASS. Full build/CTest/publication evidence follows after completion.

## 2026-10-03 - T-0097 public paired release verification

Town source 6dc66fa08f8554ce14cd7e438a0a7e657c77e76d: Windows master/release
37079654049 / 37079654265 and YAML 37079654043 / 37079654239 PASS.
Public non-draft prerelease:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.120-experimental
Anonymous downloaded ZIP SHA256:
16fe68dfebe3db8bfcbd437c5f5fffd3764de724da57bf1d939ce498f874733f
Embedded version/source/workflow and EXE/SDRplay module hashes verified.
Shipped CLI help/bias-T and RDS reference/parity/negative checks PASS.

FUBAR source d9bcb0a0e819912db5be84a1f7d9ec46077d655c: main/tag builds
37081402863 / 37081405864 and release 37081407222 PASS.
https://github.com/blkph0x/FUBAR/releases/tag/v1.1.44
Anonymous downloaded ZIP SHA256:
1a52483abacee08a8435072e4f2592c8d5495736113a9a8d7602281dd2bab6af
Source/version/workflow/EXE hashes verified; control DLL equals Town's shipped
DLL (c634ef3e4df2746283062b08a808f6b95011a7cd804a5dfdd64a60cee2e1c50e).
Shipped FUBAR CLI/WAV/website/live-stream/listener-slot self-test PASS.

Manual browser playback now resumes at 9.65 s from a 9.57 s pause, not zero;
invalid tune input leaves RF unchanged and error visible across workspace tabs.
Both native apps remain running, browser returns to Radio with no active lease.
No new RF/voice quality claims. Town outputs to speakers, FUBAR captures VB-CABLE:
live audio routing must be paired intentionally. Public community directory
GET timed out at 15 s, consistent with native HTTPS announce failed; local web
and bridge remain functional. No network/privacy configuration was changed.
Native compact-height FUBAR pointer resize was blocked by a Windows picker
overlay; physical scrolling acceptance remains open. Qt compact-layout tests pass.

## 2026-10-03 - T-0097 desktop and companion usability

Windows/MSVC Release local build passed; initial full CTest 16/16 passed.
P25 exact-patch negative tests pass. New workspace GUI fixture covers selector,
panel reopening, retained widget values, compact layout and persistence.
Real browser control lease + 98.1 MHz WFM tune confirmed in the Town desktop.
Mode/frequency drafts survive polls and tab switches. Browser walkthrough found
the Inmarsat map missing-success-flag issue; repaired with a GUI regression test.
Final local Windows/MSVC Release build and full CTest 16/16 PASS (68.67 s),
including the new map test. Final FUBAR build and CTest 3/3 PASS. Browser checks
at 1280x850 and 390x844 found no page overflow in any workspace tab; actual
Inmarsat loopback map response now returns HTTP success with ok=true, not 502.
Disconnected receiver disables commands; releasing the lease disables all
mutating workspace controls. FUBAR native Tab traversal confirmed. Physical
compact-height FUBAR scrolling remains a field check; Town compact layout is
covered by Qt tests. Existing speaker/VB-CABLE routing was not changed, so no
end-to-end live-audio claim is made. Public Actions qualification pending.

## 2026-09-28 - T-0095 public release verification

Source 91b455a7033bb7c85415c39451909d5de6f6c641. Master/release Windows Actions
36401138823/36401138563 and YAML 36401138699/36401138592 all PASS. Release:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.118-experimental
Public non-draft experimental portable ZIP downloaded anonymously and verified:
SHA256 `fcea31ff76be24994a9c5a6cf958cf2efbac152d85ae82915c333d23394cde30`;
EXE `bbb27b1c9339ee66aa7b559f99adc1c266f0dccb64d5fd06f0b26f5c5ad02e19`;
SDRplay module `3a2b3d3c1f14cbf413277ab772344afe60768cab917daa527218297c19658337`.
Embedded source/version/workflow and binary hashes match. Shipped diagnostics
defaults remain disabled with the existing public HTTPS collector endpoint.
Signed stable updater release remains v0.2.96, not replaced by this portable.

Downloaded executable: CLI help/bias-T, RDS reference/parity/negative checks,
reviewed-recording valid/malformed checks, real 10500 GUI/CLI IQ replay all PASS.
Each map path produces eight validated units, four position reports and two
aircraft with Classic Aero ICAO fields. Both 8400 paths retain exact v116/v117
WAV SHA256 `b1d4c75b92ece9280769de46fc3d0f3d224c990d40ee189e0f4afa7e74659282`.
This is the private five-second analytic-IF reconstruction, not full tuner IQ;
no private recordings, aircraft IDs or transcripts published. Local final gates:
72 Inmarsat cases / 23442 assertions and full 16/16 CTest PASS after deadline
repair. Collector 6/6 PASS, bounded numeric HTTPS synthetic report persisted.
Physical live call/aircraft association and this installation's RF positions
remain field acceptance gates. P25 and shared RF/audio unchanged.

## 2026-09-28 - T-0095 CI expiry boundary repair

Release Actions 36398159214 built successfully but stopped before publication
on one new unit assertion: retained position did not expire at receipt + 1200.
467 cases passed, one failed, two explicit external-fixture skips. The age test
used `now-received < ttl`; fractional monotonic receipts can produce
1199.9999999999998 at an exactly computed 1200-second deadline. Add a fixed
5937/7-second receipt case to reproduce locally, then compare `now < received+ttl`
instead. Keep the strict boundary; no epsilon, relaxed test or duration change.
The deterministic case reproduced locally before the repair (two assertions
failed: position expiry and identity eligibility); this is not a speculative
CI-only change.
Rebuilt app, unit and GUI targets PASS; 72 Inmarsat cases / 23442 assertions
PASS after repair. Original master Actions 36398159249 PASS; release run failed
closed before assets were published. A labelled synthetic numeric-only map
report was also posted through authenticated public HTTPS and verified in the
collector's JSONL. No aircraft IDs, coordinates, speech or decoded text uploaded.
Complete Release CTest after deadline repair: 16/16 PASS, 66.25 s, real reference
IQ enabled. Re-run Actions on the repaired source before publishing.

## 2026-09-28 - T-0095 qualification and consent test repair

Windows MSVC 17.14.40 / Qt 6.11.1 Release build of SDR_Town, unit, Inmarsat
GUI and Workspace targets PASS. Inmarsat unit selection: 70 cases / 23431
assertions PASS. Actual local GUI/CLI reference replay: eight validated units,
four position reports, two mapped aircraft, both ICAO fields equal validated
Classic Aero addresses. GUI/CLI 8400 WAV SHA-256 remains
`b1d4c75b92ece9280769de46fc3d0f3d224c990d40ee189e0f4afa7e74659282`.
RDS CLI and reviewed-recording negative gates PASS. First full CTest run found
new consent test used QMessageBox::done instead of activating its standard
button: Yes was not recorded, then a further click waited without an answer.
Repair the test to click the real Yes/No button and require acceptance before
continuing. No production consent gate or timeout is weakened. Rerun required.
Second run exposed unspecified argument evaluation order in new test setup:
`setRf({received()}, now())` could sample `now` first, so strict future-receipt
validation correctly dropped the row. Seed receipt and snapshot in separate
statements; add pointer assertions before fake reply use. The GUI controller
also now takes its immutable RF snapshot before its monotonic timestamp,
avoiding a transient future-age rejection during a concurrent new reception.
Third run: pre-existing live busy-traffic map test failed. Hidden receiver
snapshots were skipped, and the recent-identity-only view could evict a retained
position after 750 other identities. Keep hidden presentation snapshots current
without network/logging; merge the two original independently bounded registries
under one store lock, preserving monotonic receipt ages. Original retention test
is unchanged; add a focused union/TTL regression. Full rerun required.

Final local build PASS; 71 Inmarsat cases / 23439 assertions PASS. Complete
Release CTest 16/16 PASS (64.80 s), with real burst IQ reference enabled for
Workspace, plus live GUI lifecycle/consent/hostile API tests. Compact 560x360
and desktop 1100x650 map PNGs inspected; green/blue/estimate pixel assertions
and 512-marker bound PASS. Existing busy-traffic map retention gate unchanged
and passing. P25/shared RF/audio guard: zero protected modifications. Python
collector suite 6/6 PASS; updated collector restarted without proxy/firewall
changes and public HTTPS health reports OK, allowance 15. Actions/public asset
verification pending. Private fixtures/audio stay in ignored build/.
Three further InmarsatLiveGui repeats PASS (12.26 / 12.27 / 12.17 s), including
the real ten-second network deadline and Yes/No consent interaction.

## 2026-09-28 - T-0095 implementation qualification (in progress)

Initial Windows MSVC Release build compiled the application, then caught a
missing initializer brace in the new synthetic multi-call test (C2059,
test_inmarsat_tracking.cpp:114). Replaced the deeply nested initializer with
explicit structured assignments; no runtime gate was weakened. Full rebuild
and functional/public-release gates pending.

## 2026-09-27 - T-0094 public release verification

Source 884c87d791d3d893d6618004dd17e2d3ae73865e. Windows master/release Actions
36317237872/36317237782 PASS; YAML 36317237855/36317237788 PASS. Public release
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.117-experimental is a
non-draft prerelease; latest signed updater remains v0.2.96. Anonymous download
ZIP SHA256 `bedb9c53721f2be5a3de8475ab8da4e84e928f020410ad70172636c9793b45f6`;
executable `00e583b57fb3090f175a69edeb47201e921dd923f2127aedd4d11571836e650e`;
SDRplay module `55613a347aaac2f3fb52b9e2f1d9e09bb2156746dddbc55eceffbbbaaa0e5ee6`.
Embedded version, source commit and workflow run match. CLI help/bias-T, RDS
and diagnostic recording positive/negative checks PASS on the downloaded EXE.
Shipped CLI and GUI independent 10500-burst replay both finish with eight
validated frames, four position reports and two map tracks. Submitted 8400
analytic-IF reconstruction (not full tuner IQ) yields byte-identical WAV in both
paths to v0.2.116: `b1d4c75b92ece9280769de46fc3d0f3d224c990d40ee189e0f4afa7e74659282`.
Three private 10500 bundles again produce ground-to-air counts 4/3/1, zero ADS-C
or accepted positions on cold replay. Public collector health still confirms 15.
Private recordings/results remain local under ignored build/. No online lookup,
RF acceptance or remote marker provenance is claimed by these verification gates.

## 2026-09-27 - T-0094 final local qualification

Windows Release rebuilt successfully (t0094-layout-build.log). Full CTest with
the independent burst IQ enabled: 16/16 PASS, 53.57s (t0094-verified-ctest.log).
Whole Workspace suite repeated five times PASS; separate reference-to-GUI map
case repeated 20 times PASS. Four accepted position reports produce two tracks.
Collector 6/6, diagnostics packaging 1/1, release verifier 16/16 and recording
CLI tests PASS; P25 freeze guard passes with no protected source changed.

An earlier optional GUI reference run reported non-complete state without a
readable QString value. Added snapshot INFO output; this did not recur in the
repeated tests. Cause remains unproven (ISS-0049), not a repaired decoder defect.
A later attempted headless run exited 0xc0000409 before test output. CDB confirmed
Qt could not load the requested offscreen plugin: this deployment contains only
qwindows.dll. Remove that test-only environment override; do not alter decoder
code or the Windows package to conceal a test-launch error. CDB evidence is in
ignored build/t0094-cdb.log. No physical SDR/remote marker source acceptance
is inferred from these local passes. GitHub/public asset qualification pending.

## 2026-09-27 - T-0094 10500 receipt and map evidence

Confirmed cfef9adc91cd498183b32da77b3c4c91 plus cd0e5a9c/570087e1 on the
collector after allowance deployment. Each five-second live snapshot has +260
good/zero bad CRC SUs. Cold replay completes 4/3/1 messages respectively, all
ground-to-air block IDs: seven empty ACKs and one A4 ground-service message.
Zero ADS-C, accepted positions, application failures or identity mismatches.
Cold CRC failures are acquisition; they are not the live CRC measurements.
Two earlier new submissions also yield ACK/MIAM ACK, not position reports.
Original 10 Msps IQ is only 1.6384 ms; the five-second full reproduction is modem
IF, not complete wideband RF. Private bytes/text stay under ignored build/.

Added scalar direction/ADS-C/position counters to existing telemetry and
before/after recording snapshots. Quota-only full CTest initially 16/16 PASS
(55.45s). Expanded reference run then found two test-expectation errors: an
empty initializer constructs null JSON rather than an empty report object, and
the independent burst has FOUR accepted reports for TWO aircraft. Direct-IF
probe confirms reports on AES 3958038 three times and 4195750 once; correct
those assertions, not the parser/acceptance rules. Final qualification pending.

## 2026-09-27 - T-0094 upload allowance deployment

Collector tests 6/6 PASS after raising allowance to 15. Tests cover the 15th
acceptance/16th rejection, restart persistence, one rolling-day expiry, retained
request-rate/format/auth guards and public health policy. SQLite backed up via
SQLite backup API before restarting only the verified collector process on 8787;
existing credentials/configuration and recording history preserved. Public
https://gearsqueens.online/sdr-town-diag/health returns ok=true and
recordingsPer24Hours=15. Existing clients may resend without an app update.
The affected installation had four accepted recordings; no new 10500 payload
had reached storage when the user requested this change. Do not infer position
parser/map failure from a recording which has not arrived.

## 2026-09-27 - T-0093 submitted 8400 live speech

Received bundle 086bf146 from v0.2.116, five seconds of 48 kHz modem IF and
40000 live PCM samples at 8 kHz. Recording counter deltas: input +240000,
CRC good +30/bad +0, PCM +40000, codec corrections +9, combined M/E/T flags +8.
Do not describe all eight as erasures: InmarsatVoiceEvidence combines mute,
erasure and tone flags. Live PCM has no clipped samples, peak 11416, RMS
-26.6435 dBFS. Zero 20 ms frames occupy [0,.02], [3.5,3.52], [3.78,3.88],
[4.2,4.22] seconds: 160 ms total, maximum contiguous 100 ms. These are decoder
PCM intervals, not proof of lost speech or a speaker underrun.

Existing project .venv-stt faster-whisper base.en CPU/int8 returns a coherent
English sentence with both beam 1 and 5; beam 5 no-speech probability .01094,
mean log probability -.35371. ASR is supporting evidence, not a human quality
score or physical speaker measurement. Transcript and recording stay private.
System Python STT was not usable (Torch/NumPy mismatch, faster-whisper child
access violation); the existing project-local backend works, no environment or
application dependency changes made.

Native direct-IF cold replay: eight C frames, two rejected, CRC 18 good/6 bad,
150 voice words/24000 PCM samples, seven corrections/seven combined mute flags,
zero repeats. It recovers the final three seconds; last-second correlation
with live PCM .99137. This is late cold acquisition, not a live two-second loss.
Shipped v0.2.116 executable with analytic IQ reconstructed from the submitted
IF (NOT the original tuner IQ): GUI/CLI fast/paced all match native direct-IF
PCM exactly. WAV SHA256
`b1d4c75b92ece9280769de46fc3d0f3d224c990d40ee189e0f4afa7e74659282`.
The original wideband IQ excerpt is only 16384 samples at 2.048 Msps (8 ms),
insufficient to reproduce the complete tuner/channelizer path for five seconds.

Matching live session telemetry around capture has continuous lock, zero
speaker drops/device failures, unchanged speakerZeroFill=10000, zero input
discontinuities and unchanged reset/rejected-C-frame/source-identity counts.
Average processing-time/input-time ratio ~.155, not a whole-PC CPU percentage.
No warnings in the examined two-minute window. No DSP, queue, vocoder or gain
change is justified by this sample. Longer speech, other sources and a known
reference rendition are still needed for broader acceptance.

## 2026-09-27 - T-0092 public release verification

Source 238d48fd835f29672f1f4598672a134e4a922e53; release Windows Actions
36311464893 PASS, release/master YAML 36311464826/36311464563 PASS. Public
release https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.116-experimental
is not a draft and is explicitly a prerelease. Signed updater latest stays
v0.2.96. Anonymous ZIP download 42,787,326 bytes, SHA256
`6da79b321f1b2922e27de59a033f98d171de228e914a172e1216ae47a2e1d03f`.
Executable SHA256
`f077b0477f6eea08a4821d183dd9b1ce00242188cfd8b40d3e2237aeacd9e766`.
SDRplay plugin SHA256
`457ab33e4da9d6804da0e9f561e1305f17d61efce3897f75bad36be18892216e`.
Version/commit/run and both binary hashes match embedded provenance; zlib,
Jansson, Aero codec and all application licence files exist. Shipped CLI help
exit 0, biastee command present, independent RDS black-box suite PASS.
Shipped 10500-burst reference: eight messages, five interpreted applications,
zero application failures and two positions. Shipped 8400 reference: 1375 words,
220000 PCM, WAV SHA256 identical to all four local GUI/CLI runs (audit linked
below). Private captures and credentials were not published. This is decoder
and playback-path evidence, not physical RF/clear-conversation acceptance.

Final CI review found AircraftGui was compiled into inmarsat_live_gui_tests but
omitted from the CI CTest selection. Add that selection in the documentation/
qualification follow-up, without changing application code or published assets.
Exact expanded selection passes locally: 12/12, 13.97 s. Follow-up master Actions
must complete before closing the user-facing delivery; source release Actions
already tests and publishes the exact application commit above.

## 2026-09-27 - T-0092 expanded application/identity qualification

DEC-0162 extends scope before publication. Added pinned libacars application
modules/ASN.1 and Jansson 2.15.0. Initial full-app integration compile failed
because the legacy ASN.1 MSVC header redefines int8_t in a C++ translation unit;
isolated CPDLC status behind a small C adapter rather than modifying ASN.1
math/platform code. Also exported libacars' required generated config include.
Rebuild PASS. First application run 138/139 assertions: old test expected MIAM
file-request metadata to be unsupported, which is now correctly parsed as
control; expectation changed to control, never decoded file content. New
independent CPDLC, compressed OHMA, ADS-C direction/identity/CRC tests pass.
Expanded full build initially found missing InmarsatMessageStore linkage in the
Workspace test harness after sharing identity handling with replay; added its
source to that harness. Next full build and CTest 16/16 PASS (55.98 s).
CLI/GUI fast/paced 8400 reference all byte-identical, 1375 words/220000 PCM;
see INMARSAT_APPLICATION_VOICE_AUDIT_20260927.md. Public 10500 burst gives two
positions/five interpreted applications with zero application failures. Final
additional fragment-safety/prose tests PASS (142 assertions / seven application
cases); final full Release build PASS and CTest 16/16 PASS (55.62 s). Release
verifier 16/16 PASS, frozen-P25 guard PASS with zero protected changes. Public
release verification remains pending at this checkpoint.

Final review found per-channel map stores unnecessarily retained message bodies.
Keep only identity fields in those private stores; live message log/sink retains
raw/application text. Initial CI runs for 01efe0a cancelled before publication
so this memory-bound repair can be included in the same unpublished version.
Memory repair full Release build and CTest 16/16 PASS (55.27 s). Additional
GUI default-output replay: zero overflow/device errors, one source transition;
see the audit for consumed/flushed/idle distinctions rather than claiming speech.

## 2026-09-27 - T-0092 field evidence and local qualification

Windows/MSVC 19.44, source baseline e63d1e7. Extended reference probe compiled;
three real IF bundles replayed locally. 30409b9e produces 104 CRC successes,
77 acquisition failures, five ACARS messages after cold start. New application
adapter decodes the MA payload as MIAM CORE v1 ACK and labels the four empty
ACKs. Other two clips yield 104/156 CRC successes but no complete cold-start
messages. Live five-second deltas +260/+0 are different from cold acquisition.
Private logs/PCM under ignored build/inmarsat-field-0092, never published.

New dependency installed using C:/vcpkg (stale environment path has no binary).
First MIAM compile failed for missing upstream version.h; added that support
header. Subsequent probe/app/test builds pass. Focused ADS-B/application run:
133 assertions / 17 cases PASS. AircraftGui via CTest PASS, screenshots 560x360
and 1100x650 inspected, no overlap. Direct GUI harness invocation initially lacked
Qt's platform-plugin path; CTest supplies the configured path and passes.
Release verifier Python 16/16 PASS; frozen-P25 guard 0 protected files changed.
Full Release build PASS (`build/t0092-qualification-build.log`); final CTest
16/16 PASS, 55.38 s (`build/t0092-ctest.log`). This includes native unit,
Inmarsat application/live GUI, AircraftGui, device and remote diagnostics suites.
Inmarsat band-plan and P25 guard negative tests PASS. No protected P25 source
changed. Public Actions/package verification remains pending at this checkpoint.

Existing scalar Mode-S throughput is not qualified for continuous 20 Msps:
the noise fixture's 6000 samples at 2.4 Msps took 0.021 s on this host for
0.0025 s of RF. Packet correctness at 20 Msps is covered; sustained physical
capture/decoding is a separate open performance gate (ISS-0047). Runtime gap
counters expose backlog loss. No detector-threshold workaround was introduced.

## 2026-09-27 - T-0091 public release verification

Final source 31235b890dafb0fd5283792f9baf3807c7a844ff. Final local full CTest
15/15 PASS (53.57s). Real GUI app.runtime includes allowlisted audio output,
volume/mute/ring/underrun metadata; observed queue/budget/network drops zero,
snapshot 3.0816 ms. This is idle telemetry validation, not physical RX acceptance.

GitHub Windows master 36303530136 and release 36303530038 PASS. YAML runs
36303530160/36303530036 PASS. Release workflow includes native suites, Python
checks, GUI/CLI parity, package validation, public download and shipped CLI/RDS.
Public release: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.115
Downloaded ZIP 42,546,521 bytes, SHA256
`dab0de525499453723edf0794921076451751db5240ca08d1cbf8da6251f5703`.
Executable SHA256
`a43223b0caa8a82386dfc971eb31f0aff528108118cd039992eae56b26e210d0`.
Embedded version/source/run and SDRplay plugin hash match. Packaged diagnostics
target public HTTPS, require opt-in, contain ingest credential but not admin
credential. Local shipped CLI help exit 0 / biastee command PASS; shipped Aero
survey data verifier PASS. Collector received shipped CLI app.system with
commit 31235b8 and workflow 36303530038 at 2026-09-27T08:00:01.622Z.

Workflow's tag-name heuristic initially marked this numeric tag non-prerelease
(already latest=false). Set GitHub release metadata explicitly to experimental
prerelease; assets/tag unchanged. Signed updater latest remains v0.2.96. Future
numeric portable releases must still verify their prerelease metadata. No real
RSPdx was available; affected remote tester reproduction is still required.

## 2026-09-27 - T-0091 opted-in diagnostics qualification

Windows / MSVC 19.44.35227 Release build PASS. Full CTest 15/15 PASS (53.33s).
New RemoteDiagnostics tests cover private-text exclusion, real Qt button signals,
consent withdrawal/re-enable, 32-action overflow, 1000-report owner queue and
5000-report cross-thread flood. Collector tests 6/6 and packaging test PASS.
P25 guard negative tests PASS; exact MainWindow telemetry/consent-only digest.
Collector SQLite backed up locally, service restarted with token files (not
command-line credentials); public HTTPS health and 2080 historical event index
verified. Local 0.2.115 app.system/app.runtime received through public endpoint;
CPU/RAM fields populated, first runtime sample cost 2.7992ms. Release validation
and physical RSPdx tester acceptance remain pending at this checkpoint.

GUI smoke: real Refresh button produced ui.intent=Refresh/press, ui.actions
batch count1, subsequent runtime state at HTTPS collector. Browser fixture
desktop1366/mobile390 screenshots inspected; mobile table initially compressed
payload columns, corrected to stacked full-width rows and rechecked. Filtering,
detail expansion and no page overflow PASS (test_diagnostics_dashboard.cjs).
Second full CTest 15/15 PASS (55.94s); final diagnostics rebuild/test PASS.
Collector unauthorized large-body fixture intermittently saw Winsock10053:
server rejects before consuming body. Authorization probe now uses a small body;
full-size valid upload/quota tests retained, collector6/6 PASS again. No new
dependency added; optional visual fixture uses the host's existing Playwright.
An additional whole-process opt-out launch was denied by tool policy; no claim
for that run. Config/menu/observer opt-out and cross-thread discard tests pass.

Final server payload inspection caught the existing sanitizer removing the whole
audio metadata object as though it were PCM. Corrected with an explicit scalar
audio-health allowlist (arrays/unknown fields still removed) and network-body
assertions. This finding does not alter the audio engine or speaker behavior.

## 2026-09-27 - T-0087 P25 diagnostic audit (no application rebuild)

Published v0.2.114 GUI 60s capture / 75s exit PASS, exit0, 60.032s IQ,
983564288 bytes, no capture overruns/resets/write errors. Capture and all
private voice/STT evidence stay under ignored build/. CLI same-IQ +4.5..20.5s
v113/v114 identical 272 frames / 5.44s output and WAV SHA256. Both partial,
not full continuous acceptance. +26..34s staleTG30302 produces zero PCM;
TG10120 produces 304/304 AMBE, 6.08s PCM, duty .76, valid clear ESS/MAC,
PASS_CONTINUOUS_AUDIO numeric gate and coherent local base.en STT sentence.
Offline CC +30..32s proves TG10120 grants existed in captured in-band RF.

Local unchanged traffic/follow suites PASS: 68 assertions/11 and 190/51.
Isolated production-cpp harness initially failed because C++17 cannot provide
std::span; corrected harness to C++20, MSVC19.44.35227 Release build PASS.
Harness reproduces all three invalid lifetime transitions documented in audit.
P25 guard since1f7740e PASS:73 paths changed, zero protected. No runtime fixes.
CLI logscan invocation with Python-only --json/--audit flags failed because the
CLI treats the tail as its path; direct existing Python helper invocation PASS.
Its slow-worker heuristic is not CPU-overload proof: measured worker median
80.916ms/160ms fresh input, qDrop/rDrop/seqDrop and producer drops all zero.
No physical transmitter, changed PPM/gain, forced-clear or unsafe slot flags.
GUI stopped normally; no SDR_Town processes left by the audit.

## 2026-09-27 - T-0086 continuous SSTV autosave

Initial workspace test link exposed its missing spdlog dependency; added the
existing imported target, then app/native/workspace/Inmarsat GUI Release builds
PASS. Full CTest 15/15 PASS (54.96 s), including Rust transport/backend tests.
test_sstv_worker.py PASS for Robot36/Martin1, manual/auto and complete/partial,
both stream worker and live GUI. GUI screenshot inspected at normal size.
Hidden archive regression on user Scottie1 WAV PASS (41 assertions): six images
plus 481 seconds silence, 1149 seconds total input, PNG/sidecars exist before EOF;
second run cancellation preserves all six. Parser test archives 100 events with
no retained pixels/metadata. Rolling-report filenames and GUI save-root/repeated
unique sessions pass. Paced virtual Soapy live NFM test produces complete Scottie1
320x256, 5310490 input samples; evidence build/sstv-autosave-virtual.
No physical RF proof or production P25/shared demod edits. Source
68a6586873b19d82b57c783247918c35f372268a published: Windows master/release runs
36297862530 / 36297862769 and YAML runs 36297862518 / 36297862738 all PASS.
Public v0.2.114 ZIP SHA256
e7d45f67c2d36b56d84c27cabb2acf5a4af00fb6b7fde5e0e03e55811500dcd3 verified;
embedded version/source/run, EXE and SDRplay hashes match. Shipped CLI help,
SDRplay status, RDS and Inmarsat survey tests PASS. Shipped CLI user Scottie1
WAV decode complete; shipped continuous helper five complete images over
556.717 seconds PASS. GUI normal/small screenshots inspected.
Extra legacy image CLI script fails on Auto Robot36's trailing partial in both
0.2.113 and 0.2.114, with identical hashes: recorded as ISS-0041, not a regression
or a passing gate. Existing GUI instance left running; offline checks explicitly
allow multiple instances and disable remote diagnostics/control server.
Prerelease is public with ZIP/checksum; stable latest remains signed v0.2.96.

## 2026-09-27 - T-0085 SSTV paced virtual device

Test harness build initially needed Qt6::Gui for QImage and a missing closing
lambda delimiter; corrected, then Release native and GUI targets PASS. Full
CTest 15/15 PASS (49.00 s). User WAV: file + fast auto/manual NFM complete Scottie1.
Paced 2.4 MS/s real DeviceManager/ring/live-session/helper: complete Scottie1,
33 previews, 269624000 simulated IQ samples; RGB MAE 0.64425/255 vs file output.
Late entry (first five seconds omitted), RF Auto: expected zero images PASS,
route searching, zero helper input samples.
Late-entry manual NFM/image Auto PASS: 248-row partial Scottie1, 5152481 helper
samples, rather than the Auto RF gate's zero samples.
No production code changed; fixture is synthetic RF and cannot verify physical
USB/antenna/tuning or GUI selection. Source b8bd4e089e5fe6651a45b1438b013b288d2a7a4f
published; Windows CI 36295831518 and YAML run 36295831513 PASS. P25 guard PASS,
10 changed paths, zero protected files. Test-only update, no new app binary.

## 2026-09-27 - T-0084 Inmarsat rejection/identity audit

Windows / VS2022 Release: SDR_Town, sdr_town_tests, inmarsat_live_gui_tests and
sdr_town_workspace_tests build PASS. Inmarsat filter: 23168 assertions / 50 cases
PASS. Full CTest: 15/15 PASS, 52.69 seconds. GUI map screenshot inspected;
identified-but-unlocated status visible without a fabricated aircraft marker.
Application GUI dry-run exit 0, ok=true, no errors/warnings. Recording reproduction,
remote collector four tests, and RDS CLI reference checks PASS.
GUI/CLI paced/fast replay first failed the old literal-true EGC codec expectation;
updated it to false plus physical_probe_only, then all four replay modes and
malformed-file rejection PASS. No failing tests waived. RF audio not claimed.
P25 guard PASS: 29 paths changed, zero protected files.
Source b915bfd3a30f11feef8e227467a7ccb46462a5d4; Windows master run 36292917845
and release run 36292917785 PASS; both YAML validations PASS.
Public https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.113 is non-draft,
experimental prerelease. Downloaded ZIP/source/run/version/module checks PASS.
ZIP SHA256: 49ef36de4275c9c017180fa12a01c522e02ddd466437d6a8c0c7dfe459fbb356
EXE SHA256: cff603e2c8ce17a45f61580d478a45329582363dc79084d12695bc492657a04c
SDRplay SHA256: 809dd35175cd68f462abdd6d5500e7644bfeb513baaa93d5d2fe1770f690141d
Downloaded app CLI/SDRplay status, Aero survey, RDS, recording reproduction and
GUI/CLI paced/fast replay parity PASS. Stable latest remains signed v0.2.96.

## 2026-09-27 - T-0083 bounded diagnostic recording

Collector tests PASS (validation, quota across restart, expiry, HTTP authority).
Synthetic CLI IF reproduction PASS: exactly 240000 samples, no invented PCM,
malformed input rejected. Initial public POST failed Apache 404, not collector.
Deployed matching collector (only SDR Town service restarted). Added recording
upload to dedicated /etc/apache2/sdr-town-diag.conf; backup before-v112 retained;
Apache configtest PASS and reload PASS. No other proxy routes changed; admin
API stays LAN-only. Synthetic public HTTPS upload and exact local admin retrieval
PASS receipt ae969ec26e43418285554873b8a70cb8. Packaged credential matches local
collector, value never logged. No private radio data used in service validation.
Release app/test builds PASS. New pipeline fixture initially failed compilation
(missing InmarsatPipeline.h include); repaired, then 21630 native assertions /
12 cases PASS. Full CTest 15/15 PASS (51.54 s). GUI consent dialog screenshot
inspected at 500x340: controls fit; capture/send disabled before consent/data.
Application GUI dry-run PASS with no errors/warnings. No live RF acceptance claimed.

Published source 8579852875211f5a05d379aa26cc42be5ad898b2. Windows master
36290344389 and release 36290344624 SUCCESS; both YAML checks SUCCESS.
Public: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.112
ZIP SHA256 b23b1653875857b49d5287b559abc673be6176cd0944f6f4902c42b97345763b
EXE SHA256 1b27dcedfba285f2cc9f96ed7598deb477ba2a312d7bdbf3620f4d7b379f31ea
SDRplay SHA256 470c1624300566f5c55a617293d247ca076238b9060c13b58eee8e59970a3bd2
Downloaded source/version/run and hashes verified. CLI help/status and five Aero
surveys PASS. RDS first failed due to user's open instance (not decoder failure);
file-only test now explicitly allows another instance and disables telemetry.
RDS, diagnostic reproduction/unpack and shipped GUI dry-run then PASS.
Public synthetic upload using downloaded configuration and exact local admin
retrieval PASS, receipt 85ecd41a8d944ea99bb18c6d1ea229cd. No real RF uploaded.
Public/non-draft/experimental verified. Latest explicitly restored to signed
v0.2.96 after GitHub reported v0.2.111; final latest API confirms v0.2.96.
P25 guard PASS: 24 paths changed, no protected files. User's running app untouched.

## 2026-09-27 - T-0082 Inmarsat identity hardening

Focused native/watch/replay tests PASS: 23040 assertions / 39 cases. Release app,
native and GUI test builds PASS. Full CTest PASS 15/15 (52.09 s); first invocation
with inherited QT_QPA_PLATFORM=offscreen stalled WorkspaceTests and was stopped.
Normal Windows environment passes both that test alone and the complete suite.
GUI dry-run self-test PASS (ok=true, no errors/warnings). Inmarsat GUI suite PASS;
360x220 map screenshot inspected: unlocated voice status fits without overlap.
Source: 96a10d632153d429b9bd2181a096e7314e71490b. Windows CI master 36287882575
and release 36287882996 SUCCESS; YAML master 36287882564/release 36287883001 SUCCESS.
Public release: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.111
Public ZIP and checksum downloaded; version/source/run provenance verified.
ZIP SHA256: e6bfb57d57572db19771d15044354b70abaf9a0ca1856816d4a7c728dfaafd73
EXE SHA256: 4bce153055dda610614d3499e9b4488203f3880157381e2bb87ae3098e823d3c
SDRplay SHA256: f7348530c6a10d6d6ce6afd41fe2c323684f8f8534cb9cd6b0f8cd2f09f95687
Downloaded app CLI help/SDRplay status, five Aero survey checks, RDS reference
tests and GUI dry-run self-test PASS (ok=true, no errors/warnings).
Release public/non-draft/experimental prerelease; latest remains v0.2.96.
P25 guard: 19 changed paths, zero protected. No P25/NFM/WFM runtime edits.
No real Inmarsat antenna or physical speaker source-switch verification claimed.

## 2026-09-27 - T-0081 RDS and power gates

Published source bf8e2bb. Windows CI 36285842641 and YAML 36285842668 SUCCESS.
No runtime change or new release; public v0.2.110 remains current.

Release tests build PASS. Initial harness assumed MA_AT_END for partial read;
corrected to accept MA_SUCCESS or MA_AT_END with explicit returned frame count.
Candidate double-filter route then FAILED expected two groups (got one); draining
FIR supports did not alter failure. Direct discriminator branch passes expected
PI/PTY and no fewer groups than baseline. Final focused run: 3207 assertions in
two cases PASS. No production edits; WFM_RDS_QUALIFICATION.md records full scope.
Full local CTest: 15/15 PASS (50.52 s).

## 2026-09-27 - T-0080 C++ prototype

Published source 8e116ec. Windows CI 36284271627 and YAML 36284271706 SUCCESS.
No runtime change; verified public application remains v0.2.110.

Release tests build PASS; 478 retained-FIR/convolution/PCM/MPX assertions PASS.
Three benchmark executions repeat signal metrics: isolated 50 kHz deviation
case difference ~-85 dB at 10 MS/s, time/input ~0.29. Test-only cascade, not
production integration. Details and limits in WFM_RETAINED_PROTOTYPE.md.
All 15 local CTest suites PASS (54.43 s).

## 2026-09-27 - T-0079 independent filter isolation

Published source 8e600d2. Windows CI 36280496010 and YAML validation
36280496001 SUCCESS. Offline oracle tests were run locally; no claim that CI
runs the optional NumPy study. Public app v0.2.110 unchanged.

Three oracle unit tests PASS; 24 offline cases complete with finite results.
NumPy 2.0.2 on this Windows host; SciPy deliberately unused due version warning.
Results in WFM_FILTER_ISOLATION.md. No C++ or runtime changes; no app rebuild
required for the offline scripts. Longer full-rate candidate rejected for cost.

## 2026-09-27 - T-0078 WFM sweep

Published test source f52275e. Windows CI 36279199045 and workflow validation
36279198984 SUCCESS. No runtime change or new release asset; v0.2.110 remains
the verified public application build.

Release test build PASS. Two 72-case WFM sweeps validate and repeat all signal
metrics exactly (build/wfm-sweep-110.json). Seven Python parser tests PASS;
all 15 CTest suites PASS (50.60 s). Test-only changes on ecfe239; measured
quality failures recorded in WFM_IMAGE_SWEEP.md, not hidden by pass status.

## 2026-09-27 - T-0077 audit checks

Clean 787488e (runtime 1f7740e), existing Release tests. Fresh
build/fm-audit-110.json validates 36 measurements. Focused WFM/CTCSS/HF/classifier
run passes 9104 assertions in 41 cases. Independent 80 ms Hann tone projection
gives 67/71.9 Hz power ratio 1.22045, below current lock threshold 4. No runtime
edits or rebuild; published 0.2.110 unchanged.

## 2026-09-27 - T-0076 public release verified

Source 1f7740ee161d988d95917c666c05714123be5383, experimental v0.2.110.
Windows release 36277352426 / master 36277352558 SUCCESS; YAML 36277352359 /
36277352401 SUCCESS. Public ZIP SHA256:
0a9c5fa4b58fce03525a583ebb90c76b140fb97c777114973c8d572a602a8416.
Embedded source/version/run and EXE/SDRplay module hashes verified. CLI help,
SDRplay status, Inmarsat surveys, RDS references and GUI startup PASS; GUI exit 0,
ok=true, empty errors/warnings. Public prerelease, not draft; latest remains
signed v0.2.96. No hardware overload, physical listening or all-CPU guarantee.
Release: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.110

## 2026-09-27 - T-0076 NFM anti-alias measurements

Candidate response/direct-convolution tests26 assertions PASS at2.048/2.4/10MS/s.
Sampled stop rejection>93dB, pass ripple~0.0024dB. NFM/CTCSS/DCS84369 assertions
PASS before adding20 first/second-image cases; those80 assertions also PASS.
Three36-case benchmark runs in build/fm-benchmark-110.json; WFM metrics unchanged
in all runs. NFM2.4MS/s +40dB image difference-72.49dB vs+3.23dB before; wanted
gain approximately0dB vs-5.55dB.10MS/s cost ratio0.261-0.276, mean0.27012.
Additional 320/384 kS/s response cases PASS (30 assertions total). Release app
and tests build PASS; all 15 CTest suites PASS (50.87 s). RDS CLI reference
fixtures PASS. GUI dry-run startup exits 0 with ok=true and no warnings/errors.
Public CI release qualification subsequently passed as recorded above.

## 2026-09-27 - T-0075 public release verified

Source5c205cbfb9e7d6712a933d9c22b34e5a46eadb3e, experimentalv0.2.109.
Windows release36273965450/master36273965357 SUCCESS; YAML36273965509 and
36273965437 SUCCESS. Downloaded public ZIP SHA256:
1397b55689a7ad0c8a3fe3fcfa967bab7b98d7519be4a62252c3eb94bbd23505.
Embedded source/version/run and EXE/SDRplay hashes verified. CLI help/status,
Inmarsat survey, RDS references and GUI startup PASS; GUI exit0, ok=true, empty
warnings/errors. Public prerelease not draft; latest signed release still0.2.96.
Physical listening/other CPU architectures remain separate acceptance.

## 2026-09-27 - T-0075 WFM FIR prototype evidence

Reference ring/impulse/reset/tap-length fixture419 assertions PASS. Three36-case
benchmark runs saved to build/fm-benchmark-109.json. Against0.2.108 baseline,
all36 first-run cases have identical wantedGainDb, blockerAudioRelativeDb,
differenceRelativeDb and audioSamples. WFM10MS/s time/input ratio~0.64 versus
~2.59 baseline;2.4MS/s~0.16 versus~0.63. This is speech-demod cost on this host,
not GUI/RDS-total CPU. All108 repeated cases match baseline signal metrics.
Release app/test build PASS; combined WFM/NFM/RDS25360 assertions PASS. RDS CLI
reference and GUI dry-run (exit0, ok=true, no warnings/errors) PASS. Full suite
PASS15/15 in45.05sec. Public CI/asset results pending.

## 2026-09-27 - T-0074 measured FM baseline

Release test build PASS, measurement math5 assertions PASS. Explicit benchmark
180 assertions/36 cases PASS; wrapper repeated three complete matrices and saved
build/fm-benchmark-108.json. Parser four tests including negative cases PASS.
Default full CTest15/15 in47.84sec PASS. Results/provenance/limitations recorded
in FM_INTERFERENCE_BASELINE.md. Tests/docs only; app version remains0.2.108.
Source0dfe8cf pushed; Windows CI36247770217 SUCCESS, YAML36247770228 SUCCESS.
No new binary release needed for unchanged runtime; current asset remains0.2.108.

## 2026-09-26 - T-0073 public release verified

Sourceffd68c6 / experimental v0.2.108. Windows release36245181841 and
master36245181642 SUCCESS; YAML36245181839 and36245181638 SUCCESS.
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.108
Downloaded ZIP SHA256:
f30bd6bf0e7b922ce288d68b2702ee34f07de4e666d6972488a659f0122597c4.
Embedded source/version/run and EXE/SDRplay module hashes verified. Public CLI
help/status, channel survey, RDS references and GUI startup PASS (exit0, ok=true,
empty warnings/errors). Transient local DNS failures recovered on retry without
changing network/security settings. Physical listening not claimed.

## 2026-09-26 - T-0073 WFM repair qualification

Baseline0.2.107 has measured PCM partition failures in DEC-0144/ISS-0037.
Independent WFM causal FIR/decimator/PCM state repairs count and waveform
invariance. First run32 assertions/two cases PASS (12 partition configurations
and four reset/source transitions). NFM1626 assertions/five cases PASS;
RDS23235 assertions/ten cases PASS. Added zero-lookahead/zero-phase-repair and
nonzero latency-counter assertions before final full build. Final Release build
PASS; WFM80 assertions/two cases PASS, full CTest15/15 in45.62sec PASS. RDS CLI,
GUI startup (exit0, ok=true, no warnings/errors) and guard mutation tests PASS.
Public CI/download verification pending. No hardware listening claim.

## 2026-09-26 - T-0072 public release verified

Source2133b44, v0.2.107. Windows release36242577020/master36242576898 SUCCESS;
YAML release36242577024/master36242576895 SUCCESS. Public portable prerelease:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.107
Downloaded archive SHA256:
4a0ccbc7e59aa75aab0c495b613f0efb2688ba84dff76eb037e0269cd463e80d.
Embedded source/version/workflow and executable/SDRplay module hashes verified.
Downloaded CLI help/status, Inmarsat survey, RDS references and GUI startup PASS;
GUI exit0, ok=true, empty errors/warnings. Transient local network lookup failure
recovered; release metadata update retried successfully. No RF listening claim.

## 2026-09-26 - T-0072 local Auto BW gates

Release app/native/workspace build PASS. Checkbox Qt fixture18 assertions PASS:
default-enabled, varied proposed widths, disabled preservation, changed manual
value, reload and re-enable. RDS CLI reference fixture PASS. GUI dry-run exit0,
ok=true, errors/warnings empty. Exact shared-file guard negative tests PASS.
WFM explicit characterization (not default suite) reproduces ISS-0037: at
2.048MS/s PCM4800/4800 maxError0.0494488; at2.4MS/s4800/4805 error0.209476.
No WFM fix or RF listening claim. Default full suite15/15 PASS in44.75sec.
Public CI/asset verification pending.

## 2026-09-26 - T-0071 public release verified

Source ff986642df8e32a5135df11ed7621b54506f5fa5. Windows release36240404038
and master36240403829 SUCCESS; YAML36240404019 and36240403802 SUCCESS.
Public experimental v0.2.106, not a draft; signed latest remains v0.2.96.
Downloaded ZIP42,483,830 bytes; SHA256
401f063c0c0c799ec6bb59cf4b78790f1710c0685e02384100bb95ababe15b06.
Embedded source/version/run, executable and SDRplay module hashes verified.
Public CLI help, SDRplay status, RDS reference and survey tests PASS; public
GUI dry-run exit0, ok=true, no warnings/errors. Hardware reception/listening
not inferred from smoke tests. Collector deployment remains ISS-0035.

## 2026-09-26 - T-0071 PCM reproduction and isolated repair

Base b5cd608 / accepted app 80155ac (0.2.105). Actual PCM partition fixture
initially FAILED all eight rate pairs: at 2.4 MS/s ->48 kHz, 4800 whole versus
4667 split; at 10 MS/s, 4800 versus3918. At 48 kHz ->44.1 kHz,4410 versus4401.
Causal cubic clock repaired counts; remaining maximum error <=0.000101 matched
the block-based fade completion. Same fade threshold applied per sample removes
that difference. Initial three-case PCM rerun PASS33 assertions, before adding
explicit zero-lookahead/zero-phase-repair assertions. Final local run PASS:
51 assertions / three PCM cases; full CTest 15/15 in 46.55 seconds, Release
application build, RDS CLI fixture and GUI dry-run (ok=true, no warnings, exit0).
Exact shared-file guard mutation tests PASS. Public CI/assets pending.

## 2026-09-26 - T-0070 public release verified

Source 80155ac0d87122b067c3f26112e651cb1cb2a54c, v0.2.105. Windows master
36237622015 and release 36237621821 SUCCESS. YAML master 36237621931 and
release 36237621742 SUCCESS. Public experimental prerelease, not a draft:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.105
Workflow initially created a non-latest ordinary release from the numeric tag;
metadata was corrected to prerelease without replacing assets. Latest signed
updater release remains v0.2.96.

Downloaded ZIP 42,482,296 bytes; checksum and embedded source/version/run and
EXE/SDRplay hashes verified by build/verify-public-099.ps1 with explicit version,
commit and run arguments. ZIP SHA256:
092a8e5a94921f9c2ea57a6d86c498c19387aed20dc37f827e18ba8fe6bb8193
EXE SHA256:
3d0c9464f068bf7bc59b276a409758937edbfecdeda586984aa9c565e1a0234e
Public help/SDRplay-status/recorded RDS and Aero survey tests PASS. Public GUI
dry-run startup/self-test/shutdown PASS (exit 0, ok=true); no live RX claimed.
SDRplay enumeration reports vendor API service unavailable on this host, so no
physical SDRplay acceptance was performed. Root remote_diagnostics.defaults.json
verified: consent disabled, HTTPS collector URL correct, token present (not
logged), budget 65536 bytes/minute. Server receipt/deployment remains ISS-0035.

## 2026-09-26 - T-0070 reproduce then repair NFM

Windows/MSVC Release baseline f61ad0a. New actual Demodulator [nfm][stream]
test first FAILED seven parameter combinations. At 2.4 MS/s whole/split counts
were 1154/1157 (8192) and 1154/1272 (tiny); at 48 kHz tiny partition maximum
waveform difference was 1.418734. After causal FIR/phase repair first rerun
PASS 1561 assertions. Added explicit-reset/rate-transition and local logging
tests; full CTest PASS 15/15 in 47.68 seconds before final stage-timing/retention
additions. Final build/test/publication results follow when completed.
No P25 or WFM DSP setting/algorithm changed; shared Demod changes are an exact
reviewed digest pair in the frozen guard, with negative mutation tests PASS.

Final local app/core/diagnostics Release build PASS. Full CTest 15/15 PASS in
46.11 s; [nfm][stream] PASS 1575 assertions / 2 cases; RemoteDiagnostics suite
repeated five times PASS in 4.38 s. Recorded RDS CLI positive/negative fixtures
PASS against the rebuilt executable. No live RF test or audio listening claimed.

## 2026-09-26 - T-0069 read-only DSP baseline

Windows host, existing MSVC Release build; source 09cc9dd / app 25236d2.
`ctest --test-dir build -C Release --output-on-failure`: 15/15 PASS, 46.48 s.
No recompilation or live RF acceptance claimed. All production/test files
unchanged. PowerShell calculation matching C++ round(sr/target) at 2.4 MS/s:
M1=13, M2=4, rate=46153.8461538462; whole 16384 input =>315 stage-two samples,
two 8192 inputs =>316. Boxcar response at fs/13+1500 Hz: -41.7886708 dB.
These are isolated stage calculations, not measured final receiver audio.
Full evidence and follow-up gates: DSP_AUDIT_20260926.md.

Newest entry at the top. Record facts, not hopes.

## 2026-09-26 - 0.2.104 public asset verified

Source 25236d289dc1228ecc2632a3b52356619bb2e659. Windows release run
36234984587 and master 36234982550 SUCCESS; YAML 36234984518 and 36234982549
SUCCESS. Initial SQLite lifecycle failure repaired; intermediate superseded
runs canceled by normal concurrency. Final release public/non-draft:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.104

Independent download: 42,478,012-byte ZIP, checksum and embedded source/run/
version provenance PASS. Extracted CLI help, SDRplay status, RDS fixtures and
five dated Aero surveys PASS. Consent-off HTTPS defaults with collector-only
credential and 65536-byte budget present; secret values never printed. P25
guard against 46984f3: 39 paths / 0 protected PASS. Signed updater Latest
remains v0.2.96; no installer claim.

ZIP SHA256 ad43c67ab29cc6371b60155b5d5e843317c1e88b26c118c878a1a7871f598982
EXE SHA256 4493fe086fa404e624db61705636cfc35f42c51842527f2b4890b26d24ca0b6b
SDRplay SHA256 53676afe92834fcd00d9222469d7ca0ecd48b1f1d5c0358362cf1ea51562c053

T-0068 software milestone complete; physical rotor/SWR acceptance ISS-0036
and collector deployment/security ISS-0035 remain open, explicitly documented.

## 2026-09-26 - Rotor GUI full-path repetition

Added actual widget Connect/Arm-confirmation/Move/Close-to-Stop loop against
loopback ERP fixture. Initial harness selected the meter port by QObject
traversal order, and QMessageBox::done did not simulate clicking Yes. Named
port controls and actual button clicks correct the harness, not motion policy.
Final eight-case antenna suite repeated five times PASS (28.51 s). Existing
GUI/receive paths unchanged; latest app rebuilt. CI rerun includes these tests.

## 2026-09-26 - CI collector handle lifetime repair

Initial source da1cf44 Windows runs 36234693736 (release) and 36234692520
(master) failed collector Python tests before compile. Python3.12 retained
SQLite handles after transaction contexts, preventing temporary DB removal
(WinError32). _connect now scopes transaction AND finally closes the connection;
no GC timing reliance or ignored cleanup errors. This is a real collector
lifetime fix, not a test waiver. Both YAML checks passed. Rerun required.

## 2026-09-26 - Antenna control local gate

DEC-0140 / T-0068. MSVC2022/Qt6.11.1 SDR_Town and antenna_control_tests
Release builds PASS. Full CTest 15/15 PASS (59.75 s), antenna-all-tests.log.
Native window screenshot inspected; fixed compass-label placement/contrast
and escaped tab ampersand. Additional split-response, Stop ordering and
unsupported-SWR tests added for final qualification. No rotctld/rigctld binary
or physical rotor/meter is present; no hardware acceptance claimed.

Publish rotor/client telemetry as an experimental milestone independently of
the blocked collector deployment. Release notes explicitly retain ISS-0035;
server-side protections are not claimed deployed. No workaround/retry of the
blocked service restart. CI/public asset verification pending at this entry.

## 2026-09-26 - 0.2.104 diagnostics local qualification; deployment blocked

Windows/MSVC2022/Qt6.11.1 Release builds SDR_Town, sdr_town_tests,
sdr_town_workspace_tests, inmarsat_live_gui_tests and remote_diagnostics_tests
PASS. Full CTest 14/14 PASS, 58.90 seconds (build/diag-104-ctest.log).
Collector Python regression tests 3/3 PASS: auth separation, malformed/deep/
nonfinite payloads, rate ceilings and actual localhost receipt. Packaging test
PASS for missing-required credential, configured consent-off defaults and
invalid credential rejection. Menu cancellation/opt-out and persisted consent,
CLI forced-off, timeout/recovery and acknowledgement are in RemoteDiagnostics.

Local generated defaults verified: expected public HTTPS endpoint, enabled
false, collector credential present, 65536-byte/min cap. Secret values were
not printed. Collector-only GitHub secret configured; no admin key published.
Frozen-path guard on modified runtime paths PASS; git diff --check PASS.

Synthetic watch benchmark 10 cases PASS. 2.048 MS/s load ratios 0.266-0.410;
10 MS/s 1.163-1.759 (still overloaded). Stage fixture for one 2.048 MS/s
worker: 540.6 ms total, channelizer321.9/probe71.5/validation69.8/modem59.9/
setup17.5 ms. Synthetic quiet IQ does not establish real voice acceptance.

Collector deployment attempt was rejected by environment policy before
execution. Existing listener PID13624/proxy left unchanged. New server
protections are only locally tested, NOT deployed. Public receipt of the new
configuration and external-network acceptance remain unverified. No commit,
CI release, or public 0.2.104 asset claimed. Release held for deployment/security
qualification; shared-key authenticity and per-install enrollment remain open.

## 2026-09-26 - 0.2.103 public release verified

Source cb2ee37dcb6566b95c360e1725884f6b41a2e32d. Local full CTest 13/13
PASS (40.27 s), plus InmarsatLiveGui/InmarsatHostLifecycle each repeated ten
times, all 20 PASS (24.32 s). Final native screenshots reviewed. P25 frozen
guard: 23 paths / 0 protected locally and on real prior-commit master CI diff.

Release Windows CI 36231029153, master 36231028781, release YAML 36231029152
and master YAML 36231028806 all SUCCESS. Public non-draft portable release:
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.103

Independent download to build/public-0.2.103-local: 42,441,933-byte ZIP,
checksum, build-info source/version/workflow, EXE and SDRplay hashes PASS.
Extracted executable CLI help, SDRplay status, RDS recorded-MPX checks and
all five packaged Aero survey tests PASS. No physical RF/bias-T activation.
Evidence: build/monitor-103-public-check.log and monitor-103-repeat.log.

- ZIP: 0e02a5be51e85ef5de8899ad8063c490d333198eeb80ce932aee408506e6169e
- EXE: ef1f9a30477ff925ca4caf0dfa000da5f0a5dddcc8798196e8b0fbd3d2ce0c44
- SDRplay: 5f3f6a2d4741bcd667d05d7bbd293ab5f32a32d112392e72593221e8bbcd6206

GitHub /releases/latest remains v0.2.96 intentionally: portable v0.2.103 must
not replace the signed installer updater. T-0066/ISS-0034 delivered; physical
satellite/hardware acceptance and GAPS_20260926.md remain explicit open work.

## 2026-09-26 - Inmarsat monitor qualification in progress

Final local Release targets SDR_Town, inmarsat_live_gui_tests, sdr_town_tests,
sdr_town_workspace_tests PASS; CTest 13/13 PASS (40.27 s). New monitor tests
exercise registry retention/ordering/age/clear, country boundaries, live vs
inactive/stopped status, bounded history, copy/filter/pop-out and numeric sort.
Native screenshots exposed QStyledItemDelegate double rounding despite correct
item->text(); display-role strings now preserve frequency precision and that
case has a regression assertion. Reviewed build/monitor-103-visual PNGs after
repair; no overlap, horizontal scroll preserves additional aircraft columns.
Logs: monitor-103-final-build.log, monitor-103-display-build.log,
monitor-103-final-ctest.log. Frozen P25 path guard PASS. CI/publication pending.

MSVC 2022 / Qt 6.11.1 local Release. Initial monitor/store compilation PASS.
First test invocation used a new screenshot directory without creating it;
existing constellation screenshot checks failed to save (not a DSP failure).
The newly added GUI tests were edited after that target compiled, so this run
is not qualification of those tests. Rebuild all affected targets and create
the visual output directory before the final full CTest run. No release yet.

## 2026-09-26 - Tester release parity recheck

At local HEAD 5724777, git diff v0.2.102..HEAD changes only tests and docs;
src/include/external/data/CMakeLists.txt/cmake are identical. Local Release
SDR_Town target rebuild PASS; full CTest 13/13 in 38.84 s
(build/release-parity-build.log, build/release-parity-ctest.log).
GitHub v0.2.102 remains public, regular, with the 42,420,596-byte ZIP and checksum.
Current GitHub asset digest matches the independently rehashed previously
verified ZIP: 3e9f9aafcb46c0e7f0f3061c784c286879aff27848313b11f619d56a45e9dfc5.
Extracted executable hash matches embedded provenance. Release CI 36222945435
and latest test integration CI 36224393076 both remain successful.
A new combined download/extract/execute verification command was blocked by
the execution policy and was not retried. This recheck uses the current GitHub
digest against the previously downloaded and smoke-tested package, not a new
download/smoke claim. Local and CI toolchains need not yield identical EXE bytes;
application source parity is proven, not reproducible-binary identity.
No duplicate application release is needed. v0.2.102 is the newest portable
tester release; GitHub latest remains v0.2.96 for signed-installer updater safety.

## 2026-09-26 - fubarzi PR 33 selective test integration (T-0065)

Final integration source: 9237e7747c79f19a9e8eadc20338e0c7e70108b4.
Full Windows CI 36224393076 PASS; workflow validation 36224393080 PASS.
PR #33 closed 2026-09-26 with explicit selective-integration explanation,
commit/test links and contributor attribution. No contributor branch deletion.

MSVC 2022 Release native GUI/core test targets build PASS. Full local CTest:
13/13 PASS in 41.02 s (build/pr33-build.log, build/pr33-ctest.log).
Final explicit-include rebuild PASS (build/pr33-final-build.log).
InmarsatLiveGui and SdrplayControlLifecycle repeated ten times each: all
20 suite executions PASS in 27.28 s (build/pr33-repeat.log), including 50
synthetic Inmarsat start/process/stop cycles. Zero-IQ generated no valid voice
or PCM. Map test verifies painted-image retention after 750 messages evict the
position from chronological history, plus explicit clear. Not an RF/audio test.
Frozen P25 guard PASS. Only tests/docs changed; application version stays
0.2.102. Full CI and PR closure are now complete as recorded above.

## 2026-09-26 - Verified 0.2.102 public release

Source 89a0067be09ec555ab7d00250e0a9167e9b3ba19. Windows release 36222945435
and master 36222945379 PASS. YAML 36222945521/36222945396 PASS.
Public regular release: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.102
ZIP 42,420,596 bytes plus checksum, independently downloaded/extracted.
Checksum/source/run/executable/module provenance, CLI smoke, Aero survey and
recorded-MPX RDS tests PASS (build/repair-102-public-check.log).
ZIP: 3e9f9aafcb46c0e7f0f3061c784c286879aff27848313b11f619d56a45e9dfc5
EXE: c9e7d7431903decd54c53d684155f62d5c9eff7f0c7d86618cf9592a19b78e2d
SDRplay: 333a04a1b291f3137fbce50f4b54edef34cea974ad0f693c9b0cce9723486b74
GitHub latest still v0.2.96: portable release did not replace the signed updater.
Whole watch GUI screenshot inspected: build/inmarsat-102-watch.png; no overlap.
P25 guard 6fc02fd..89a0067 PASS (23 paths, zero protected). Hardware/multi-SDR
acceptance and 10 MS/s realtime are not claimed by these gates.

## 2026-09-26 - DEC-0136 local qualification, 0.2.102

Windows / MSVC 2022 Release app, native GUI, core and workspace build PASS.
Initial CTest 13/13 in 40.75 s. After mirrored FIR optimization, final CTest
13/13 in 37.86 s (build/repair-102-fir-build.log, repair-102-fir-ctest.log).
Exact old/new FIR arithmetic test: 20,000 inputs, many ring wraps. Whole versus
fragmented channelizer parity includes 2.048 and 10 MS/s. Mixed data/voice test
verifies both workers persist beyond timer deadlines with no reset or false PCM.
GUI tests exercise wheel anchor, pan clamp, click mapping, no drag selection,
retune reset and persistence. Render inspected: build/inmarsat-zoom-pan.png.
Direct fixture invocation initially lacked its Qt DLL path (0xC0000135); rerun
through CTest's configured environment PASS (repair-102-gui.log).
Frozen P25 guard PASS: no protected implementation changes.

Idle synthetic benchmark before/after (1,2,4,8,16 workers):
2.048 MS/s before 0.33/0.31/0.31/0.33/0.51; after 0.26/0.30/0.27/0.28/0.41.
10 MS/s before 1.32/1.34/1.36/1.41/2.29; after 1.22/1.21/1.18/1.22/1.74.
Ratios are processing/RF seconds, lower is better. These short synthetic
measurements include startup and no real speech. Earlier benchmark during
compilation is excluded as contaminated. 10 MS/s realtime remains ISS-0032.
Public CI/release verification remains pending before delivery is complete.

## 2026-09-26 - Verified 0.2.101 public release

Source 9ff87c6573090212333f693842eb8e0180408b27. Windows release 36211687629
and master 36211687681 PASS; YAML 36211687620/36211687791 PASS.
Public regular release: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.101
ZIP 42,418,875 bytes plus checksum, independently downloaded and extracted.
Checksum/source/run/executable/module provenance, CLI smoke, Aero survey and
recorded-MPX RDS tests PASS. Evidence build/repair-101-public-check.log and
build/public-0.2.101-local/. Physical satellite/multi-SDR acceptance not claimed.
ZIP: de6ee176586c195754a6562a9311954d020558e43158ccaac2ab284aa64cc51c
EXE: f66c366ee1eed0299f69ddd9e2f2bf65f717406f6acf44e2fa4edc36374c89d3
SDRplay: 230c9934baeb6cb24f10f1a9741af053ec6a32f7d20f5e2da9bb4f9ac61cde4e
P25 guard 831c6e6..9ff87c6 PASS: 20 paths, zero protected changes. Portable
testing release does not replace signed updater. T-0062 multi-SDR remains open.

## 2026-09-26 - 0.2.101 channel budget and map-cache qualification

DEC-0135 / T-0063. Final MSVC2022 Release app/unit/GUI/workspace rebuild PASS;
CTest 13/13 PASS in 38.54 seconds (repair-101-qualified-build/ctest.log).
Tests cover 16-budget validation/persistence, 8 data + 8 voice grouping, strict
majority evidence, expiry/visit reset, bounded partial fallback, and independent
map retention through >500 messages, stale updates and 256-aircraft eviction.

Synthetic watch benchmark at 2.048 MS/s, 64 x 65536 samples (2.048 RF seconds):
1/2/4/8/16 workers wall processing/RF ratios 0.299/0.305/0.310/0.326/0.513.
16-worker max block 48.13 ms; average alone does not guarantee no live overruns.
No valid voice frames in synthetic input: NOT a full vocoder/live RF benchmark.
Evidence build/watch-expanded-benchmark.log; earlier baseline in
build/watch-baseline-benchmark.log. Keep existing conservative saved budgets.
Shared device ownership remains unchanged; multi-SDR is T-0062, not delivered.

## 2026-09-26 - Public 0.2.100 constellation release verified

Source c313f065c9f308958653397b7a9176c12430868b. Windows release 36208705124
and master 36208705250 PASS; YAML 36208705172/36208705179 PASS.
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.100 is public, regular,
not draft. ZIP 42,415,627 bytes and checksum independently downloaded.
Hash/provenance/executable/module validation, five survey checks, CLI help,
SDRplay plugin registration and recorded-MPX RDS smoke tests PASS.
ZIP: fd10a58fc69e547dc2f722f97ef7a36cc56fd722ee158c39d2900057f3d9d8d3
EXE: 16918cb4340d54311a2f3c84079b95f82e2f8d1bde7e60db8fb861f8009eb979
SDRplay: d5c510cd3305be2417800a0b4ca1cd40930b5e7c119425427b42d7593e81de3e
Evidence: build/public-0.2.100-local/, repair-100-public-check.log and CI logs.
No local SDRplay service or satellite reference: physical reception unverified.
P25 guard a7685c0..c313f06 PASS: 19 changed paths, zero protected. Portable
testing release does not replace signed updater v0.2.96.

## 2026-09-26 - 0.2.100 constellation qualification (DEC-0134)

Local MSVC2022 Release app/GUI/unit/workspace build PASS. Final incremental
build includes all edits; CTest 13/13 PASS in 38.13 seconds. Actual MSK/OQPSK
scatter callbacks tested for 600/1200/8400/10500 and reconstruction. Three native
channel workers concurrently produce independent ID/frequency/rate snapshots.
GUI tests cover pinned watch selection then Tune/preset activation, preview
non-retune, exact peer selection, stale clearing and EGC unsupported status.
Synthetic IF and widget fixtures are not RF lock/reception acceptance.
Inspected normal 270x250 and compact 170x180 PNG renders: axes, point, status and
frequency/rate legible without overlap. Evidence: build/inmarsat-constellation-*.png,
repair-100-final-build.log and repair-100-final-ctest.log. Survey checks PASS.
Native demod/scheduler/audio/P25 implementation unchanged. Publication pending.

## 2026-09-26 - Public 0.2.99 verification

Source 9834492c7d5f222b48471a2b504812693d5381ea; Windows release run
36206766578 and master 36206766533 PASS. YAML 36206766545/36206766515 PASS.
Regular public release: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.99
ZIP 42,414,667 bytes plus SHA256 sidecar. Anonymous download, source/version/run
provenance, executable/module hashes, CLI help, all five survey datasets and
recorded-MPX RDS tests PASS. Downloaded plugin registers from the extracted
folder as 0.5.2-48bd8b4; vendor service absent here, no RSP RF acceptance claim.
ZIP: c4a7a8e635f01c040f7833ee46a697dd830ce0f3a8ed1e1b89e47fd86df61487
EXE: 73853a21e4516885fb98c4eef7eb7231bf1e83982390729a7568dbc26ecd49ae
SDRplay: cf5ad380afbc4207c3a8790e39b616b619820e43432af31e5bd727b0eeaed8e4
Evidence: build/repair-099-public-check.log, public-099-local/, and CI logs.
P25 guard 1c0777d..9834492 PASS: 14 paths changed, none protected. Portable
testing release, not signed updater replacement; dated presets not live survey.

## 2026-09-26 - 0.2.99 rate-selector qualification (DEC-0133)

Initial MSVC Release link failed LNK1104: running SDR_Town.exe locked the
output. Closed that instance gracefully; unchanged source rebuilt successfully.
App, Inmarsat GUI, unit and workspace targets PASS. CTest 13/13 PASS in
36.54 seconds. GUI regression covers all five plans and seven decoder choices,
explicit preview versus Tune, preserving matching/manual/restored frequencies
and no-preset status. Offline Inmarsat survey and workflow self-tests PASS.
Evidence: build/repair-099-build-retry.log and repair-099-ctest.log.
No P25 processing changes; no live satellite reception claim. Publication pending.

## 2026-09-26 - Verified public 0.2.98 release

Source 6f0e3f3d72b34f8ddbbabc0e68073c5f42339d92. Windows release run
36204228544 and master 36204228658 PASS; YAML 36204228453/36204228537 PASS.
https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.98 is public, not draft,
not prerelease. ZIP 42,414,064 bytes plus SHA256 sidecar. Latest intentionally
remains signed v0.2.96; this is a portable testing release, not in-app update.

Independent anonymous download/extraction verification PASS:
ZIP: 84cf4c7d11486a6da37a93f223a02ce462ede52073f743ec25c151e36a6e3524
EXE: 1c1c086a8e77023e867bfc806074a11ac8e51f4d28712b89b756f3bbad1df0dc
SDRplay DLL: 4f89ebcd49c3c7563358bba8b172ecea4c7cf9ee7f40c9ab03e64e77bbc8e895
Embedded commit/workflow/module provenance matches. Public CLI exit 0, help
includes biastee; status selects downloaded folder's sdrPlaySupport.dll
0.5.2-48bd8b4 and registers its driver. Service unavailable here, no RSP RF proof.
All five downloaded Aero survey datasets PASS; downloaded RDS recorded-MPX,
reference/parity/confirmation/malformed/missing-file tests PASS.
Evidence: build/public-098-local/. Committed P25 guard PASS: 28 changed paths,
zero protected. No P25 decoder or audio changes in this release.

## 2026-09-26 - 0.2.98 local qualification (DEC-0132)

MSVC2022/Qt6.11.1 app/core/workspace/Inmarsat GUI builds PASS, final repeated
build PASS. CTest 13/13 PASS in 39.65 seconds. New GUI case verifies exact
1546.0625 MHz selection, 10500 bit/s and engine settings; default corrected to
1546.005 MHz. Five source survey JSON gates PASS; workflow YAML self-tests and
P25 guard negative tests PASS. No P25 decoder/receiver/audio edits.
Pinned SDRplay module built against build/vcpkg_installed x64 Soapy and API
3.15 development headers. Actual app CLI status exit 0 reports bundled
sdrPlaySupport.dll 0.5.2-48bd8b4 registered. This PC lacks the vendor service;
no RSP RF/voltage acceptance claimed. Upstream format/narrowing warnings remain.
Logs: build/repair-098-{build,final-build,ctest,sdrplay.out,sdrplay.err}.log.
CI publication/public-asset qualification pending. Existing 0.2.97 promoted to
regular non-Latest release at user request without changing its asset bytes.

## 2026-09-25 - Verified CI-built 0.2.97 experimental publication

Source bf83d97c6029b866227eaa11ea4fc8fb3ad2f0e3. Windows release run
36110284353 and master run 36110284332 PASS; workflow validation runs
36110284422/36110284382 PASS. Release job verified its anonymous public
download, checksum, source/run provenance, CLI launch and recorded-MPX RDS.
Public release: https://github.com/blkph0x/SDR_Town/releases/tag/v0.2.97-experimental
Portable ZIP (42,359,949 bytes) and SHA256 sidecar published, not draft.
ZIP SHA256: 92e3221658de1a1a75a0d241c531401231ed1378474ce3d3dd76efd8dbac1a3c
Executable SHA256: f6f46ee305f19c50b18ef312f7f4628b75ad178344caaf5266308d3cc7bba487

Independent local anonymous download/hash/extraction/provenance verification
PASS; downloaded executable CLI help exposes biastee and exits 0. RDS CLI
recorded-MPX/reference/parity/confirmation/malformed/missing-file tests PASS.
Actual attached generic RTL/R820T serial 00000001: probe exit 0, driverSupport=1,
saved=OFF, driverReported=OFF, voltage=not-measured. No hardware ON test.
Evidence: build-audit-20260925/rtl-bias/published-0.2.97/.
This is a portable prerelease, not a signed installer/in-app updater release.
GitHub latest remains v0.2.96. Mandatory rule persisted in AGENTS.md,
DEVELOPMENT_RULES section 11 and SOURCE_OF_TRUTH.md. P25 DSP unchanged.

## 2026-09-25 - CI package gate prevented incomplete RTL release

Run 36108455138 at source 4714570: build/core/lifecycle/GUI/replay/SSTV PASS;
staging FAILED on missing SoapyRTLSDR.dll before any public release. Fix adds
a pinned source module build against the same vcpkg Soapy/RTL libraries.
Local module configure/build PASS, HAS_RTLSDR_SET_BIAS_TEE=1, version
0.3.3-6ca357c. Existing upstream narrowing/nonfloating-complex warnings remain,
not treated as proof of RF correctness. Also stage the compiler CRT explicitly
because CI windeployqt could not find VCINSTALLDIR. Replacement run required.

## 2026-09-25 - Mandatory publication workflow preparation (DEC-0130)

Workflow YAML validator/self-tests PASS; P25 guard negative self-tests PASS.
Application source is the locally tested b2d4de8 RTL repair (13/13 suites),
with version bump to 0.2.97. CI will rebuild/test the exact new version.
Release branch checks version match and versioned notes, refuses asset
replacement, recognizes experimental tags as prereleases, and anonymously
downloads/verifies public hashes, run/source provenance and CLI execution.
CI execution/publication remains pending; no success claimed by this entry.
Initial runs 36107415468/36107415463 cancelled before publication after source
audit found CI disabled RDS DSP. Existing backend enabled with target-checked
MinGW tools and packaged MPX replay gates. Local versioned 0.2.97 build PASS,
CTest 13/13 PASS in 35.84 s. Replacement clean CI runs are required.

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
