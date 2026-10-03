# Receive-chain audit and bounded HF/CW repair

Baseline: `badcba4e510bed8ff8ef48231bc4cd5ab6d36e74`, 0.2.120.
T-0098 / DEC-0167. Source review plus executable synthetic regression tests.
Not an on-air certification, exhaustive race analysis, or a measured RF dynamic
range specification. The submitted audit does not identify its exact commit;
its statements below are checked against this baseline, not accepted as facts.

## Highest-priority confirmed gaps

1. **Device ownership is still global, not per physical device.**
   `DeviceManager::acquireDeviceLease` compares one owner/index, allows equal or
   higher priority takeover without force, and release matches owner only.
   `setCenterFreq` also exists outside the lease interface. A per-index map alone
   is insufficient: immutable physical identity, generation-bound lease tokens,
   authorized retune and per-device release must form one contract. SDRplay dual
   channels need a shared physical-device arbitration domain. Do not remove the
   common Soapy lock until these relationships are tested.
2. **Real hardware overflow is not propagated as a stream discontinuity.**
   The `numElems == -4` branch of `rxThreadFunc` increments a process-wide static
   counter and logs; it does not advance `streamEpoch`. The next successful
   append has contiguous software indices despite missing RF samples. Existing
   decoder gap handling cannot detect this particular loss. By contrast,
   retunes/ring resets/cursor overwrite do have epoch or position evidence.
   Repair needs a hardware-overflow injection test and epoch boundary published
   atomically before new IQ, with P25 non-regression acceptance. Not changed here.
3. **Detached worker recovery remains a lifecycle risk.**
   The stub handoff timeout detaches and aborts the upgrade. Generation checks,
   lifecycle mutex, `stopFlag` and `active=false` exist: it is inaccurate to say
   successful handoff deliberately runs two writers. Nevertheless the detached
   old worker retains stream access and cannot be joined normally. Reopen,
   destruction and session replacement require fault-injected tests. An
   unconditional GUI-thread join is not a safe fix for a genuinely stuck driver.
4. **Hardware test-tone TX lacks the separate P25 arm contract.**
   `CliApp`'s explicit `tx tone` command sets `attemptHardware=true` and calls
   `startToneTx` without checking the P25 TX arm state. DeviceManager additionally
   promotes known driver names to TX-capable, swallows tuning/gain failures and
   can still activate the stream. GUI P25 PTT is separately armed and currently
   a stub; conflating these two paths is incorrect. Add explicit hardware TX
   authorization and fail-closed capability/tune/rate/gain checks, then mock
   `writeStream` to prove no samples before authorization. No RF TX tested here.

## Claim-by-claim verdict

| Submitted claim | Verdict at baseline and evidence |
|---|---|
| Separate analog/P25/Inmarsat/side-decoder paths and large orchestration units | Confirmed architecture. `Demod.cpp`, `HfDemod.cpp`, `P25LiveDecoder.cpp`, `InmarsatEngine.cpp`, `MainWindowP25Orchestration.cpp`. File sizes alone are not a defect. |
| Stub-first asynchronous real open | Substantially confirmed for a newly opened ordinary RX. Existing active streams/composites have other paths; not literally every call. `DeviceManager::startStreaming`. |
| Detached stub/real worker risk | Confirmed timeout paths; actual use-after-free or simultaneous-writer incident is not proven. Lifecycle/generation guards must be considered. |
| All live Soapy users share one lock | Confirmed for guarded reads/retunes, including a requested 100 ms read timeout. Not every Soapy method uses it (e.g. spectrum-rate readback). Lock removal requires driver ownership work, not a mechanical mutex replacement. |
| Per-device lease absent | Confirmed. One owner/index remains in the header and acquire/release code. |
| Digital decoders do not receive timed gaps | Too broad. `RecentIQWindow` has epoch/start/end/discontinuity; P25 orchestration, Inmarsat engine, SSTV RF and RDS check them. The concrete missing link is hardware overflow above. Soft catch-up deliberately leaves the flag false but changes absolute position, which chronological consumers can detect. |
| No real/stub runtime state | Incorrect as an absolute claim. `getRuntimeStateLabel`, tune sequences and control status exist. A single authoritative atomic ownership/source snapshot is still missing; composites even use `isReal` as a soft combined-source flag. |
| AUTO can never select WFM | Incorrect for application behavior. `MainWindowP25Orchestration.cpp` calls `chooseSmartModeAndBandwidth` before demodulation; CLI does too. `P25VoiceDecode.cpp` implements band/classifier selection, including WFM. Leaked AUTO still becomes NFM in `Demod.cpp`; that fallback and dead AUTO arms are real maintenance debt. `classifyModeAround` itself is unused. |
| NFM stage 2 is unfiltered sample skipping | Misleading: the channel FIR runs before the retained samples. Adequacy requires measuring attenuation at folding bands. A pick-every-M stage after sufficient filtering is not intrinsically wrong. Stage-1 Kaiser rejection is covered by existing tests. |
| Independent multiplex oscillator implies speech/tone disagreement | Separate state confirmed; a phase-constant complex rotation disappears under FM differentiation. Drift or inconsistent correction would need a sample comparison. Separate phase alone is not proof of a bug. |
| WFM capped input-rate FIR then decimation | Confirmed rate plan. The FIR computation is now causal overlap-save `WfmSpeechFir`, not naive full convolution per sample. Alias rejection/CPU need measured blocker sweeps, not a claim that every WFM channel fails. Existing retained-rate prototypes and RDS RF fixtures remain experiments, not production. |
| HF delegated plus unreachable duplicate detector code | Confirmed. `HfDemod::supports` returns early; later AM/SSB/CW detector branches in `Demod.cpp` are unreachable for those modes. Remove separately with exact output/dispatch gates; do not modify shared FM/P25 code casually. |
| 257-tap SSB/AM, 401-tap CW, 700 Hz pitch | Confirmed. These counts are not themselves proof of inadequate selectivity. Existing tests quantify sideband/blocker rejection. SAM/ISB/DSB are absent from the demod enum. |
| Repeated identical GUI frequency writes cause resets | Denied: changes greater than 0.5 Hz cause the HF comparison to reset; identical writes do not. A 1 Hz correction with stable explicit station identity did reset; reproduced and repaired here. The GUI currently passes explicit identity for tone decoding, not every HF call, so no universal AFC improvement is claimed. |
| HF has no data tap / must use squelched speech | Incorrect for USB/LSB/AM: pre-squelch tap exists (after AGC/audio shaping). This pass extends it to CW and avoids copying when no tap is requested. Speech LPF is already disabled on SSTV RF routes. |
| Wide AM and SSB settings clamped silently | Confirmed profile policy: AM >30 kHz reverts to10 kHz; SSB >12 kHz to6 kHz. Useful for stale WFM settings, inadequate as an explicit wide-AM product contract. New diagnostics expose effective values; GUI acknowledgement/manual-wide semantics remain future work. |
| Every output block rounded/padded to requested count | Too broad. HF ignores the legacy requested count and uses persistent resampling; NFM/WFM have dedicated PCM clocks. Existing partition tests verify these behaviors. |
| Legacy global free demod wrapper | Confirmed compatibility hazard. Most production callers use owned instances; audit actual call sites before removing it. |
| Audio callback has the correct real-time shape | Confirmed no producer mutex/logging and zero-fill/atomic counters in callback. Producer allocation/locking remains; that alone does not prove live demod runs on the Soapy RX thread (GUI DSP has its own worker). |
| Empty control token silently bypasses authentication | Denied for normal startup: `SdrTownControlServer::start` rejects empty/whitespace tokens unless unauthenticated mode was explicitly enabled. The private authorization helper's empty-token branch is redundant defense-in-depth debt. |
| Open health, ordinary token comparison, echoed exceptions | Confirmed. Health returns version, not arbitrary state. Loopback reduces remote reachability; timing leakage severity requires a threat model, not an automatic high rating. Exception leakage and bounded connection lifetimes deserve follow-up. |
| Signed manifest and installer hash | Confirmed. Distribution/plugins are not thereby authenticated as a complete installation. Preserve signed updater vs experimental portable separation. |
| Diagnostics rejects all non-HTTPS | Not literally: helper accepts HTTP without a bearer token; bearer-authenticated config requires HTTPS. Official opted-in configuration uses HTTPS. Recording consent/review is separate; no evidence in this audit proves consent bypass or IQ exfiltration. |
| Inmarsat map merges different ages invisibly | Not true of current implementation as a blanket claim. RF/online/estimated sources have separate ages/expiry and explicit online consent in tracking model/panel. Field identity-to-call acceptance remains open, not silently proven by map markers. |
| dBm without calibration | Channel values are uncalibrated dB/power ratios, not dBm. Keep calibration distinction explicit. No measured antenna-terminal dBm feature is claimed. |

## Repairs and verification in this pass

- Reject non-finite HF IQ/gain, invalid rate bounds, and NaN squelch without
  poisoning subsequent FIR/AGC state. Rate contract: input8 kHz..100 MHz,
  output8..384 kHz; upper limits are resource budgets, not device capability
  claims. Default invalid/zero output rate retains the existing48 kHz fallback.
- Explicit same-station NCO corrections preserve phase/history/sample clock;
  identity/mode/rate/filter changes still reset. Plain frequency changes without
  explicit stable identity retain conservative reset behavior.
- Per-owner HF diagnostics expose blocks/samples/resets/rejection/corrections,
  reset reason bits and effective filter settings. No logging in sample loops.
- Tools > CW / Morse Decoder observes an explicitly selected analog demod with
  an independent IQ cursor/demodulator. WAV/FLAC/MP3 file input, automatic or
  manual pitch/speed, bounded transcript, copy/save, worker cancellation,
  source/gap counters. No RX retune, lease, TX, speaker or P25 changes.
- Pinned MIT GGMorse recognizer, existing MIT JFFT and miniaudio. Continuous,
  filtered conversion to4 kHz; input gaps reset recognition, not join fragments.
  Decode latency includes upstream's3-second analysis history. Experimental:
  output is an estimate, never an instruction or proof of a station identity.

Two new HF cases failed before repair (poisoned sample accepted; same-station
1 Hz correction raised discontinuity). Existing HF cases plus these pass after
repair. Independent ITU-timed Morse fixture decodes `CQ DE VK2ABC`; RF fixtures
exercise NFM/WFM keyed audio, AM, USB, LSB and a keyed CW carrier. File-rate conversion,
silence, gap/epoch reset, invalid data and GUI cancellation are separately tested.
See BUILD_NOTES for complete gate results and actual publication evidence.

## Safe follow-up order

1. Mock hardware-overflow to decoder-epoch propagation; acceptance on recorded
   P25/Inmarsat/RDS/SSTV and live voice before releasing any shared RX change.
2. Device-scoped ownership tokens and physical-driver domains, with two-device
   simultaneous tests and stub-handoff/close/reopen fault injection.
3. Hardware TX explicit authorization and fail-closed configuration, no RF until
   bench-authorized; fix control exception/connection behavior with API tests.
4. WFM blocker/RDS/CPU measurements, then qualify retained-rate implementation.
5. AM fading/overmodulation and weak/adjacent SSB measured sweeps; intentional
   bandwidth policy, configurable CW pitch, synchronous AM as a separate mode.
6. Native live CW/MCW and hand-keyed fixtures, robust timing/error indicators,
   optional source selector beyond main receiver. Do not advertise CW RF
   acceptance from a generated WAV alone.

P25, device leases, hardware driver calls, FM filters, audio engine, network map
and consent paths are deliberately unchanged in this patch. Those unresolved
findings are not marked fixed just because HF/CW tests pass.
