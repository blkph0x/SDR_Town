# Implementation audit reconciliation: 0.2.137

Date: 2026-10-11. Reviewed tree: `44738dc9b6b3d3a318f7e81b0afc8a9c67ca576f`;
application source: `8e888dd76bd948b704f97a10582e6d10d4f4a6de`.
Inputs: the user's "Deep logic/workflow audit" and "Full code audit".

This is a review, not a receiver rewrite or a new release. No application,
P25 DSP, timing, security, audio, driver or station-frontend source changed.
Line references below refer to the reviewed tree. The supplied reports did not
identify their exact commit, so their "current HEAD" claims are not reproducible
version identifiers.

## Verdict

The reports correctly identify orchestration size, unreported failures, unfinished
P25 transmission and residual driver-lifecycle risk. They are not a validated bug
list. Some "concrete issues" are hypotheses with no failing input or execution
trace. Satellite automation and DVB-S2 descriptions omit implemented paths. One
P25 queue claim also mistakes a rejected submission for committed IQ consumption.

The highest priority is the **reproduced receive lifecycle faults below**, not
changing P25 gates or immediately splitting files to meet an arbitrary line limit.

## Confirmed findings

### P1: detached RX can publish old IQ into a replacement session (ISS-0105)

`src/DeviceManager.cpp:1621` waits about 300 ms for an RX worker, then detaches it.
The stream is labelled stopped and its buffers reset at lines 1705-1711. Starting
again reuses the same `StreamState`. The old RX loop checks its generation at
line 3656, **before** entering `readStream`, but the successful return publishes
at lines 3737-3739 without revalidating the generation under the publication lock.

Measured reproducer using a fake Soapy driver and freshly compiled, unmodified
`DeviceManager.cpp`:

1. Start a fake real RX normally; wait for `live hardware`.
2. Hold one read inside a condition variable until after Stop has detached it.
3. Start a replacement stub session. It emits bounded synthetic noise (each
   component is approximately -0.025 to +0.025), not the old read's marker.
4. Release the old read with 1024 distinguishable `(0.375, -0.125)` samples.
5. Read the replacement session's ring: **all 1024 old samples are present**.

Three independent runs reproduce this with both test orderings. This proves
cross-generation sample contamination, not merely a potential race. It does not
prove this occurred in the user's accepted P25 audio or on every normal retune.

Related static gaps requiring the same repair boundary:

- An old `RunningGuard` clears a shared running flag without a generation check
  (`DeviceManager.cpp:3529`); it can clear a replacement worker's indicator.
- The spectrum handoff timeout detaches the old worker but does not abort the
  handoff (`:1384`). The replacement uses the same `myGen` (`:1437`) and clears
  `stopFlag`; a delayed spectrum worker can resume in that same generation.
- The spectrum publication after FFT (`:3496`) lacks a final generation check.
- Detached workers retain references to manager-owned state. The manager destructor
  only stops TX (`:157`); generation counters do not extend object lifetime.
  Shutdown use-after-free was not reproduced in this pass.

Required repair: make cancellation and publication atomic with respect to stream
replacement, including IQ, overflow, retune metadata and spectrum publication;
separate generation-owned completion/lifetime from the reusable device slot.
Quarantine a stuck physical driver until it actually exits. Do not simply replace
detach with an unbounded join on the GUI thread. A native driver can genuinely
hang. Driver process isolation remains the longer-term recovery option.

### P1: non-SDRplay activation failure reports live hardware (ISS-0106)

`DeviceManager.cpp:1336` stores `activateStream`'s return, but only checks failure
when `applyInfo.isSdrplay` is true. Other drivers proceed to publish handles and
`live hardware` at `:1492` even when activation returned an error.

Fake non-SDRplay driver returns `SOAPY_SDR_STREAM_ERROR` (-2). Three runs produce:

```text
AUDIT activation=-2 runtime=live hardware
```

The driver produces no valid samples. This can make an unavailable receiver look
successfully opened and sends diagnosis down the wrong decoder/audio path.
Repair: validate activation for every driver, dispose only the failed local
attempt's resources, publish a structured failure, and never publish readiness.
Retain successful RTL, SDRplay and generic-driver startup regression coverage.

### P2: some operator paths announce success without checking it (ISS-0107)

At `MainWindow.cpp:204`, the Listen start return is discarded and exceptions are
swallowed; `anyStreaming` becomes true regardless. The tune route at `:5125`
similarly reports "Started monitor + tuned" without checking start or tune results.
Audio output selection also has empty catches (`:217`, `:5138`). An ownership
refusal or setup failure can therefore be followed by misleading success text.

These are specific call-site defects, not proof that all exception handlers are
bad. Cleanup/destructor handlers, rethrows and logged fallbacks have different
contracts. Hardware-open failures do already log and expose `runtimeState`;
the audit's claim of generally invisible synthetic success is too broad.

Repair: distinguish requested/opening/ready/failed; check immediate refusal and
asynchronous completion separately. Correlate one operation ID with device key,
workflow/session/generation, stage, elapsed time and sanitized failure category.
Keep logs rate-limited and content-free; no status-bar/log work in audio callbacks.

### P2: legacy device test can turn unexpected exceptions into warnings (ISS-0108)

`tests/test_devicemanager.cpp:163` and `:166` catch across the whole basic test,
then emit `WARN` rather than failing an unexpected exception. An exception before
the relevant checks can make an unexercised section appear successful. Catch2
assertions already recorded as failures are not erased by these catches.

Repair: split deterministic fake-driver contracts from optional hardware tests;
explicitly skip only a known missing prerequisite before exercising it. Unexpected
exceptions must fail. The newer lifecycle/ownership tests are substantially better
and should be reused, not replaced.

### Existing P2: large companion responses still need paging (ISS-0104)

The FUBAR 64 KiB caller truncation was fixed and the real 90 KiB DLL test passes.
The native DLL still limits HTTP responses to 256 KiB; large maps/messages need
bounded pages and explicit oversize handling. This is tracked as T-0130. Raising
the bound without collection limits is not a complete repair.

## Claim-by-claim reconciliation

| Supplied claim | Current evidence and classification |
|---|---|
| MainWindow is very large | Confirmed: 14,064 lines, plus 1,451 voice-worker and 1,735 orchestration lines. Maintenance risk, not a demonstrated critical runtime fault merely because of size. |
| Other large files | Confirmed: P25LiveDecoder 9,844; P25VoiceDecode 9,193; DeviceManager 4,271; CliApp 5,348. |
| All these catch-all handlers swallow errors | Overstated. Some log, some rethrow, some are cleanup guards; the specific empty handlers and unchecked results above are real. |
| Per-device Soapy I/O locking and stable identities | Confirmed (`DeviceManager.cpp:42-67`, `DeviceOwnership.h`). Factory operations remain process-wide. A factory hang may block other opens, not all running radios' reads. |
| Multi-device ownership generally permits stealing | Not established by these reports. Domain, token and stable-identity checks exist and negative ownership tests pass. This does not complete multiple independent P25 session controllers. |
| Stub hides hardware failure with a fake carrier | Overstated. `DeviceManager.cpp:3844-3869` emits bounded synthetic noise, not a carrier, and logs no-hardware. Device Manager/workflow status exposes runtime state. Noise alone is not an adequate operator warning; some primary actions still misreport success as above. |
| P25 block and streaming have different contracts | Correct (`P25LiveDecoder.cpp:6378-6440`). This alone is not a bug; caller input chronology matters. Default streaming remains opt-in; do not flip it from this report. |
| Phase search/reset logic necessarily causes present garble | Unproven. Search budgets and retained protocol evidence are real; no supplied capture demonstrates a new bad candidate selection. Preserve accepted code pending a differential reproducer. |
| Any rejected voice job silently loses one hop | Too broad. `submitP25VoiceDecodeJob` rejects at `MainWindowP25Voice.cpp:645-662`, but rolling IQ is marked submitted only in the success branch (`MainWindowP25Orchestration.cpp:1029-1032`). The failure log's word "dropped" is misleading for retained rolling IQ. Bounded ring eviction is a different loss event. |
| P25 encryption/slot gates are complex | Correct, but a condensed comment list is not a complete security proof. Current unbound-slot isolation tests pass; no gate relaxation is justified. Unknown/encrypted mute and wrong-slot rejection remain requirements. |
| HF resampling has residual images or slow AGC after rate jumps | Hypothesis, not measured by either report. Current identity-aware continuous correction avoids resetting on every target correction (`HfDemod.cpp:406-422`); poison, alias and clock tests pass. No new HF numerical defect established. |
| Satellite still invents 900 s passes, ignores missing TLE or loses selections | Stale. `SatPassPlanner.cpp:122`, `:449-474`, `SatCatalogue.cpp:223-258` validate epoch/pass and round-trip selection. Current default/named automation uses shared checked policy and refuses busy P25/Listen ownership. Focused and native GUI tests pass; real pass acceptance remains open. |
| DVB-S2 only short QPSK 1/2, no 8PSK/APSK | Incomplete/outdated. Generic `demodDvbs2Frame` (`frontend/DvbFec.cpp:519`) slices MODCOD-selected constellations and applies rate-specific FEC. Test `test_frontend_station.cpp:484` covers pilots, 8PSK, 16/32APSK and shaped IQ. Restricted legacy entry points and stale strings remain. These synthetic tests do not certify a finished blind live receiver or broadcast-picture playback. |
| Commercial decryption is missing functionality to complete | Not a receive-hardening requirement. Scrambled transport handling remains fail-closed; do not expand scope to decryption. |
| TX is wholly a placeholder | Partly true: P25 speech encoder/framer are diagnostic skeletons (`P25AmbeEncoder.cpp:40`, `P25Phase2TxFramer.cpp:19`). Hardware tone TX is real, separately authorized (`DeviceManager.cpp:3906`) with driver range/readback gates. CLI labels placeholder output and defaults tone TX to file-only. |
| AIS/DSC/VDL2/SAME absent, ONNX placeholder | Confirmed (`BandPlan.cpp:34-66`, CLI model/encode help). Frequency labels are not decoders. These belong in the roadmap, not the defect count. |
| Control server/auth/update safeguards | Confirmed in the examined paths: empty-token startup rejected, bounded constant-time token helper, redacted handler errors, signed manifest verification. This is not certification of every remote endpoint or dependency. |
| Inmarsat is a "full" chain | Substantial code exists, but "full" needs channel/voice/map field evidence, not class names. Keep RF identities/positions distinct from online lookup and the documented partial capability/acceptance limits. |

## Verification in this pass

- Built an isolated Catch2 executable with the **current unmodified**
  `src/DeviceManager.cpp`; existing Release objects supplied unrelated dependencies.
  Windows/MSVC 19.44, Qt 6.11.1. No production executable replaced.
- Fault probe: two assertions fail as specified, 13 prerequisite assertions pass,
  on each of three runs. One test returns activation -2; one holds then releases a
  read across Stop/restart. No physical RF stream or transmitter used by the probe.
- Existing focused regression filter:
  `[ownership],[hardware-loss],[tx-safety],[sstv-pass],[hf],P25*`:
  **227 cases / 84,487 assertions pass**.
- CTest: HF audio filter, SDRplay controls, RTL Bias-T, workflow lifecycle,
  satellite SSTV GUI and frontend station: **6/6 pass**, 22.08 seconds.
- The new failure cases are not covered by those green checks. Green existing
  tests do not close ISS-0105/0106. No new on-air/audio listening acceptance claimed.

Local evidence (ignored, not release material):
`build/audit_20261011/lifecycle_probe.cpp`, `CMakeLists.txt`, `probe-1.log`
through `probe-3.log`, `regression-tests.txt`, `ctest.log`. The two test scenarios
above should become permanent regression tests with the production repair. Do
not add expected-failure allowances to make CI green.

## Ordered implementation plan

1. **T-0132: device lifecycle transaction.** Preserve current accepted DSP. Add
   permanent failure fixtures first: delayed read returning positive data,
   overflow, exception or tune completion after cancellation; retry/Stop; slow
   spectrum exit; different-radio isolation; shared SDRplay lifetimes; process
   teardown. Repair session publication/completion and quarantine together.
   Every activation failure must stay non-ready and be observable. No unbounded
   GUI join and no unconditional native close while a driver still uses a handle.
2. **T-0133: truthful workflow outcomes and tests.** Replace unchecked return and
   success-on-exception paths with operation outcomes, keeping requested state
   separate from applied state. Add bounded content-free diagnostics via the
   existing consent-controlled uploader. Test denied lease, removed USB, failed
   output activation and asynchronous cancellation in native UI and FUBAR.
3. **T-0130: companion paging and remaining parity.** Explicit page/cursor/limit
   transport, oversize tests beyond 256 KiB, exact named-session addressing and
   stale-reply rejection. Do not broaden file/consent/TX/rotor permissions.
4. **Controller extraction after characterization.** Start with device-operation
   outcomes and native/HTTP adapters. Keep P25 DSP separate. Extract one ownership
   boundary per commit, with recorded transition traces and unchanged reference
   PCM/security results. Do not use <3000 lines as the acceptance criterion.
5. **New decoders and P25 TX remain separate projects.** Fix reliability before
   promoting absent protocol implementations or diagnostic transmit skeletons.

Repair acceptance must include the red probes turning green, existing multi-radio
and GUI tests, paired accepted P25 replay PCM/security checks, stop/restart stress,
bounded memory/thread cleanup and public package verification. Hardware driver
hang handling cannot be certified solely with synthetic tests.
