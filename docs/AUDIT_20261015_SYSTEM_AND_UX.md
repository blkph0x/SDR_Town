# SDR Town system audit and operator-workflow plan

Date: 2026-10-15. Decision: DEC-0222. Task: T-0135.

## Scope and verdict

Reviewed source baselines:

- SDR Town `a0d4e21a02cbceee4c89945e9579f1b1fc9f8acc`, master, version 0.2.138.
- FUBAR `308a6539eb560f5d4351eea421a601a327d3d589`, main, version 1.1.50.

Both matched their fetched upstream branches. This pass changes documentation
only. Production receive, P25, UI, settings, ownership and updater code remain
unchanged. The report includes FUBAR because its web controls and the shared DLL
are part of the operator's end-to-end workflow.

The application has substantial working functionality and useful safety
boundaries. It is not ready for a claim of complete robustness or complete code
coverage. Normal test suites pass, while targeted negative tests reproduce
several failures those suites miss. The highest-value next work is lifecycle,
audio ownership and truthful outcomes, followed by simpler task-oriented views.
Another broad P25 rewrite is not justified by this audit.

This is a broad implementation and workflow audit, not an assertion that every
line, protocol permutation, device or OS was exhaustively checked. Principal
first-party chains, callers, state transitions and integration boundaries were
traced; very large orchestration/protocol units were reviewed selectively.
Third-party dependencies were assessed at their integration boundaries, not
independently re-audited in full. There is no measured line/branch percentage.
Comments and historical documents were used as navigation, not proof of behavior.

Evidence labels below:

- **Reproduced**: executed against current code or the paired public binary/DLL.
- **Code-proven**: a concrete executed-path contract violation or interleaving;
  not necessarily reproduced as a physical-radio failure.
- **Qualification gap**: requires a new measurement or reference fixture before
  changing behavior. It is not a demonstrated defect.
- **Design proposal**: an intended improvement, not something implemented now.

## 1. Verification performed

Windows host, MSVC 19.44, Qt 6.11.1, Release builds.

| Gate | Result and important limit |
|---|---|
| Fresh Town core/workspace targets and application | Build PASS |
| Town CTest | 22/22 suites PASS, 108.97 seconds |
| Core Catch cases within CTest | 550 passed, 2 skipped; 244,100 assertions passed |
| Workspace Catch cases within CTest | 52 passed, 6 skipped; 1,056 assertions passed |
| Fresh FUBAR `build-fubar-msvc` application | Build PASS |
| FUBAR CTest | 4/4 suites PASS, 1.34 seconds |
| Fresh Town native GUI screenshot harness | Listening 960x720, Trunking 1280x900, HF 800x700, Analysis 1600x900 PASS; no RF/startup errors |
| Fresh Town + fresh FUBAR + public control DLL | Paired authentication, named workflow views, privacy, ownership and explicit device selection checks PASS; bounded dry-run, no RF tune |
| Public FUBAR 1.1.50 + public DLL, synthetic native service | Oversized map and queue-admission failures reproduced |
| Current DeviceManager, fake driver | Activation failure and late read contamination reproduced |
| Current FM demodulators, malformed IQ | Persistent non-finite PCM reproduced in NFM and WFM |
| Current Inmarsat tracking model, synthetic aircraft | Valid bounded collection exceeds companion response budget |

The eight skipped core/workspace cases require independent SSTV recordings,
an independent JAERO burst IQ fixture, or explicitly enabled personal P25 alias
database/import fixtures. Their skips are not passes. Other registered SSTV
suites ran, but that does not establish these omitted fixtures ran.

Native screenshots and the actual loopback web page were inspected. The web
inspection included a narrow viewport and Radio/Inmarsat navigation. This is
not a substitute for novice-user trials, screen-reader testing, every DPI,
physical speaker listening or long-duration multi-radio stress testing.

No new live P25/STT acceptance was performed. No physical RF transmission or
rotor movement was requested. SDRplay's vendor service is not installed on this
host, so ABI/control tests cannot establish physical RSPdx reception/bias-T.
No live external aircraft API or remote diagnostics collector changes were made.

Local evidence, intentionally not committed (captures/settings/tokens must not
be uploaded with an audit):

| Repository | Evidence |
|---|---|
| Town | `build/audit_20261015_ctest.log`, `build/Testing/Temporary/LastTest.log` |
| Town | `build/audit_20261015_faults.log`, current rebuild of `build/audit_20261011/lifecycle_probe.cpp` |
| Town | `build/audit_20261015/probe.cpp`, `dsp-map.log`, `synthetic-map.json` |
| Town | `build/audit_20261015_gui/` screenshots, logs and profile results |
| FUBAR | `build/audit_20261015/ctest.log`, `pair/result.json`, `probe.py`, `result.json` |

The negative-probe specifications and actual results are recorded below so the
next repair can promote them to permanent tests. They are not registered as
passing CI tests, and their expected failures are not hidden in the green count.

## 2. Findings, ordered by repair priority

### F01 - Old RX can publish into a replacement stream

**High; reproduced; ISS-0105, T-0132.**

Sources: [DeviceManager.cpp](../src/DeviceManager.cpp), lines 1585, 1624 and 3719.
The RX generation is checked before a potentially blocking native read. Stop
advances the generation and may detach the reader. A replacement then reuses
stream state. The old successful read can append samples without an atomic
post-read generation/publication fence.

Probe: hold fake `readStream`, stop past the detach window, start replacement,
then return 1,024 marked samples from the old reader. All **1,024 stale samples**
appear in the replacement ring. This repeats the prior audit against current
compiled source. It is not proof this caused a specific past P25 recording.

Repair: give each RX attempt independent lifetime/state, validate its generation
after every blocking driver return and under publication synchronization. Audit
retune completions, overflow handling, running flags and spectrum handoff too.
Quarantine unjoinable driver work; never close/free its native object while it
can still use it. An unbounded join on the GUI thread is not an acceptable fix.

Gate: delayed read/tune/overflow, cancel during open, stop/restart and device
removal; zero old samples/completions in the replacement, bounded GUI response,
and independent radios continue. Keep existing accepted P25 replay outcomes.

### F02 - Generic activation failure becomes "live hardware"

**High; reproduced; ISS-0106, T-0132.**

Source: [DeviceManager.cpp](../src/DeviceManager.cpp), line 1336. A negative
`activateStream` result is rejected only when `isSdrplay` is true. A fake generic
driver returning **-2** still reaches the runtime state **live hardware**.

Repair: check activation status for every driver, retain the error category and
clean up only the failed generation. Ready/applied status must follow successful
activation, not just successful open. Simulation must remain visibly simulation.
Gate: all negative return codes and exceptions, success, cancellation and retry.

### F03 - Emergency audio trimming bypasses the consumer lease

**High; code-proven interleaving; ISS-0110, T-0136.**

Sources: [AudioEngine.cpp](../src/AudioEngine.cpp), callback lines 159-186 and
`trimQueuedAudio` lines 498-515; [AudioEngine.h](../include/AudioEngine.h),
`ConsumerLease` and `discardAll`.

The callback correctly acquires a consumer lease. Trimming writes the same
`readPos` under the producer's `audioMutex`, which the callback never acquires,
without taking the consumer lease. Atomics prevent a torn cursor, not lost
updates or safe ownership of ring sample storage.

Concrete allowed schedule: callback reads cursor 100; trim advances it to 420;
callback stores 148. Discarded samples 148..419 become queued again. A producer
can also reuse newly freed slots while the callback is still reading them.
Existing discard tests do not exercise this trim path.

Known callers: the **analog** emergency queue branch in
[MainWindowP25Orchestration.cpp](../src/MainWindowP25Orchestration.cpp):1646 and
[SatcomScannerEngine.cpp](../src/SatcomScannerEngine.cpp):718. The source filename
does not make this a demonstrated P25 decoder bug; do not change vocoder/slot
logic to repair it.

Repair: use the established consumer-exclusion contract or a consumer-owned
trim request. The real-time callback must remain nonblocking. Gate the actual
production callback with deterministic barriers, marked PCM, wraparound, multiple
outputs and concurrent trim/push: no replay, stale reads or cursor rollback.

### F04 - One non-finite FM sample contaminates later clean audio

**Medium; reproduced; ISS-0111, T-0136.**

Source: [Demod.cpp](../src/Demod.cpp):450 and persistent FM filter/discriminator
state. Input rate is checked, but non-finite IQ can enter stateful processing.
The separate [HfDemod.cpp](../src/HfDemod.cpp):359 already has input protection.

Probe, separately for NFM and WFM: 24,000 IQ samples at 240 ksps, one NaN at
index 100, then ten clean 100 ms blocks. Each mode returns **4,780 non-finite
PCM samples** in the corrupted block and **48,000** in the following clean
second. This is malformed-input robustness, not evidence ordinary RF emits NaN.

Repair: reject invalid input/configuration before changing persistent state,
signal a discontinuity, and reset the affected stream in a defined way. Add a
last finite-output guard with a bounded diagnostic, not silent prolonged poison.
Gate: NaN/Inf IQ and configuration, recovery, variable block sizes, multiplex
gap semantics and unchanged valid-input PCM. No speculative filter retuning.

### F05 - Some native start actions report intent as success

**Medium; code-proven; ISS-0107/0108, T-0133.**

Sources: [MainWindow.cpp](../src/MainWindow.cpp):204 and 5125;
[test_devicemanager.cpp](../tests/test_devicemanager.cpp):98-167.

The primary start helpers discard `startStreaming`'s boolean, catch exceptions,
then set active status or report started/tuned. Nearby audio catches are silent.
A legacy basic device test converts unexpected exceptions to warnings before
later assertions. Already recorded Catch assertion failures are not erased.

Newer [WorkflowRadioSession.cpp](../src/WorkflowRadioSession.cpp) already models
ownership, applied tune, actual readiness and a bounded opening deadline. Reuse
that contract rather than introduce another independent readiness interpretation.

Gate: native/CLI/web agree on requested, opening, ready, failed and stopped;
no success on denied ownership, failed audio output, absent device or activation
error. Test known unavailable hardware separately from unexpected exceptions.

### F06 - A supported Inmarsat collection breaks the web map transport

**Medium; reproduced; ISS-0104, T-0130.**

Sources: [InmarsatMessageStore.cpp](../src/InmarsatMessageStore.cpp):153,
[InmarsatTracking.cpp](../src/InmarsatTracking.cpp):43 and 103,
[SdrTownControlClient.cpp](../src/SdrTownControlClient.cpp):167-186;
FUBAR `src/sdr_town_bridge.cpp`:426-446.

Independent bounded identity and position registries can produce 512 aircraft.
The current model accepts 512. A synthetic valid snapshot with 512 identities,
256 positioned, produces **284,750 bytes**. The DLL stops its HTTP read above
256 KiB; FUBAR provides a 262,144-byte output buffer.

Feeding this actual model output through the public executable/DLL returns
**HTTP 502**, with **262,143 bytes of truncated JSON copied into the error**.
Ordinary small maps passing does not cover this valid population.

Repair: stable snapshot IDs plus bounded pagination/deltas for map/message
views, explicit oversize/truncation status and short redacted errors. Account for
HTTP framing as well as JSON bytes. Do not merely replace the cap with unlimited
allocation. Gate 512 aircraft, long Unicode values, fragmented responses,
deletions, source filtering, named sessions and a changing snapshot mid-page.

### F07 - Web control queue has no effective admission bound

**Medium; reproduced; ISS-0113, T-0138.**

Sources: FUBAR `src/web_server.cpp`:3032, 3060 and 3146.

Queue entries use caller-chosen `clientId`; a new ID is appended without a queue
limit. Stale queued entries are discarded during promotion, while status walks
and serializes the entire queue under the shared lock. One local test peer
successfully queued **64 distinct IDs**. Source permits continued growth.

Two extensions limit one lease, not one person's total use under new IDs. This
is fairness/resource control, not proof an outsider can guess an existing
controller's secret ID or bypass current control ownership checks.

Repair: bounded queue/status, server-issued admission/lease tickets, request
budgets and optional authenticated membership for public community stations.
An IP address alone is not a unique human identity and can unfairly exclude
households behind NAT. Preserve server-side expiry and extension limits.
Gate duplicate IDs, rapid new IDs, stale clients, concurrent promotion and
restart, with bounded memory and unchanged active-owner exclusivity.

### F08 - Aircraft presence can keep an obsolete position "fresh"

**Medium; code-proven; ISS-0112, T-0137.**

Source: [AdsBTrackStore.cpp](../src/AdsBTrackStore.cpp):137, 167, 325 and 379.
Every accepted local identification/velocity frame refreshes `lastSeenUnix`.
Coordinates retain `positionValid` without a separate last-position timestamp.
Visibility expires on general last-seen; a local valid position wins over the
network track. Continued non-position messages can therefore retain an old RF
location indefinitely and override a newer network fix.

Also, local `ingestModeSFrame` never assigns `altFt`; only the network path does.
Status serializes the default altitude without a presence flag. Unknown local
altitude is not a measured zero. This finding is about the local ADS-B store,
not the separately aged Inmarsat tracking model.

Repair: per-field observation time, validity and provenance; independent TTLs
for identity, position and velocity; monotonic expiration and explicit replay
clock. An identity update must not refresh coordinates. Use nullable altitude;
add independently verified barometric/geometric decoding rather than inventing
a value. Gate position -> long identity-only traffic, fresh online/stale RF,
offline removal, wall-clock changes, partial updates and absent altitude.

### F09 - Updater limits arrive after buffering; signature fetch has no deadline

**Medium; code-proven; ISS-0114, T-0138.**

Source: [UpdateManager.cpp](../src/UpdateManager.cpp):131, 139, 212 and 256.
Manifest and detached signature are read without response-size enforcement.
The signature fetch waits in a nested event loop without an explicit deadline.
Installer data is fully read before checking its 512 MiB maximum. Manifest
`size` is validated but not passed to `doDownload` as a streaming byte bound.

This does not bypass signature/hash checks; those correctly fail closed. It is
an availability/resource problem before validation and a re-entrancy/cancellation
risk, not evidence the release host has been compromised.

Repair: asynchronous bounded stages, small manifest/signature caps, transfer and
absolute deadlines, single in-flight operation, safe destruction/cancellation,
stream-to-temporary-file and incremental hash with exact signed size enforcement.
Gate slow/no-end/oversize/truncated replies, redirects, repeated Check actions,
shutdown mid-fetch and valid signed update. Never relax the signature gate.

### F10 - "Share for this run" can fail after a stored opt-out

**Medium; code-proven; ISS-0115, T-0139.**

Sources: [InmarsatRemoteDiagnostics.cpp](../src/InmarsatRemoteDiagnostics.cpp):23
and [RemoteDiagnostics.cpp](../src/RemoteDiagnostics.cpp):640.
The affirmative dialog appends a URL to request diagnostics. Configuration then
overrides that request with the stored consent value. A previous stored false
therefore prevents the newly authorized one-run sharing operation.

This fails closed; it is not an unconsented upload finding. Repair with an
explicit scoped one-run authorization, not by deleting or ignoring stored
consent globally. Forced-off arguments remain authoritative; recording consent
and review/send remain separate. Test Yes, No, stored false, forced off, restart
and consent revocation during queued delivery. UI must show actual delivery state.

## 3. What earlier broad criticisms no longer establish

| Claim | Current implementation evidence |
|---|---|
| One process-wide radio lease prevents independent radios | `DeviceOwnership.h` has per-device ownership, physical-domain checks and generation/token identity. Remaining bugs are lifecycle/controller consistency, not absence of this foundation. |
| Every live Soapy call shares one global lock | Live I/O locking is per physical driver domain. Factory locking remains shared. Do not remove necessary SDRplay same-domain serialization. |
| Audio ring is 192,000 with power-of-two masking | Current ring capacity/initialization enforce power-of-two storage. F03 is a different, still-open ownership defect. |
| AUTO cannot select WFM | GUI receive calls `chooseSmartModeAndBandwidth` in `MainWindowP25Orchestration.cpp`:541 before DSP. Core fallback is not the whole GUI workflow. |
| Control token is optional by accident / compared with ordinary equality | Current server validates required token, uses constant-time comparison and bounded HTTP handling. Explicit development no-auth mode is distinct. |
| DeviceManager TX opens without any arm/authorization gate | Current hardware TX requires authorization and validates driver channels/ranges. This audit does not qualify actual RF output or regulatory compliance. |
| Every small HF adjustment must reset | Current HF path distinguishes nominal identity and continuity. Verify callers before asserting every AFC update resets it. |
| Local aircraft worker discards everything except the last 16k samples | The old capped `processMagnitude` helper remains, but the current worker uses the streaming magnitude path. A legacy helper is not proof of that live data loss. |
| No current companion radio recovery | Explicit stable-key selection and protected same-user Windows pairing exist; actual paired dry-run checks pass. Historical ISS-0109 is not reopened by this audit. |

Do not reintroduce the historical package-review blockers as a new task. Current
published paired versions are 0.2.138 / 1.1.50. Historical tracker paragraphs
remain evidence of their date, not current release instructions.

## 4. End-to-end coverage map and remaining qualifications

| Chain | Implementation traced | Current gap / next proof |
|---|---|---|
| Device discovery -> controls -> ownership -> RX -> IQ consumers | DeviceManager, DeviceOwnership, WorkflowRadioSession, SDRplay/RTL controls | F01/F02/F05; multi-radio delayed-driver soak, real RSPdx acceptance |
| NFM -> discriminator -> speech/tone taps -> audio | Demod, NfmInputDecimator, ReceiveDecoder, AudioEngine | F03/F04; current normal tests pass; adjacent blocker/deviation and output stress matrix |
| WFM -> speech + multiplex -> RDS | Demod, WfmSpeechFir, RDS integration | Full-rate capped channel FIR remains. Tap count alone does not prove an audible bug; measure alias rejection, CPU and RDS before changing rate plan |
| AM/USB/LSB/CW -> audio/data | HfDemod and early delegation in Demod, CW session | Dead alternative HF branches remain maintenance risk; qualify weak/strong signals, effective bandwidth, AFC continuity and output latency before changing taps |
| P25 CC -> grants -> follow -> selected-slot voice -> speaker | Control/follow/traffic/voice orchestration and relevant tests | Selective review, not every FEC/vocoder line or new on-air acceptance. Preserve accepted math/security; existing capture-backed response/tail issues stay separate |
| Inmarsat IQ/DDC -> demod -> FEC/messages -> identity/position/voice -> maps | Engine/pipeline, message store, tracking, network source and diagnostics boundaries | F06/F10; supplied external IQ fixture skipped; no invented RF position or voice identity; retain separate RF/online/estimated ages |
| Local ADS-B IQ -> magnitude -> Mode S -> tracks -> native/web map | Worker, ModeS, AdsBTrackStore | F08; verify field provenance/altitude before declaring complete local aircraft telemetry |
| SSTV RF/audio/file -> mode detection -> helper -> image storage | Router, live session, worker and satellite scheduler | RF Auto considers USB/LSB/NFM, not explicit AM; file Auto no-frame fallback ISS-0101 remains. Independent recordings and real satellite passes still needed |
| CTCSS/DCS/DTMF/CW | Decoder math/input validation, session/tap routing and tests | CTCSS long-window acquisition is not short reverse-burst detection; do not remove experimental label on long-tone tests alone. No claim all concealed DTMF transformations are recognized |
| APT, AX.25/APRS, satellite and rotor workflows | Integration boundaries, representative pure/GUI tests | APT percentile normalization is not wedge calibration; no physical rotor/SWR/actual satellite pass proof in this run |
| Control DLL -> FUBAR server -> browser | Actual paired requests, authentication/ownership negatives and narrow browser view | F06/F07; per-session audio buses and remaining native-only configuration still need explicit parity work |
| Capture/replay -> diagnostics -> updater -> packaging | Input/consent/size/lifecycle boundaries, current CI definitions | F04/F09/F10; not a full dependency/security penetration assessment or new release qualification |

Additional limits: a shared receive registry currently lists RDS/CTCSS/DCS, not
every decoder domain. Workflows still have substantial GUI/CLI orchestration
duplication. Existing `FmDiagnostics` counters are health measurements, not a
complete per-decision history. No benchmark here establishes calibrated dBm,
geolocation/TDOA, universal satellite coverage or every advertised host OS.

## 5. Test coverage must become measurable

No first-party line/branch coverage collection was found in the inspected CMake
and CI scripts. Full Windows app CI exists; the non-P25 Linux gate compiles a
subset, not the entire desktop application. No equivalent full macOS/Linux app
acceptance was established. No instrumented sanitizer run was performed here.

Specific blind spots:

1. Negative device probes are outside normal CTest and need permanent fixtures.
2. `test_audioengine.cpp` includes copied callback logic; exercise the actual
   production callback or its production helper, especially the trim interleaving.
3. MainWindow is not exercised by the core/workspace library tests as a complete
   application. Keep actual-executable automation, not only substitute widgets.
4. Optional fixture skips need their own visible qualification ledger so green
   suites cannot imply a recording or physical device was tested.
5. Shared transport tests must cover the maximum supported collection, not only
   ordinary 90 KiB responses and empty named-session maps.

Next: establish a reproducible baseline per first-party module, report excluded
generated/vendor code, then set meaningful changed-branch gates. Add ASan/UBSan
where toolchains permit, and concurrency tooling on isolated portable components.
Use deterministic barrier tests for Windows driver/audio ordering regardless of
sanitizer availability. Coverage percentages are navigation aids, not RF quality.

## 6. UI findings and proposed operator model

### Observed friction

- The Listening screen places RF gain, direct sampling, bandwidth policy,
  filters, classifier status and output selection beside ordinary tuning.
  Radio identity and actual readiness are less prominent than advanced knobs.
- The P25 view retains analog controls while adding CC/TG tables and many
  actions. Which controls affect which active receiver is not obvious.
- Workspace presets change layout, not necessarily demod/frequency. An HF
  workspace must not imply that HF reception has automatically been configured.
- Compact native screenshots show signal/floor annotations competing with the
  spectrum frequency edge. Qualify label placement across DPI and dock sizes.
- The narrow FUBAR view spends much of the first viewport on header, status,
  tabs and listening controls; tuning is lower down. Later workflow tabs require
  horizontal navigation. Radio has both mode actions and a Tune mode draft.
- Inmarsat web controls expose a free-text workspace/session identity and many
  start/open/reload choices. These are implementation concepts users should
  normally select from active sessions, not type correctly.

Screenshots used isolated test settings/synthetic spectrum. Their sample
frequency or waveform is not evidence of a real hardware display defect.

### Proposed default, preserving expert capability

Use three independent visible concepts throughout native and web:

| Concept | What the operator sees |
|---|---|
| Radio/source | Stable radio name, available/busy/unplugged, hardware vs replay, current owner |
| Receive session | Task, requested channel and applied center/rate, starting/receiving/failed, decoder state |
| Audio destination | Listening/muted, selected session/output, volume, output failure if any |

Default Listen screen: radio selector, frequency with explicit units, mode
(Auto/manual), Start/Stop, spectrum/waterfall, volume and squelch. Put gain,
bandwidth policy, filters, ppm, direct sampling and decoder diagnostics in a
clearly accessible Advanced area. Show effective values when automatic settings
are active; never silently enable bias-T or steal another workflow's radio.

Use task pages or docks for Listen, P25, Aircraft, Satellites, SSTV and Tools.
Keep a compact active-session list so several radios/tasks remain understandable.
Do not hide an ownership conflict behind a generic disabled button: name the
owner and offer explicit stop/change-radio actions. Preserve existing expert
layouts and provide a reversible beginner layout, not a forced migration.

| Task | Primary flow | Advanced details |
|---|---|---|
| Listen | Select radio -> frequency/mode -> receive -> volume/squelch | RF/filter/clock settings, tone decoders, output routing |
| P25 | Select system/CC and radio -> monitor -> choose follow policy | Grants, aliases, slot/security evidence, timing/diagnostics |
| Inmarsat | Select radio/session -> data/voice watch channels -> start -> map/messages | Per-channel lock/constellation, schedule, association evidence, replay |
| Aircraft | Select local radio or explicit online source -> start -> map | Provenance, position age, confidence, filtering and raw message diagnostics |
| SSTV | Select source -> RF/image mode -> output folder -> receive | Doppler/pass plan, history, signal diagnostics and recovery details |
| CW/tones/APRS | Select source/session -> enable decoder -> results | Bandwidth, thresholds and decoding evidence |

Map labels must distinguish last RF fix, online fix and estimate; unknown
location stays unknown. "Transmitting aircraft" and "audio playing now" are
different states. An alias or guessed nearest aircraft cannot create identity.

Mobile web: replace the overflowing workflow strip with a compact selector,
keep Take Control/status and the task's primary action nearby, separate immediate
commands from editable drafts, and do not overwrite a draft during polling.
Use selected session lists, not raw session IDs. Read-only visitors see the
same applied state but no misleading editable controls. File access, device
power, telemetry consent, TX and physical rotor movement retain local boundaries.

### Shared UI contract, not a second application engine

Expose one typed runtime snapshot/command result from existing controllers:
session ID, workflow, stable radio key, lease/generation, source type, desired and
applied tuning, input epoch/cursor health, effective mode/BW, decoder lock,
audio route and last structured failure. Adapt native/CLI/web views to it in
small steps. Do not move DSP into GUI adapters or duplicate ownership logic.

Test desktop 1024x768/1280x720 and scaling, narrow web 360/390 pixels and larger
layouts, keyboard focus/accessibility names, long aliases and error text. Avoid
root-page horizontal scrolling; dense tables may scroll within their own region.
Verify cold start, unplugged/busy radio, missing driver, muted output and offline
operation. Run novice tasks without coaching before calling the layout intuitive.

## 7. Instrumentation plan

Use existing local/capture event and opt-in remote channels. Do not log every
sample or duplicate IQ into telemetry. Add bounded structured outcomes where
the traced boundaries are missing evidence.

| Boundary | Useful evidence |
|---|---|
| Command -> ownership -> applied state | Correlation/session ID, operation, owner/token generation, requested/applied sequence, elapsed time, rejection category |
| Driver -> IQ -> decoder | Stream epoch, absolute cursor, gap cause/count, read latency, queue age/high-water mark, stale completion rejected |
| DSP -> PCM -> audio callback | Input/output counts and timestamps, finite-input rejects, produced/consumed/dropped frames, underruns, queue latency histogram; no callback logging/allocations |
| Map/identity updates | Local source category, per-field age/validity, stale-position suppression, identity-association reason; aircraft content excluded from general remote telemetry |
| DLL/web transport | Route category, session, payload/page size, duration, truncation category, queue depth; never bearer tokens or raw JSON error echoes |
| User experience | Start requested/ready/failed, output ready/muted, recoverable action; do not confuse button presses with successful operation |

Report interval aggregates and transitions with budgets, retention and dropped-log
counters. Keep high-detail local captures explicit and bounded. Separate ordinary
health consent from IQ/audio review/send, make opt-out cancel queued delivery,
and expose actual delivery/error state. Quantify p50/p95/p99 latency rather than
assuming another fixed delay will repair audio.

## 8. Ordered implementation plan and non-regression gates

| Order / task | Deliverable | Exit proof |
|---|---|---|
| 1 / T-0132 | Session-safe driver lifetime and activation | Both reproduced lifecycle probes turn green; cancel/retry/multi-radio matrix; no stale IQ/completions |
| 2 / T-0136 | Safe audio trim and FM invalid-input recovery | Real callback race fixture; finite/recovered PCM; valid PCM and RDS/tone continuity unchanged |
| 3 / T-0133 | Truthful start/tune/audio outcomes | Same native/CLI/web result under success, denial and injected failures; unexpected exceptions fail tests |
| 4 / T-0130, T-0137 | Paged companion maps and per-field aircraft freshness | Measured 284,750-byte case usable via pages; no stale coordinates renewed by identity; unknown altitude stays unknown |
| 5 / T-0138 | Bounded web admission and updater transfers | Queue/resource stress, server-controlled fairness policy, slow/oversize/cancel update tests; auth/signature protections unchanged |
| 6 / T-0139 | Scoped consent, diagnostic contracts, coverage baseline | Consent matrix, bounded telemetry, visible skipped qualification, first-party coverage artifact |
| 7 / T-0140 | Beginner native/web workflow views | Existing expert layouts preserved; no DSP changes; novice and accessibility tasks pass; active-session state matches all clients |
| Later | Measured DSP and capability improvements | WFM blocker/CPU/RDS benchmarks, HF reference sweep, short-tone fixtures, APT calibration and physical-device acceptance before features graduate |

Small commits should each include a failing-before/passing-after reproduction,
one causal change, affected tests and tracker updates. Shared audio/driver
changes need accepted P25 replay comparisons plus live confirmation before a
new audio-quality claim. Never relax encryption/slot/CRC gates to make a graph
or speaker appear busy. Preserve unrelated user work and exact release provenance.

This is an execution plan, not a new blanket release embargo or an assertion
that every finding must be combined into one risky patch. Publish qualified
working application updates under the standing release rule; this audit alone
needs a documentation push and CI, not a replacement binary.
