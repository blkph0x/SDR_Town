# SDR Town Full Receive-Chain Audit

Date: 2026-09-20  
Scope: whole application, with P25 and WFM treated as frozen accepted baselines  
Audit mode: read-only runtime/source review plus existing build and unit gates

## Executive status

The current project has a credible receive architecture and a strong automated
test base. The present evidence supports these conclusions:

- **P25 Phase 1/2:** accepted by the user as clear and continuous. This audit
  does not change, retune, refactor, or otherwise reopen the P25 path.
- **WFM:** accepted by the user as correct. Keep the current WFM/RDS behavior
  as a regression baseline; do not change it while working on other modes.
- **NFM:** usable but not finished. The reported robotic quality is consistent
  with several identifiable DSP risks, but a hardware or known-IQ acceptance
  run is required before selecting a fix.
- **AM:** not field-qualified. The implementation exists and has unit coverage,
  but there is no evidence yet for weak-signal, overmodulated, fading, carrier-
  suppressed, or 10/20 kHz real-signal behavior.
- **New decoders and satellite tools:** many are buildable and smoke-tested,
  but several are prototype-level or lack an end-to-end live acceptance gate.
- **Release state:** source, executable, documentation, and git tag are not
  currently one coherent version. This is a release/process defect, not a DSP
  defect.

## Invariants for the next work

1. No changes to P25 demodulation, framing, TDMA, slot selection, MAC/ESS,
   vocoder feeding, follow state, P25 audio queueing, or P25 timing.
2. No changes to the accepted WFM audio or WFM/RDS data path except a dedicated
   regression test that proves it remains byte/sample behaviorally equivalent.
3. Every analog change must be isolated to the selected mode and tested against
   fixed recordings before it is allowed into the shared receive path.
4. A decoder is not considered complete because it compiles or produces a
   plausible log line. It must have a bounded input contract, discontinuity
   handling, known-good fixtures, and GUI/CLI parity.

## Chain map

```text
startup / CLI or GUI
  -> DeviceManager ownership, sample rate, center frequency, gain, PPM
  -> IQ producer and recent-IQ/spectrum views
  -> DDC, channel FIR, mode demodulator, audio LPF, squelch, resampler
  -> AudioEngine ring and miniaudio playback devices
  -> mode-specific tap/decoder (RDS, tones, SSTV, satellite, aircraft, etc.)
  -> GUI state, CLI output, persistence, diagnostics and release packaging
```

The architecture also has secondary consumers that can retune the same device:
P25 follow, satcom scanning, pass tracking, aircraft map, replay, capture, and
remote control. They need an explicit resource-ownership policy before the app
grows further.

## Verified baseline

The Release build completed for `SDR_Town`, `sdr_town_tests`, and
`sdr_town_workspace_tests`. CTest completed 3/3. Additional targeted runs also
passed:

- `[demod]`: 5 test cases, 546 assertions.
- `[rds]`: 10 test cases, 23,235 assertions.
- `[satcom]`: 9 test cases, 266 assertions.
- The complete Catch2 binary currently reports 325 test cases.
- `git diff --check` found no whitespace errors.

These are valuable regression gates. They are not a substitute for live RF
acceptance, and none of these results should be interpreted as proof that AM or
NFM audio is field-correct across hardware and signal conditions.

## Findings by chain

### 1. Startup and lifecycle

**P1: native receiver teardown is still a risk.** `DeviceManager` can detach a
real initialization thread and can detach an RX worker after a short join
window when a native driver read is stuck (`src/DeviceManager.cpp:1285-1375`).
That prevents the GUI from waiting forever, but it leaves a native device and a
worker with an intentionally unresolved lifetime. This is a correctness and
shutdown-safety issue, especially during retune, device removal, and exit.

**P1: the stub-to-real handoff is operationally complex.** Streaming starts a
stub path before the real device is necessarily ready (`src/DeviceManager.cpp:
870-1012`). Every RF-dependent consumer must distinguish real IQ, stub IQ,
stale IQ, and a source generation boundary. The source contract should carry a
source id, generation, sample cursor, timestamp, and gap/discontinuity reason.

**P2: early crash logging is append-only.** The startup log is intentionally
useful, but `main.cpp:90-109` does not show rotation or a size ceiling. It should
be bounded independently of the rotating spdlog sink.

**P2: version truth is split.** The repository currently describes project
version `0.2.73` in CMake/README, while the checked-out git description is
`v0.2.66-dirty`, and several task/build documents still describe 0.2.59-0.2.62.
Release automation must establish one source of truth and reject mismatches.

### 2. Device, IQ, spectrum and tuner ownership

**P0/P1 architectural gap: no central resource lease.** Satcom scanning and
pass tracking call `DeviceManager::setCenterFreq` directly from worker code
(`src/SatcomScannerEngine.cpp:484-552`), while the main receiver, replay,
capture, aircraft map, and P25 follow also use the same device. A feature can
therefore retune or consume recent IQ belonging to another feature. Add a
cooperative receiver lease/scheduler with owner, priority, requested center,
bandwidth, sample rate, and preemption reason. Do not implement this inside
P25; make it a surrounding device contract.

**P1: recent-IQ windows are not a complete stream contract for secondary
decoders.** The satcom scanner repeatedly requests a fixed recent window and
creates a new demodulator (`src/SatcomScannerEngine.cpp:384-432`). That can
duplicate or omit samples at window boundaries and gives decoders no explicit
gap or epoch information. Secondary consumers need a cursor-based feed or a
documented overlap/deduplication layer.

**P2: spectrum and audio consumers need provenance.** The waterfall, classifier,
capture, decoder, and audio paths should be able to state which device,
generation, center frequency, sample rate, and RF-time interval produced a
block. Without this, a visually plausible signal can be associated with the
wrong tuned frequency after a retune.

### 3. Analog DSP

#### WFM: frozen regression baseline

The current WFM chain has a high-rate path, de-emphasis, pilot/notch handling,
MPX/RDS tap, final LPF and resampling. The existing WFM continuity and RDS
tests pass, and the user reports that WFM is perfect. Do not modify this chain
while repairing NFM or qualifying AM. The only immediate work is a regression
fixture that proves WFM audio and RDS remain unchanged.

#### NFM: usable, but the robotic quality has plausible causes

The code contains a staged NFM front end (`src/Demod.cpp:561-724`) with a
boxcar/CIC-like reduction, a sharp channel FIR, and decimation before the
discriminator. The final path also applies fixed 750 microsecond de-emphasis,
an NFM-specific final LPF, squelch/fade, and hard clipping (`src/Demod.cpp:
948-1049`). The following are concrete investigation items, not assumptions:

1. **Integer-rate staging:** the first reduction uses a rounded integer factor.
   At arbitrary SDR rates this changes the effective rate and transition band.
   Measure alias energy and discriminator distortion at 2.4, 2.048, and other
   configured rates.
2. **Front-end level normalization:** the NFM path normalizes magnitude before
   the discriminator. This can make weak noise appear voice-like and can make
   limiter/clipping behavior depend on the capture rather than the RF level.
3. **Fixed de-emphasis:** 750 microseconds is a reasonable default for many
   systems but is not universal. It should be a selectable profile, with a
   documented default and an off option for nonstandard/data use.
4. **Chunk-state proof:** FIR, discriminator, de-emphasis, squelch fade, and
   resampler state must be shown partition-invariant under arbitrary IQ block
   sizes. Existing tests cover important DSP continuity, but not the full NFM
   chain against a known real repeater recording.
5. **Signal bandwidth coupling:** channel bandwidth, deviation, audio LPF and
   squelch calibration need a mode/profile matrix for 12.5 kHz, 25 kHz, wideband
   FM, weak signal, and tone-bearing signals.

#### AM: implementation exists, field behavior is unproven

The current AM branch is an envelope detector with a 20 Hz carrier tracker and
normalized envelope output (`src/Demod.cpp:766-779`). It has basic AM energy and
squelch tests, but not a complete AM acceptance suite. The gaps are:

1. No independent real-signal fixtures for broadcast AM, airband AM, HF AM,
   weak carrier, fading, overmodulation, or impulsive noise.
2. The default channel bandwidth is mode-dependent and can disagree with the
   user-selected RF bandwidth; 10/20 kHz behavior must be measured rather than
   inferred from the UI value.
3. No explicit synchronous/product detector or selectable sideband-aware AM
   workflow is present. That is a feature gap for weak or fading AM, not proof
   that the existing envelope detector is incorrect for strong AM.
4. Carrier tracking, DC removal, AGC/level behavior, and squelch need tests for
   carrier loss and reappearance. The acceptance criterion must include clean
   audio, prompt mute, no false opening on noise, and no long tail after loss.

#### USB, LSB and CW

These paths are functional basic workflows. They currently have less field
coverage than WFM and P25. The main gaps are selectable RF/audio bandwidth
profiles, AGC behavior, BFO/offset ergonomics, and recorded HF acceptance.

#### Shared analog risk

The free compatibility wrapper in `src/Demod.cpp:1071-1072` uses a static
`Demodulator`. Current GUI/CLI paths mostly use per-receiver instances, but all
callers of the wrapper must be audited because static DSP state can bleed across
callers, modes, or tests. This is a non-P25 correctness risk.

### 4. Audio output

The original non-power-of-two ring bug is fixed: the active ring is initialized
with a power-of-two capacity and guarded by a runtime check. The callback is
nonblocking and zero-fills underruns, which is a sound real-time policy.

**P1: multi-output sample-rate contract is underspecified.** Each miniaudio
device is requested at 48 kHz, but `AudioEngine::startDevice` stores the last
device's actual rate in the single engine-wide `m_sampleRate`
(`src/AudioEngine.cpp:322-377`). If two devices negotiate different rates,
producer resampling and device callback consumption can disagree. The engine
needs a per-output rate/resampler contract or a hard rule that all active
outputs must use the same negotiated rate.

**P2: output selection behavior needs an acceptance test.** Main-window tuning
currently selects output indices by policy rather than proving the operating
system default is the one the user intended (`src/MainWindow.cpp:4849-4869`).
Test default output, multiple outputs, device removal, and re-enumeration.

The remaining audio backlog is therefore not the old cursor-wrap bug. It is
mainly rate negotiation, device lifetime, producer provenance, and test depth.

### 5. Existing decoders and data paths

**RDS:** recorded MPX, adapter parity, reset and GUI display have strong test
coverage. Keep it behind the WFM regression gate. Live RF acceptance should be
kept as a separate evidence item, not conflated with recorded parity.

**CTCSS/DCS and tones:** the adapters are deliberately display-only and do not
change speaker audio. Known-tone RF acceptance remains open. This is the right
default safety behavior; the missing item is a known repeater fixture.

**SSTV:** recorded Robot36/Martin validation and bounded live-input lifecycle
tests exist. Live RF image acceptance, additional modes, and weather-satellite
mission validation remain open. Do not advertise a universal live decoder.

**AX.25/APRS:** `src/Ax25AprsDecoder.cpp:127-150` uses a simple fixed-frequency
correlator and integrate-and-dump bit clock. It is suitable as an experimental
smoke path, not a robust weak-signal modem. Missing pieces include timing
recovery, filtering, frequency/deviation tolerance, NRZI/bit-stuff edge cases,
and known-good recorded frames.

**NOAA APT:** `src/AptImageDecoder.cpp:16-47` builds envelope pixels using a
fixed approximate samples-per-line calculation. There is no verified line-sync,
channel separation, slant correction, telemetry or weather-image acceptance
fixture. Treat it as a preview prototype.

**ADS-B/Mode-S:** `src/ModeS.cpp:169-204` uses a simplified preamble and bit
decision path. `decodeCprPair` currently ignores the even/odd selection and
always publishes the even solution (`src/ModeS.cpp:80-92`). That is a concrete
correctness bug for global CPR pairing, even though helper tests pass. It needs
separate even/odd global decoding, age/position consistency checks, and live
2.0/2.4 Msps acceptance.

**Inmarsat/Aero:** the demodulator reports heuristic lock/EbNo values and uses
fixed 8 samples/symbol assumptions (`src/InmarsatDemod.cpp:92-160`). The voice
path packs 96 bits directly into an AMBE frame and invokes an AMBE decoder
(`src/InmarsatVoice.cpp:54-80`), but there is no independently verified
air-interface frame/FEC fixture. Keep this experimental until the exact
protocol mapping and clear test vector are proven.

**Satcom scanner:** scanning, lock, recording, and decoder invocation happen in
one worker and retune the shared device. It needs resource leasing, stream
cursor/gap metadata, bounded recording quotas, and mode-specific acceptance
before it can be called production-grade.

### 6. GUI, CLI, control and persistence

The GUI starts no stream automatically on launch (`src/MainWindow.cpp:
5103-5111`), which is a good startup-safety decision. Heavy classifier work is
rate-limited and kept out of the UI pump, also good.

The main remaining risks are:

- `MainWindow.cpp` and `CliApp.cpp` remain very large ownership hubs, making
  cross-feature regressions hard to localize.
- CLI and GUI do not yet have one shared end-to-end analog acceptance harness.
- Secondary feature actions can compete for a receiver without a lease.
- JSON/config loading for satcom uses permissive `value()` reads with little
  range/schema validation (`src/SatcomScannerEngine.cpp:76-105`). Invalid
  frequencies, rates, dwell times, paths, or device indexes should be rejected
  or clamped with a visible diagnostic.
- The local control server intentionally listens on localhost and supports a
  token, which is appropriate for the local control contract
  (`src/SdrTownControlServer.cpp:48-58`, `:200-207`). Any future non-local
  binding must require authentication, rate limits, and explicit opt-in.

## Prioritized backlog

### P0: protect correctness before feature growth

1. Establish one version/release manifest and make the build fail on source,
   README, tag, asset, or update-manifest mismatch.
2. Add receiver leases and a source-generation/cursor/gap contract around
   `DeviceManager`; keep P25 behavior unchanged behind the contract.
3. Define deterministic shutdown ownership for native RX and document the
   bounded fallback when a driver is stuck. Add stress tests for start, stop,
   retune, device removal, and application exit.

### P1: finish analog foundations

4. Build a recorded NFM qualification set: 12.5/25 kHz, strong/weak voice,
   tone-bearing repeater, frequency offset, fading, and noise-only cases.
   Measure audio continuity, RMS, clipping, noise, and intelligibility.
5. Run the same matrix through GUI and CLI and prove identical demod output for
   identical IQ partitions. Then investigate rate staging, normalization,
   de-emphasis and limiter behavior one variable at a time.
6. Build an AM set covering broadcast, airband/HF, 10/20 kHz bandwidth,
   weak/fading carrier, overmodulation, carrier loss and noise. Add acceptance
   criteria before changing the detector.
7. Remove or quarantine the static demod compatibility wrapper after all call
   sites are audited.
8. Give AudioEngine per-output rate accounting or enforce a single negotiated
   rate for all active outputs.

### P1: qualify decoder platform

9. Move all secondary decoders to a shared bounded receive-session contract with
   source identity, cursor, rate, center, and gap semantics.
10. Add live/recorded parity tests for SSTV, RDS, tones and satcom without
    allowing any secondary worker to retune an unleased primary receiver.
11. Fix ADS-B global CPR even/odd selection before treating aircraft tracking
    as production-correct.
12. Replace prototype AX.25/APT assumptions with timing/sync/FEC/line-sync
    stages and known-good fixtures.
13. Keep Inmarsat voice behind an explicit experimental capability until an
    independently verified protocol vector proves the frame layout and FEC.

### P2: product polish and expansion

14. Reconcile README, `docs/TASKS.md`, build notes and feature-alignment docs
    with actual shipped behavior. Remove claims that are only roadmap items.
15. Add per-mode status panels showing source, RF sample rate, effective RF
    bandwidth, audio rate, squelch state, queue fill, underruns and gaps.
16. Add a testable band-plan/schema validation layer and explicit country/data
    provenance for every band label.
17. Split the largest GUI/CLI orchestration modules around stable contracts;
    do this incrementally and never as part of a DSP bug fix.

## Acceptance plan for the next work

### NFM exit criteria

- Same known IQ capture decodes identically through CLI and GUI.
- Arbitrary producer block sizes produce the same PCM within a defined numeric
  tolerance.
- No NaN/Inf, sustained clipping, or false audio on noise-only input.
- 12.5 kHz speech is intelligible at strong and weak levels, with documented
  deviation, de-emphasis and LPF settings.
- Tone/DCS/CTCSS taps do not alter the speaker PCM.
- Retune and stream gaps reset only the intended state and do not leak old audio.

### AM exit criteria

- Known-good AM fixture produces intelligible audio at 10 and 20 kHz RF widths.
- Carrier loss closes squelch promptly without a long tail or false reopening.
- Weak/fading carrier does not create pumping, severe distortion or false speech.
- Overmodulation is bounded without destroying normal speech.
- Detector, AGC/carrier tracking, LPF and squelch behavior are visible in logs.

### Frozen regression criteria

- P25 current acceptance captures remain unchanged and are run only as a
  no-regression gate.
- WFM audio/RDS capture and the current GUI behavior remain unchanged.
- No P25 source file is modified as part of NFM, AM, decoder, or ownership work.

## Recommended execution order

1. Freeze and label the current P25/WFM binaries and captures.
2. Reconcile version/documentation truth without changing DSP.
3. Add receiver ownership/provenance and lifecycle tests.
4. Build the NFM and AM capture/measurement harness.
5. Fix NFM only after the capture matrix identifies the dominant mechanism.
6. Qualify AM with the same discipline.
7. Harden secondary decoders and satellite ownership after analog acceptance.

This is the foundation. The next implementation pass should begin with the
P0 ownership/provenance tests and the NFM/AM fixture harness, not with a broad
refactor or a change to P25.
