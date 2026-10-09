# Decisions

## DEC-0208 - Per-USB live IO and WASAPI prime for all device combos (2026-10-09)

Collector client 4988148b (Ryzen 9 7950X, Win11, RSPdx + two RTL,
0.2.129–0.2.131): `app.runtime` showed ~100 audio underruns/s with
`ringFillPercent` 0 and `queuedSamples` 0 while the RSPdx was stopped, then
the same empty ring through NFM/WFM/USB/LSB. Playback names were HyperX
Virtual Surround Sound and VB-Audio Virtual Cable. When RSPdx and RTL both
streamed, `liveIoWaitUs` ran to tens of seconds and both radios overflowed.
DEC-0190 already named per-physical-device parallel I/O as a later task.

Decision: keep `Device::make`/`unmake` on a process-global factory mutex.
Key live `readStream`/`writeStream`/tune/gain by USB `stableKey` so different
radios can run together and the same serial still serializes retune vs read
and TX `writeStream` vs `stopTx` try_lock. Open WASAPI as stereo 48 kHz with
a 20 ms period hint, duplicate the mono ring to every callback channel, init
on prewarm, and start playback only after two periods (at least 40 ms) are
queued, or immediately for a test tone. Do not invent PLC or change P25
slot/security/vocoder. Exact frozen-path digests: DeviceManager.cpp
72a1200f… → 7f27cd25…, AudioEngine.cpp e4219b87… → b622f93e….

## DEC-0207 - stopTx must not wait forever on gSoapyLiveIoMutex (2026-10-08)

`stopTx` waited 500 ms, detached the TX thread, then `lock()`ed
`gSoapyLiveIoMutex` before `unmake`. `writeStream` holds that same mutex.
A wedged driver therefore stalled `stopAllTx` in the DeviceManager
destructor and hung process exit. Hardware emission is `startToneTx`, not
the P25 voice stub.

Decision: `DriverIoMutex::try_lock` succeeds only when the lock is free and
no ticket is waiting. It must not take a ticket on failure. `stopTx` uses
`std::try_to_lock`. If the mutex is busy, detach the Soapy pointers from
`TxStreamState`, keep the handle on a leak list, and return. A later
`stopTx` that wins `try_lock` reclaims leaked handles. `writeStream`
re-checks the pointers under the mutex. No new wait budget. No P25, RX
detach, or voice-heuristic change.

## DEC-0206 - Loopback control is default-deny (2026-10-08)

`SdrTownControlServer` listened on `127.0.0.1:8765` with
`allowUnauthenticated = true` and `GuiRuntimeConfig.controlAuthRequired = false`.
`requestAuthorized` treated an empty token as authorized. `/v1/health` is
version-only, but `GET /v1/status` returns frequency, mode, and
`activeDevice.serial` whenever the handler is installed. Same-machine clients
did not need a wedged driver.

Decision: require a token by default. `Config.allowUnauthenticated` and
`controlAuthRequired` flip to fail-closed. `start()` still refuses a missing
token unless the operator passes `--control-allow-unauthenticated`.
`requestAuthorized` no longer treats an empty token as success. Presented
Bearer / `x-sdrtown-token` values are compared in constant time over UTF-8
bytes. `/v1/health` stays unauthenticated. Query-to-body merge is unchanged.
Do not change P25, TX teardown, or RX detach in this patch.

## DEC-0205 - Do not inherit saved satellite auto-capture at process start (2026-10-07)

Opening SDR Town jumped the spectrum to SO-50 FM 436.795 MHz and refused Listen
retune. That frequency is the built-in selected `so50-fm` downlink, not P25 and
not the Record Enc Grant IQ control (which never sets 436.795).
`SatcomScannerConfig.autoCapture` defaults true and is restored from the last
session. `SatcomHubWidget` starts a 1 s timer in its constructor, even when the
Listening workspace hides the Satcom dock. `autoCaptureTick` then `armPass(...,
force=true)` and takes the Listen radio. `canUseDevice(Listen)` fails afterward,
so Monitor Freq / spectrum clicks cannot change anything.

Named Satcom sessions already forced `autoCapture=false` until a fresh operator
arm this run. The default hub engine did not. DEC-0176 only suppresses the
timer during GUI dry-run and explicitly left normal auto-capture unchanged.

Decision: every `SatcomScannerEngine`, including the default hub singleton,
starts with auto-capture off after loading settings. Checking **Auto capture
selected sats in range** still enables it for the rest of the session and still
saves the preference. DEC-0185 still applies once a pass is owned: hiding the
dock does not stop it. Do not change P25, encrypted mute, or hop timing.

## DEC-0204 - IQ-only encrypted grant/follow capture, speaker stays muted (2026-10-07)

GUI Start IQ Capture already writes SigMF IQ plus `_p25_log.txt` and
`_events.jsonl`. Auto-follow still skips known-encrypted grants
(`auto-skip-encrypted`) and `evaluateP25Follow` returns immediately on
encrypted proof (`ReturnEncrypted`). CLI `p25 waitgrant follow` also skips
known-encrypted TGs. Manual Follow TG can tune a Phase 2 encrypted row, but
the follow SM still bounces to CC as soon as ESS/grant proves encrypted, so
the traffic dwell is truncated.

Recording RF of an encrypted grant+follow does not require opening speaker
audio. Encrypted mute (SoT L1 / REQ-P2.6) stays fail-closed.

Decision: add an explicit GUI control that (1) starts live IQ capture,
(2) enables auto-follow of **known-encrypted** grants only while armed,
(3) sets `P25FollowSnapshot.holdEncryptedForIqCapture` so the tuner stays
on traffic until teardown / carrier-drop / user stop instead of
`ReturnEncrypted`, and (4) writes timing rows into the capture log/events.
Do not save decoded WAV, do not soften slot/ESS speaker gates, and do not
change default clear-follow behavior.

## DEC-0203 - Keep healthy 80/4 on first locked-lattice empty hop (2026-10-06)

Capture `20261006_093930_588` on v0.2.128 is RF-gapless and listen=CLEAR, but
mid-call CADENCE still dips (example 09:45:14 duty 0.439, seq 1061→1064). The
empty hops between those emits still have SF+mask locked (`p2bursts=8`,
`p2sf=8`, `p2mask=8`, `p2mac=4/4`, `targetVcw=0`). DEC-0039/0048 treats
post-emit `targetVcw=0` as eye-lost and DEC-0048 escalates the **next** hop to
cand=16 / 120 ms. That hop is often the next unique-speech window, so empty
companion/dup slices occupy the single-flight worker longer than a healthy
emit (80/4).

True lost-eye still needs immediate cand=16 (DEC-0048 / capture `041612`
permanent `no-vcw`). Two consecutive locked-lattice empties still escalate so
DEC-0039 companion-burst re-lock is preserved.

Decision: extract `p25Phase2PlanLiveHotSearch`. When SF+mask are still held,
the first post-emit empty hop stays on healthy `kP25LiveHealthySustainBudgetMs`
/ cand=4. Escalate cand=16/120 only on streak ≥
`kP25LiveLockedLatticeEmptyEscalateStreak` (2) for locked lattice, or on the
first miss when structure is gone. Do not raise minFresh, thin 280 ms overlap,
raise the 80 ms healthy abort, invent PLC, or soften slot/security.

## DEC-0202 - Do not retune cadence or invent PLC for residual 20 ms lattice gaps (2026-10-06)

Live capture `20261006_090937_216` on v0.2.128 (DEC-0201) is RF-gapless
(`ok_gapless`, zero overruns/resets, no `readStream -4` in-session) at
2.048 Msps for 278.624 s. Dominant hops are 208 ms fresh + 280 ms context
(849/903 submits), matching the 200 ms active-clear minimum. Longest
clear epochs run CADENCE duty 0.88–1.07 (10-window TG10327 mean 0.922, all
`drop=ok`); first TG10327 island is 0.88–1.07. Listen classifier labels
the 50.08 s live speaker WAV **CLEAR**. Mixed-slot/unsafe output, `seqDrop`,
queue/result/producer drops, and non-silent 20 ms repeats are all zero.
`wrongSlot` counts are companion-slot rejects (`targetVcw=0`, `gate=empty-audio`).

The only remaining digital speaker holes are two exact-zero 20 ms frames in
50.08 s, mapped to emit hops `seq=471` TG10327 and `seq=489` TG10329. Both
have `gaps=1`, `fed=emitPcm`, ringFill ≈49%, and imperfect MAC (`p2mac=4/6`
and `3/4`). They are bucket-A lattice misses, not cadence starvation. Raising
active minFresh to 220/240 ms already regressed a TG30302 interval on the
081738 sweep (DEC-0201). Inventing PLC or softening slot/security gates is
forbidden. Short first-eye PTTs still show acquire `drop=A/D`; that is not
the 160 ms residual D that DEC-0201 closed.

Decision: keep 240/200/280 on the active-clear path. Do not raise minFresh,
thin overlap, raise the 80 ms healthy-sustain abort, or synthesize missing
AMBE. Treat the two 20 ms zeros as RF/lattice remainder, not a new hop bug.

## DEC-0201 - Require 200 ms fresh IQ for active-clear backlog catch-up (2026-10-06)

Capture `20261006_081738_771` on v0.2.127 is RF-gapless at 2.048 Msps
(zero ring overruns/gaps/resets) and restores clear audio, but active clear
epochs still under-run the speaker ring at live duties 0.582 and 0.633.
The complete pipeline trace has 862 submitted/started jobs, queue latency
p50 0.02 ms / p95 0.03 ms, DSP p50 76.39 ms / p95 92.92 ms, no
queue/result/producer drops, no non-monotonic completion, no unsafe or
mixed-slot speaker output, and no speaker ordinal loss. The dominant active
worker shape is still 160 ms fresh + 280 ms context.

The same saved IQ was replayed across six clear call intervals at fixed
160/180/200/220/240 ms fresh hops with the unchanged 280 ms context. Relative
to 160 ms, 200 ms was the only measured point that preserved or increased
speaker PCM on every interval: aggregate 35.38 s -> 37.00 s, with speaker
timeline drops 3 -> 0 and sequencer suppressions remaining zero. A 240 ms
minimum produced more aggregate PCM (37.84 s) but regressed one TG30302
interval from 4.52 s to 4.12 s, so it is not selected as a global minimum.

The live speaker WAV has 1,758 aligned 20 ms frames. It contains zero adjacent
non-silent exact repeats, zero adjacent near-repeats above 0.995 correlation,
and zero non-silent exact repeats at lags 2-5. The one exact adjacent pair is
silence. Validation records contain no codec `R` repeat and live `seqDrop=0`.
The reported repeat is therefore not reproduced as software replay; do not add
payload-hash de-duplication or reset mbelib.

Decision: retain the proven 240 ms maximum and 280 ms overlap, but require
200 ms fresh IQ only on the active-speaker clear backlog path. Keep the
non-active speaker backlog minimum at 160 ms. Do not change slot/security
gates, hard RF-quality handling, the audio jitter buffer, follow timers, or
invent PLC. Live multi-call qualification remains required before closing the
product continuity gate.

## DEC-0200 - Revert realtime catch-up after AMBE/slot audio collapse (2026-10-06)

Capture `20261006_075758_372` on v0.2.126 is gapless (`ok_gapless`, zero ring
overruns) after the spectrum-worker move, but live speaker WAV is empty (44-byte
header only). CADENCE shows mass `Phase 2 AMBE rejected` / `ambe-rejected-zero-accepted`
with `ambe=N/0`, avg duty ≈0.10, and scheduler hops dominated by `fresh=327680`
(160 ms @ 2.048 Msps) once `phase2SessionHadBurstEye` makes `activeSpeakerClearPath`
true. The same trunk on v0.2.125 (`20261006_062201_289`) had zero AMBE-rejected
lines, duty ≈0.23, and a 3.4 MB speaker WAV.

Decision: remove the e7870ef/DEC-0199 realtime catch-up profile
(`kP25Phase2VoiceDecodeSpeakerRealtimeCatchUp*`) and restore shared DEC-0061
240/160/280 speaker backlog catch-up for active-clear and non-active paths.
Keep the DEC-0199 spectrum worker (bucket A IQ loss repair is still valid).
Do not invent PLC or soften encryption/slot gates. Live re-prove required.

## DEC-0199 - Repair measured P25 emit gaps from IQ loss and catch-up cadence (2026-10-06)

Capture `20261006_062201_289` (2.4 Msps, CC 420.350, NAC 2D2) proves two defects
that produce small clear-P25 audio emit gaps without encryption, wrong-slot, or
queue/result/producer drops:

1. Bucket A: ten P25 cursor discontinuities and thirteen recording gaps totaling
   1,906,496 samples; `sdr_town.log` reports `readStream -4` about every eight
   seconds. Spectrum FFT/rate publication still ran on the Soapy read loop.
2. Bucket D: after e7870ef raised active-clear catch-up to 360 ms max fresh with
   280 ms overlap, decode latency improved (p50 94 ms / p95 127 ms) but
   `minFresh=280 ms` forced roughly 325 ms submissions for about 280 ms of real
   PCM, leaving measured underrun climbs.

Decision: move spectrum FFT/publication to a joined per-stream worker that reads
the existing IQ ring, leave retune/driver serialization unchanged, and set the
active-clear realtime catch-up geometry to 360 ms max / 160 ms minimum / 280 ms
overlap. Normal sustain and non-active backlog geometry stay unchanged. Do not
invent PLC, soft-mute encrypted audio, or change slot/security gates. Replay of
this capture is decoder-only evidence because the saved IQ already contains gaps;
live multi-minute 2.4 Msps qualification is required for A closure.

## DEC-0198 - Validate and repair the Classic Aero audit findings (2026-10-05)

The audit was checked against source commit70525b5, not only comments or release
notes. Three concrete defects are repaired without touching P25: ACARS block
identifier reassembly now advances through both the A-Z and 0-9 cycles; an
8400 C-frame with no CRC-valid subunits or a codec erasure no longer resets the
persistent voice state; and the channelizer applies symmetric deterministic
headroom before converting to signed PCM. M/E/T codec words are muted before
they reach the audio sink, while repeat words remain available for concealment.

The scheduler's two-channel/30-second defaults are deliberate live-load policy,
not protocol correctness failures. The UI already permits a longer position
survey and operators should use the documented survey profile when mapping
slow ADS-C traffic. Parser framing/BCS strengthening and a post-codec speech
filter remain open until a protocol fixture proves the exact accepted variants;
they are not guessed into the receive path. P25 files and DSP behavior are
explicitly out of scope.

## DEC-0197 - Honest aircraft direction and off-capture watch planning (2026-10-05)

User confirms 1529.000-1530.000 MHz. Allow explicit Inmarsat spectrum planning
outside the received span, but leave unreceived RF blank and never retune merely
because the operator pans. Existing watch scheduling remains authoritative.
Rotate map aircraft only from validated ground track; unknown motion must not
imply north. InmarsatTracking already publishes groundTrackDeg but the map
ignores it. AircraftMap already rotates tracks; audit validity separately.
Clarify requested ADS-B sample rate versus applied device rate, keeping existing
wide-rate API compatibility. Audit ACARS identity/position and voice association
without inventing positions or changing P25. Tests precede release claims.

Audit extension: AdsBTrackStore routes TC20-22 to decodeVelocity, which only
accepts TC19. FlightAware dump1090 mode_s.c decodeESAirbornePosition and its
message dispatch confirm TC20-22 use airborne CPR position fields. Route these
through the existing CPR decoder; no altitude conversion is claimed here.
Reference: https://github.com/flightaware/dump1090/blob/master/mode_s.c
Fixtures retain known CPR payloads, change TC and recompute Mode-S CRC24.


## DEC-0196 - Close finite distribution requirements with verified materials (2026-10-05)

User prioritizes completion of ISS-0060 and authorizes necessary licensing
changes. The current four blockers are unconditional strings, not tests which
can become satisfied. Replace them only as actual materials and qualification
are supplied. Preserve exact-file/source, tamper, loader and replacement gates.
No P25, receive, playback or security-policy changes in this task.

Retain MIT on original source contributions and all upstream grants. Select
GPL-3.0-or-later for distribution of the combined RTL-enabled application,
using librtlsdr's explicit GPL-2.0-or-later option. Ship the GPL text, exact
application/dependency sources and rebuild/replacement instructions. This
does not relabel vendor runtimes or claim patent/legal certification.

Inventory embedded libraries/data and Qt runtime attributions from the exact
source kits. Include compiler-runtime notices and record the actual RDS GCC
toolchain/static inputs, not an inferred version. Pin a redistributable,
documented MinGW toolchain for official builds if the old local installation
cannot establish its runtime provenance. RDS behavior must pass existing gates.
Rebuild RTL/libusb/pthreads independently from the shipped vcpkg tool/source
kit without binary caches and load the replacements in a disposable package.
Tests must reject missing, altered or mismatched release evidence. Release
qualification remains finite; already-passed Qt work is not a new blocker.

Primary references: Qt open-source obligations, the shipped RTL source headers,
and GCC's libstdc++ license / COPYING.RUNTIME. Publisher authorization cannot
waive other authors' rights. Any new incompatible component must fail review.

CI follow-up: the clean runner uses Jansson2.15.1, whose upstream LICENSE
explicitly includes Lucent's dtoa permission, unlike the older local receipt's
MIT-only expression. Accept the reviewed MIT AND dtoa expression only with its
original notice present; retain the source/receipt checks. Pin CI vcpkg to its
observed commit19780d9cdf84d0944cf9a318666703b89ab6629c so future runs cannot
silently change dependency versions. Run source/attribution preflight before
application compilation, not only during final packaging.

## DEC-0195 - Complete the USB build-helper closure (2026-10-05)

T-0104 inspection of the exported libusb1.0.30 recipe confirms its Windows
dependency on vcpkg-msbuild. That helper depends on vcpkg-cmake-get-vars and
vcpkg-pkgconfig-get-modules; the latter uses pkgconf, whose recipe depends on
vcpkg-tool-meson. DEC-0188 exported only cmake/config helpers. Include the six
installed helper sources/receipts and tracked pkgconf recipe, and reject a
missing/tampered helper even if the outer manifest is regenerated. Do not ship
pkgconf runtime DLLs in the app. This is a concrete tooling repair, not evidence
of an independent full USB rebuild or completed distribution review.

## DEC-0194 - Passive correlated P25 capture evidence and stale CC fencing (2026-10-05)

T-0108 / ISS-0073. User reports lost syllables and late initial callers. Do not
change DSP budgets, speaker holds, slot/security rules or acquisition constants.
Add bounded POD trace events to the existing capture events JSONL: monotonic
time, job/session/generation, IQ positions, tuning context, queue/decode/publish
timing and PCM counts. Capture writer alone formats/writes; producers try-lock
and count overflow/contention. Capacity4096 is a diagnostic memory bound, not
a receive timing constant. No payload/PCM copying or audio callback logging.
Trace loss must be visible, not mistaken for lost RF/audio. Record unknown RID
as unknown. Wall-clock anchors correlate logs; latency uses steady_clock.

Code inspection: pending CC results carry no source epoch/context; trusted
offset is published by the worker before any stale-result check. Bind results
to device/stream epoch/reset generation/center/rate/target and reject changed
contexts before updating offsets/analyzer/grants. Test out-and-back retunes and
same-context acceptance. This is a concrete stale-result defect, not proof that
it caused every reported delay. Concurrent in-passband CC remains separately open.

Paired replay evidence (p25-follow-20261005) shows validation-on changes PCM
hashes while equal settings are repeatable. p25ResolvePhase2AmbeFrame runs
throwaway synthesis on logging enable; mbelib synthesis consumes shared rand().
Remove those probes from passive validation/deep logging. Preserve explicit
probe APIs for forensic callers; canonical mapping and decoder stay unchanged.
Require identical-IQ replay logging on/off and focused/full regression gates.

Publisher confirms Visual Studio/Build Tools licensing and redistribution
entitlement this turn. Record that confirmation separately from exact runtime
materials and other still-open package requirements; do not waive them.

Follow-up inspection: recent-window CC reads honor hardware-loss floor but not
retuneValidFromAbsolute; chronological voice reads honor both. Thus the first
256ms CC window after return can include pre-retune traffic IQ under new tuning
metadata. Reproduce with controlled pre/post-retune sample markers. Add an
explicit current-tuning-only recent read for GUI/CLI CC, preserving capture and
all other callers' existing continuous ring. Publish the applied RF center and
retune boundary under the ring lock. Do not shorten settle times or truncate IQ
captures. An epoch is not proof that every sample in a raw recent window belongs
to that epoch. Test empty post-retune input and exact subsequent sample identity.

Live qualification of the first repair exposed an initial-open contract gap:
appliedCenterHz remained zero until a queued retune, so the new CC reader could
not decode after startup. Record a successfully applied initial center after
stub-ring reset and before publishing real RX; do not infer it from GUI intent.
Also record successful PPM-path retunes. Test initial metadata before the fixture
issues any subsequent tune. Failed tuning must not manufacture applied metadata.

## DEC-0193 - Confirmed traffic teardown, not shorter silence guesses (2026-10-05)

T-0108 / ISS-0073; baseline d3975a3. Replay capture003120, skip71000ms,
center420.08875MHz, TG10120/s0 recovers CRC-valid FACCH END_PTT at absolute
dibits458616,459696,460056 (capture76.436/76.616/76.676s). The reply grant
was present by90s while live waited until97.253s to return. Preserve the IQ.
Reference: SDRTrunk P25P2DecoderState.processEndPushToTalk treats two FACCH
END_PTTs as traffic teardown. Do not treat HANGTIME, a single END, invalid CRC,
companion-slot END or overlapping copies of one END as equivalent evidence.

Add selected-slot, CRC-validated, distinct-position FACCH-end confirmation to
the observer, reset on a new selected PTT/ACTIVE/voice, and propagate it with
matching allocation/session identity to both GUI and CLI follow snapshots.
Return only after the existing observer hold and immediate speaker-drain grace;
skip GUI warm standby on this confirmed release (it otherwise disables CC
decoding for another5s). Keep all silence/acquisition/security/DSP timings.
Verify observer reuse against session/TG/frequency/slot, not session alone.
Use failing unit fixtures, identical-IQ WAV comparison and live GUI follow
before acceptance. Concurrent in-passband CC decode remains a separate change;
do not re-enable its old unbudgeted path to disguise this lifecycle defect.

## DEC-0192 - Reproduce traffic observer ordering before repair (2026-10-05)

T-0108 inspection: P25TrafficChannelProcessor::observeDecodeResult ORs all
selected-slot END/IDLE/HANGTIME messages in a decode batch, overriding a later
PTT in that batch. Test END followed by a clear PTT/voice and the reverse order,
including opposite-slot isolation, before considering changes. Reference:
_codex_refs/sdrtrunk/src/main/java/io/github/dsheirer/module/decode/p25/phase2/
P25P2DecoderState.java processMacMessage/processPushToTalk/processEndPushToTalk
process each message in order. This observer affects follow health, not the
vocoder directly; a failing fixture alone does not prove the reported field
failure. No timing, squelch, security or decoder algorithm changes authorized
by this investigation. The first live run was interrupted by a user-confirmed
USB unplug/replug and contains no validated CC; retry before RF conclusions.

The [response-order] fixture fails on6805bf6 (ended remains true after the
new PTT); reverse-order and opposite-slot fixtures pass. Permit a narrow
observer repair: fold selected-slot boundaries in capture order, preserve the
first ended timestamp until a real selected-slot restart, and retain existing
security predicates/hold durations. Add repeated-IDLE/end-timer tests before
repair. This is not a change to PCM release or CQPSK/AMBE. Live field causality
and identical-IQ non-regression remain separate acceptance requirements.
Overlapping windows must not replay old END/PTT state or renew activity. Add
absolute-dibit ordering/idempotence cases, including out-of-order input, an
unknown-security next PTT, and a new selected-slot call after teardown. Only
fresh selected-slot observations may advance the observer. Unknown absolute
positions retain stable in-batch order; never infer a global clock from them.

Live capture20261005_003120 confirms another accounting defect: at00:32:35
TG10120/slot0 has zero VCWs while slot1 TG12068 carries voice. The observer's
p2vcw=max(global, selected) publishes the companion's activity to both GUI and
CLI follow health. Offline CC replay at skip90000ms, center420.08875MHz recovers
CRC-valid TG10120 grant0x70D9 (421.350MHz/slot1/RID0x1FA4E6) while live remains
on420.100 until00:32:57.668 and follows the new channel at00:32:59.921.
Repair p2vcw to fresh, masked, selected-slot/TG codewords; keep aggregate VCWs
in a separate diagnostic field. No guessed shorter hold, slot flip, CC worker
concurrency or audio-gate change. Add mixed-slot/unmasked/overlap and follow
snapshot regression tests. CC-in-passband monitoring remains a separately
measured gap: this patch does not enable the intentionally suspended CC worker.

## DEC-0191 - Capture first-caller/response boundaries before P25 repairs (2026-10-05)

T-0108 / ISS-0073. User reports fully clear initial follows but missing second
speakers. Preserve current source/binary/settings hashes and recent logs.
Record ten minutes of GUI auto-follow IQ on saved420.350MHz with synchronized
events, validation and speaker evidence, using D: (C: has about7GB free).
Keep current RF gain/PPM, normal encryption policy and production timing.
Disable remote uploads for private diagnostic runs. Trace response transitions
across CC grant/channel resolution, TG/RID/alias identity, traffic retune/slot,
MAC/PTT/ESS context, VCWs, PCM queue and speaker release. A second RID on one TG
does not prove a slot change; lack of PCM does not prove encrypted RF. Require
observed evidence and failing regression cases before modifying decoder/follow
behavior. STT is corroboration, never sole proof of valid/continuous voice.

## DEC-0190 - FIFO admission to the existing live-driver lock (2026-10-05)

T-0103 / ISS-0072: the new four-controller mixed workflow fixture fails while
the fourth radio opens. In controllers-isolation-stress.log, gain completes at
09:20:16.642 but PPM/ready waits until09:20:30.425; another queued tune waits
13519ms. All live read/control calls contend on gSoapyLiveIoMutex, a non-fair
std::mutex. Active read loops can repeatedly reacquire it ahead of startup.
Preserve the existing serialization and critical sections (including IQ epoch
publication), but admit waiters FIFO using a mutex/condition-variable ticket
queue. Do not increase startup timeouts, remove the mixed test, move driver
calls outside the lock or change P25 DSP/gates. Record exact DeviceManager
type-only lock replacement in the frozen-path guard and reject mutated bodies.
Test deterministic admission order, the previously failing mixed controllers,
driver lifecycle regressions and full suites. This prevents waiter starvation,
not a permanently wedged driver; per-physical-device parallel I/O remains a
separate qualification task. No new dependency is introduced.

## DEC-0189 - Repeatable satellite and aircraft controllers (2026-10-05)

T-0103 continuation from26716b3. Satcom scanner/pass planner and Aircraft
track store/settings are shared singletons; Aircraft hide also stops local RX.
Named Satcom sessions must own a planner, scanner, decoder histories, output
queue and persisted configuration. Named Aircraft sessions must own their track
store/CPR state and radio worker, with separate source/network settings. Preserve
the default engines and public legacy routes. A tab switch never stops RF.
Explicit Stop/Close/application shutdown joins only the addressed workers before
destroying shared services. Async TLE completion must be lifetime-bound, not a
raw pointer into a closed session. Newly opened/saved sessions stay idle and
cannot auto-select another session's radio; named auto-capture is explicitly
armed in the current run. Named audio must not append to the primary/P25 queue.
Expose named controls through the satellite workspace and authenticated API,
using the existing 16-session-per-workflow resource budget and dry-run RF gate.
Validate real independent workers on mock hardware, settings/state isolation,
closed/invalid IDs, hide/stop/reopen and legacy endpoints. No RF quality claim
comes from mocked IQ. P25 controller extraction needs its separate dependency
inventory; do not duplicate MainWindow or claim a pool decodes concurrent calls.

Inspection also finds SatcomAsyncLog's UI and worker publish concurrently into
an SPSC overwrite ring, while its directory string is mutated without a lock.
Serialize only the bounded POD queue operation with try-lock producer admission;
count contention/overflow and format/write on the writer. Synchronize directory
snapshots separately. Shutdown drains retained events. Concurrent producers must
account exactly for consumed plus dropped events, with no torn payloads. This
does not put logging I/O or waits into the radio/audio path.


## DEC-0188 - Include the dependency build-tool sources (2026-10-05)

T-0104: the existing dependency archive contains upstream sources and port
recipes, but not vcpkg's scripts, triplets, bootstrap metadata or installed
vcpkg-cmake helpers. Export those exact tracked inputs from the configured
vcpkg checkout into a separate bounded ZIP. Verify installed helper-file hashes
against their SPDX receipts, reject modified selected tooling inputs and bind
the archive to both application and vcpkg commits. Preserve upstream notices.
Never sweep download caches, untracked files or user configuration into it.
Record bootstrap/tool prerequisites honestly; this does not contain a compiler,
prove an independent USB-stack rebuild or resolve distribution review.

## DEC-0187 - Independent Inmarsat receiver sessions (2026-10-05)

T-0103: every Inmarsat widget, monitor and settings write still targets the
singleton. Convert the engine to an explicitly owned QObject with a validated,
case-normalized session ID, independent settings/message store/IQ cursor/worker
and exact existing radio lease. Keep the empty-ID singleton for GUI/CLI/web
compatibility. New sessions must not automatically start RF or copy another
session's active radio. Expose named sessions in the satellite workspace;
closing a session joins only its worker and application shutdown joins all.
Queue failed-worker restoration to the engine's lifetime-bound QObject, not
the application with a raw pointer. Do not alter modulation/FEC/audio algorithms.
Scope decoder monitors, popouts, map caches/consent and message lists to their
engine. Shared aircraft overview remains an aggregate of validated positions.
Record session identity locally; existing remote consent/filtering stays intact.
Allow up to 16 named session tabs as a UI/worker resource budget. Reopen saved
tabs idle, never restart radios automatically. Authenticated
/v1/inmarsat/sessions opens/configures/starts/stops/closes these exact controllers;
stale/invalid identities must not fall back to the default. Dry-run rejects RF
start and must not persist its test-only tab layout.
The current diagnostic recorder selects by frequency, not engine identity;
refuse ambiguous multi-session recording instead of silently capturing another
radio. Test persistence, isolated controls/maps, missing-radio rejection,
multi-radio workers and stop/destruction with existing mock hardware.
Repeated P25/Satcom/Aircraft engines remain separately tracked, not implied by
this implementation. Release-material work remains T-0104, not license approval.

## DEC-0186 - Ship the exact tracked source tree with package evidence (2026-10-04)

T-0104: the vcpkg and Qt source kits do not include SDR Town's own embedded
libraries, data, build scripts and pinned Git submodules. Add a bounded source
ZIP derived from the committed Git objects, not a developer-directory copy.
Reject modified tracked inputs, unavailable/mismatched submodules, symlinks,
unsafe paths and duplicate archive entries. Ignore unrelated untracked files
(captures, local reference clones and credentials must never be swept in).
Bind every member hash and the submodule revisions to the package source SHA.
Test missing/tampered members, wrong revisions, dirty sources and untracked
secret exclusion. This provides sources, not a blanket licensing clearance.
Correct the stale Qt blocker wording: full replacement rebuild already passed
on CI 37198827652; the separate distribution review remains unresolved.

## DEC-0185 - Separate workflow visibility from receiver lifetime (2026-10-04)

T-0103 continues at9371201. SatcomHubWidget::hideEvent currently stops both
the Inmarsat singleton and unarmed Satcom scanning. CwWindow and DtmfWindow
also cancel workers on any hide. Workspace navigation therefore acts as a
radio-stop command even with distinct physical sources. Presentation visibility
must suspend painting only; explicit Stop, decoder-window Close/Escape and
application shutdown retain their existing ownership-aware teardown.
MainWindow shutdown explicitly stops/joins Inmarsat before host/audio/device
services are destroyed; it must not rely on a hide event to do so.

Add independently addressable decoder windows using the existing per-window
workers and receiver selectors, not copies of singleton protocol engines.
Preserve the default SSTV automation endpoint while additional SSTV sessions
have separate source settings and cancellation. Validate hide/show, Close/Escape,
two simultaneous worker instances and isolated persisted source settings.
Limit open SSTV windows to 32 as an explicit UI resource budget, not a DSP
limit. Named sessions use 1-64 ASCII identifier characters, normalized to lower
case because Windows settings keys are case-insensitive, and retain the
existing default-session endpoint for older automation clients.
This is not permission to duplicate P25 follow state or change decoding/audio.
Release-material completion is also requested; it is tracked independently under
T-0104 with actual component evidence, never by deleting an unresolved gate.

## DEC-0184 - Fence hardware settings against workflow ownership (2026-10-04)

T-0103 inspection at d0f1633: raw gain, PPM, antenna, RTL and SDRplay setters
can mutate a radio claimed by another workflow. configureDeviceCapture calls
those same unguarded setters. Main-window non-SDRplay gain still targets zero.
Source checking only in the picker is insufficient: a command can race claim,
release, stop or an index-rebinding rescan after that check.

Add a short-lived per-physical-domain control operation to DeviceOwnership.
Validate the exact session token, or permit legacy operator settings only on
an unleased radio / legacy Listen session. Pin the endpoint until the command
completes; reject competing claims/tunes and drain controls before teardown.
Do not hold the global lease mutex across driver calls or waits. A scoped
capture configuration invokes private implementations under a single permit.
Startup catch-up is tied to its stream generation, not a fresh UI permission.
P25 automatic correction retains its existing algorithm through an explicit
legacy-P25 ownership adapter; no demodulation or speaker changes.

Expose rejected controls to GUI/CLI/web callers and use the selected Listen
radio for main gain. Keep explicit administrator stop as an invalidation
operation, not as permission for stale workers to configure the next session.
Test rejected model/hardware writes, stale tokens, released-while-command-active,
same-domain conflicts, independent radios and teardown ordering with mock I/O.
This does not complete repeated P25/satellite controllers or hung-driver recovery.
ISS-0060 publication materials remain a separate gate, not new release work.

## DEC-0183 - Isolate primary repeater control from secondary VFOs (2026-10-04)

T-0103 follow-up review of 53a518d found the analog loop in
MainWindowP25Orchestration.cpp copies global repeaterDualWatchWanted into every
active NFM receiver. That can retune a secondary radio and replace its audio
target with the primary repeater output; each receiver also overwrites the
global repeater status. Selected receiver UI makes this pre-existing coupling
operationally important. The worker filters inactive receivers before looping,
so active-list index zero is NOT a valid primary identity.

Snapshot the actual primary receiver under the existing receiver-list lock,
before active filtering. Bind dual-watch RF changes and controller status to
that identity only. Ordinary per-receiver DTMF/CTCSS/DCS observation stays on;
no filter, sample timing, P25 decode, vocoder or audio-buffer changes. Exercise
same-radio/different-radio VFO identity, inactive primary and missing bindings.
Accept only this exact orchestration delta in the frozen-pipeline guard, with
negative mutations. Cancel superseded CI37192889483 rather than accepting an
incomplete routing batch; rebuild/test and verify the follow-up's exact CI.

## DEC-0182 - Connect workflow controllers without cross-radio restoration (2026-10-04)

T-0103 continues from ba26fe3. Inspection confirms Inmarsat/Satcom still restore
by a recycled device index and use one global GUI takeover record; Aircraft
reads preferred Listen rather than its selected source. Saved satellite keys
also fall back to another index when absent. These defeat DEC-0181 isolation.

Use exact generation/client lease tokens for satellite and aircraft lifecycle,
including tune, stop and restore. Explicit force may attach an unmanaged live
Listen stream, never another lease/reservation. Reuse only live hardware; do
not restart a stub under the ownership mutex. Restore only while that exact
lease remains valid. GUI pause/restore and spectrum publication carry the
same token, permit independent radios, and reject stale completions. Missing
or ambiguous saved identities fail closed instead of selecting another SDR.

Wire UI/control source selection through these rules; preserve other logical
receivers during main-source/diversity changes and show unavailable selections.
Observer decoders must use a deliberately selected receiver, not implicitly
the first radio. No DSP/filter/vocoder/security/cadence changes are authorized.
Multi-instance P25 orchestration remains a separate migration, not something
proved by a source selector or four ownership tokens.

Gates: mock-radio lifecycle with stale restoration and independent controllers,
GUI source persistence/missing-device tests, host token isolation, existing
satellite/aircraft/SSTV tests, full build/CTest, actual no-RX GUI profiles and
exact frozen-P25 guard plus negative mutations. Source CI must pass; ISS-0060
still blocks binary publication independently of this work.

## DEC-0181 - Bound release closure and isolate workflow device assignments (2026-10-04)

User authorizes moving to T-0103, with explicit radio selection and several P25
radios alongside SSTV. Clarification: this must apply consistently to **every**
mode, demodulator and workflow, including repeated instances. Do not implement
protocol-specific ownership privileges. `WorkflowRadioSession` is the common
worker-owned lifecycle adapter; SSTV is its first UI integration, not its scope.
The migration matrix in WORKFLOW_DEVICES.md must distinguish implemented
ownership primitives from remaining singleton controllers and UI adapters.
10259ca Windows37168422913/YAML37168422917 pass; downloaded
135-file evidence and all17 Qt6.7.3 runtime replacements verified in acceptance
comment203327059. DEC-0179/0180 technical gates are complete. Do not rerun or
reimplement them as missing work. T-0104 retains only the finite materials/review
items in PACKAGE_HARDENING_20261003.md, independent of T-0103 implementation.

Inspection: DeviceManager has one global lease; MainWindow's traffic selector
unconditionally opens a second SDR; SSTV live is tied to receivers.front().
Implement a per-endpoint lease book with generation-bound tokens, persistent
stable-key reservations, conservative shared SDRplay hardware domains, explicit
conflict failures, and a Devices assignment window. Duplicate identities must
not silently bind a saved assignment. Default automatic assignments preserve
single-radio behavior. Reserved secondary workflows must not be retuned by the
main monitor or the P25 source selector. Add a dedicated SSTV source selection,
independent cursor/receiver, and scoped start/stop ownership, retaining the old
read-only main-receiver tap. No demodulator/vocoder/audio timing edits.

This pass is an ownership/assignment milestone, not an assertion of several
simultaneous P25 call controllers. Current MainWindow has one follow state and
one active traffic generation; a P25 device pool supplies that existing follow
path. Multiple independent systems/concurrent calls need separate controller
instances and stream-specific audio routing, with recorded RF acceptance.
Raw driver-hang recovery and remaining legacy token migration stay explicit.
Test independent devices, conflicts, stale token after release/re-enumeration,
shared domains, duplicate keys, persistence and dedicated SSTV teardown. Review
the exact shared-file patch and preserve the frozen DSP guard for other changes.
The worker adapter reuses Inmarsat's bounded10-second hardware-open allowance;
it rejects stub/failure and uses the existing settings/rate and driver. No new
DSP timeout or guessed delay. A stopping endpoint blocks new claims until its
driver teardown completes, without holding the ownership mutex across teardown.

## DEC-0180 - Separate native-window smoke from CI layout viewport tests (2026-10-04)

T-0104 continuation. CI37164667115 compiles all Qt6.7.3 runtimes, passes
native Windows rendering/network/TLS/CLI and the960x720 actual GUI profile,
then fails the1280x900 profile: captured window1028x749. Local native Windows
passes all four sizes. This is a confirmed test viewport mismatch, not evidence
of a radio or audio defect; available runner desktop dimensions were not yet
recorded. Do not remove the geometry assertion or alter MainWindow/DSP.

Record native desktop metrics. Keep a native Windows actual-app listening
profile plus the existing native pixel/network/TLS probe. On CI, additionally
run all four exact-size layout profiles with the source-built qoffscreen plugin,
clearly identified as a QA-only input, never deployed into the real package.
Local default remains all four native Windows profiles. Preserve all startup,
no-RX, screenshot size/aspect and no-warning/error assertions. Upload per-profile
reports/screenshots/logs for this CI-only QA so failures have the missing evidence.
The full source replacement still must replace every original Qt DLL/plugin.
Local offscreen rehearsal initially produced missing-glyph boxes and1208x720
instead of960x720. Qt6.11.1 QPlatformFontDatabase::fontDir() reads
QT_QPA_FONTDIR, otherwise expects fonts beside Qt libraries. Supply the installed
Windows Fonts directory for that child process only; do not copy fonts. All
four unchanged geometry/no-RX gates now pass on the local offscreen rehearsal.

## DEC-0179 - Rebuild and exercise the complete packaged Qt runtime (2026-10-04)

T-0104 / ISS-0060. DEC-0178 source acceptance is now verified:3e4956e,
Windows37160094253/YAML37160094287 PASS, inventory135 files; exact downloaded
inventory and QtSvg result accepted in commit comment203316479. That smoke
replaced only Svg; it did not rebuild Core/Gui/Widgets/Network or platform/TLS
plugins. Extend the same harness to build pinned qtbase then qtsvg in an empty
short-path workspace, with configured MSVC/Ninja and without the existing Qt
SDK in the module search path. Replace every Qt runtime file present in the
package or fail. Preserve original package/SDK, use no radio or external HTTP.

Exercise SVG/image/icon pixels, Widgets rendering, loopback HTTP and native
TLS backend availability, then actual CLI and four no-RX GUI startup profiles.
Record exact commands, source-kit/app/replacement hashes and failures; reject
stale success receipts. Compare relevant SDK feature settings and report any
differences rather than claiming bit-for-bit upstream build reproduction.
Build only base/svg runtime modules; qttools sources remain provided but a
qttools rebuild is not implied. A source replacement test does not itself
clear linked third-party distribution review or unrelated release blockers.

First local configure selected RadioConda PCRE2/zlib/PNG/JPEG/Brotli/Zstd;
abort that disposable build and explicitly select bundled alternatives. Check
both requested and resolved Qt cache features before compiling. CI's existing
6.7.3 package additionally contains qopensslbackend.dll; do not drop it. Require
an explicit OpenSSL header SDK only for packages containing that plugin, hash
its headers, and force runtime (not linked) OpenSSL. No OpenSSL DLL is copied.
This external build-header prerequisite is recorded, not misrepresented as a
fully self-contained Qt kit. Runner's documented path is Program Files/OpenSSL:
https://github.com/actions/runner-images/blob/main/images/windows/scripts/build/Install-OpenSSL.ps1 .

The upstream source kit and Qt's bundled CMake configuration are the build
authority. Test fixture time/resource bounds are operational QA limits, not
new receive/DSP timeouts. No installed dependencies or RF settings are changed.

## DEC-0178 - Version-bound Qt sources and Microsoft runtime evidence (2026-10-04)

Continue T-0104 / ISS-0060 without radio/audio changes. CI uses Qt6.7.3;
local builds use6.11.1. Bundle original qtbase/qtsvg/qttools source archives
verified against reviewed official SHA256 pins, preserve their license and
attribution files, and capture selected SDK configuration headers. Do not
substitute one version's source for another or equate a source archive with
a successfully rebuilt full Qt distribution. Provide replacement instructions
and exercise a source-built QtSvg against a disposable runtime, not the SDK.

The local VC/Redist directory label14.44.35112 contains DLLs and a signed
installer reporting14.44.35211.0. Record actual file versions separately;
preserve the matching installer's embedded license without executing it.
Its end-user license is not publisher redistribution permission. Keep that
distinction and unresolved distribution/toolchain requirements explicit.
Collection is bounded, explicit-path, hash-checked and rejects unknown
versions, hostile archive paths, missing inputs and inconsistent receipts.
Package verification must validate nested materials, not trust an approval
flag or merely the outer archive's regenerated hashes.

Primary sources: https://www.qt.io/development/open-source-lgpl-obligations,
https://download.qt.io/archive/qt/, and
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution.

## DEC-0177 - Preserve embedded ASN.1 notices and data provenance (2026-10-04)

T-0104 / ISS-0060. The actual libacars ASN.1 tree has242 C and246 header
files. UPSTREAM-README.md already supplies the full ASN.1 BSD terms, but the
package omits individual Lev Walkin/X/IO Labs copyright notices from those
files. Preserve leading comments verbatim (apart from newline normalization),
group identical notices and record every inspected source hash, including
generated files. Reject absent/unrecognized headers instead of inferring MIT.
constraints.c, per_decoder.c and per_encoder.c have no leading notice in the
pinned upstream either (live text comparison PASS); explicitly record them as
headerless with reviewed normalized source hashes. Changes require re-review,
not fabricated copyright text. The remaining485 files have recognized headers.
This is notice evidence, not all application corresponding source or a linker
map. Do not change the generated C, decoder, or DSP.

Ship the pinned ICAO transcription's CC0 text and provenance, hash the actual
country table, offline map/resource and packaged bandplan inputs. Verify the
bandplan copies against those inputs, not just a self-consistent outer ZIP.
Keep explicit historical-data/location limits. Use bounded, explicit source
paths; no broad developer-folder sweep or private-recording collection.
Recompute the notice text from its bounded evidence on archive verification;
test missing/changed/linked inputs, unfamiliar headers, extra files, limits,
determinism and staged/ZIP tampering. All existing release blockers remain
until their independent source/rebuild/distribution requirements are met.

Primary evidence: external/acars/UPSTREAM-README.md and actual per-file
headers; pinned dataset dedication:
https://github.com/ibosoftnet/icao-aircraft-addresses/blob/2ac0f294274beddb57212eb7531ff87dfd869de5/LICENSE .

## DEC-0176 - Suppress satellite auto-capture during GUI dry-run (2026-10-04)

T-0107 / ISS-0066 interrupts packaging acceptance. The unchanged Release EXE
opened RTL RX during test_workspace_gui.py despite parsed dryRun=true. Local
satcom_20261004.log records a 145800000 Hz pass arm and recording, while the
saved satellite autoCapture option is true. SatcomHubWidget's independent timer
does not consult GUI startup configuration. Do not clear the user's settings
or weaken the no-RX test. Publish the parsed dry-run flag as a process-local
Qt application property before constructing any widgets. The satellite hub
must neither start its automatic timer nor process automatic ticks in that
session; log the suppression once. Normal satellite operation is unchanged.
Strengthen actual packaged-GUI tests to require suppression evidence and reject
even transient RX starts. This is not a global hardware sandbox: enumeration,
audio prewarm and explicit interactive actions remain outside this repair.
No frozen P25/DSP files need to change.

## DEC-0175 - Ship hash-verified vcpkg source materials (2026-10-04)

Clean CI37148935496 builds newer fmt/spdlog recipes which download three
SHA512-recorded .patch resources in addition to archives. The archive-only
collector correctly fails rather than omitting them. Extend exact resource
collection to Git-format patches (never apply them), keep source/recipe hashes
mandatory, report archive and downloaded-patch counts separately. Test an
installed receipt with an additional patch, altered bytes and missing cache.
Run the same source-material preflight immediately after dependency configure
in CI so missing resources fail before the long application compilation. Deploy
still recreates and verifies the final bundle; preflight is not a substitute.

T-0104 / ISS-0060. Installed SPDX receipts describe exact port-file SHA256 and
upstream archive SHA512. All48 recipe files for ten runtime/header dependencies
match C:/vcpkg/ports on this host; the ten upstream archives exist in its cache.
Collect those exact bytes, patches and receipts in a bounded deterministic ZIP.
Verify the bundle independently against the packaged receipts, including the
runtime DLL checksums in those receipts. A similarly named archive or current
upstream branch is not equivalent. Do not extract or execute downloaded code.

Exporter uses explicit local inputs, no network or recursive source-tree sweep.
Reject links, unsafe/duplicate paths, missing/changed recipes, resources or DLLs
before atomically replacing a prior bundle. Package verification checks its
internal membership and hashes as well as the enclosing ZIP inventory. Capture
only named source materials, never local captures, tokens or arbitrary files.
CI must build dependencies without the binary cache so its source downloads
exist. Missing materials fail packaging; there is no optional bypass.

This collects sources/patches, not the complete compiler/tooling/Qt kit or proof
of an independent rebuild. Keep the remaining publication requirements explicit;
do not turn source receipt matching into legal clearance. No radio/audio changes.
Microsoft documents SPDX resource origins as heuristic; unsupported/missing
resource checksums must fail, never be invented:
https://learn.microsoft.com/en-us/vcpkg/reference/software-bill-of-materials .

## DEC-0174 - Build-input runtime staging, never a developer-folder sweep (2026-10-04)

ISS-0064 is reproduced by StageRuntime.cmake's root DLL/plugin globs and the
CPack fallback to bin/Release. Replace both with declared current build outputs,
configured vcpkg inputs, separately built pinned Soapy modules, and the configured
Qt installation's deployment tool. Use the same Qt/MSVC deployment helper in
CI and local release. Preserve existing deployment flags, discover additional
Qt runtime outputs from that tool, and retain the strict inventory checks.
Do not infer optional graphics DLLs are unnecessary from static imports.

Tests must seed unrelated/obsolete files, validate that they are excluded but
unchanged, reject missing/colliding/linked inputs before writes, and reject a
staging destination outside the exact build/deploy_staging directory before
cleanup. Direct CPack must never fall back to the raw build folder. Keep source
kit/notice publication blockers closed until their independent evidence exists.
No radio, P25, decoder or audio edits. Qt tool and target paths come from CMake,
not a hard-coded local Qt version. Primary deployment behavior:
https://doc.qt.io/qt-6/windows-deployment.html .

## DEC-0173 - Dispatch control commands outside native socket notifications (2026-10-03)

CI37117769454 failed with access violation in the cancellation/throw test on
unchanged application code. Baseline local repeat reproduces at iteration31;
CDB debug heap reports a freed block modified, with later detection in Qt's
Windows event dispatcher. A handler can stop the server and flush deferred
deletion while a direct readyRead/native notification is still on the stack.
A QPointer after the handler protects our response, not Qt's active emitter.

Add a deterministic ordering fixture before repair: a direct readyRead observer
installed after server acceptance must run before the application handler.
Queue guarded request dispatch on the existing server thread, including the
already-buffered acceptance case. Retired sockets must not dispatch; preserve
single-command framing, limits and response semantics. Do not add worker threads,
timeouts, retries or weaken cancellation tests. Re-run stress and CDB heap checks.
Qt warns against deleting QObjects while their events are active:
https://doc.qt.io/qt-6/qobject.html#dtor.QObject .

## DEC-0172 - Auditable package contents before publication (2026-10-03)

Runtime follow-up: dumpbin on the independently downloaded CI 37111782581
rtlsdr.dll proves a direct libusb-1.0.dll import, but that DLL is absent from
the ZIP. Stage the configured libusb DLL alongside the configured RTL DLL,
then use an isolated Windows loader process with DLL-directory/System32-only
search and a deliberately missing-libUSB negative fixture. This is a package
closure repair, not a driver/DSP change or live-hardware acceptance claim.

T-0104 / ISS-0060; baseline e4d767e. The independently downloaded 65e3da3
CI ZIP lacks LICENSE.txt, LICENSING.md, ACKNOWLEDGEMENTS.md and several actual
linked library notices. Stage the missing original and dependency notices from
the checked-out/build inputs, preserving vcpkg SPDX evidence and actual source
revisions. Do not infer a binary's source version from its filename alone.

Generate a deterministic file inventory after deployment/build-info, before
compression. Account for every staged file, reject unknown runtime files,
unsafe paths, symlinks/reparse points and case collisions; verify inventory
hashes and notices again inside the ZIP and after public download. Hashes here
detect packaging errors, not malicious publisher authenticity. Keep existing
signature/checksum/provenance verification. No token/config content is logged.

Source CI builds/verifies the ZIP in the runner but uploads only the hash-based
inventory report while blocked, not another incomplete binary package.
Both Actions publication and the local installer
helper must fail closed while that report has unresolved release requirements.
A boolean in a generated manifest cannot clear the gate: verification recomputes
policy from the shipped files. Do not publish a new version just to ship partial
license work or alter original MIT terms/third-party notices.

Qt's primary guidance requires corresponding library source (or an applicable
offer), notices and the ability to replace/relink the library; an upstream URL
is not this project's delivered source kit:
https://www.qt.io/development/open-source-lgpl-obligations . Version-specific
Qt, RTL/libusb, MinGW runtime and Microsoft redistributable coverage remain
explicit evidence gates, not legal conclusions inferred from dynamic linking.
Record materials still missing and require qualified review for unresolved
combined-distribution questions. Radio/DSP and diagnostics consent stay frozen.

DEC-0171 follow-up, 2026-10-03: an added empty-poll fixture fails at 90edc4a:
getNewIQWindowForReceiver acknowledges epoch 4 while returning no samples,
so HF's pre-read epoch comparison has lost the reset when samples arrive.
Publish the consumer acknowledgement only with a nonempty delivery. Keep the
producer epoch visible on empty windows, existing retune anchoring and P25
reset logic unchanged. Test both polling during the gap and reading after it.

## DEC-0171 - Evidence-first shared infrastructure hardening (2026-10-03)

T-0102, baseline d47f000. The follow-up review found startup-only health-monitor
activation, unbounded diagnostic status replies and control socket lifetime,
alongside ISS-0055/0056/0060. Repair independently of working P25/audio DSP.
Keep the health scheduler available after startup with no snapshots or uploads
while consent is absent. Measure elapsed time monotonically; a consent/session
transition resets heartbeat/rate-limit history, never reports an opt-out gap.
Existing 8-second stall and 60-second report limits remain telemetry policy,
not decoder timing. Test transitions with an injected monotonic time source.

Control resource policy: at most 16 accepted connections, 64 KiB total request,
10-second absolute request/response lifetime (configurable for local tests).
These are bounded loopback HTTP resource budgets, not RF tuning constants.
Reject ambiguous/invalid lengths and unsupported transfer encoding, authenticate
before parsing JSON, never echo handler exception details. Stop aborts accepted
clients; each socket can dispatch at most one command. Keep all existing route,
token and FUBAR response semantics for valid requests.

Status lookup uses the configured absolute transport deadline, a 128 KiB reply
budget and one in-flight lookup per client. Opt-out aborts and discards replies.
Do not download RF content or change consent through these endpoints. Regression
tests use loopback fake peers only and exercise success, overflow, slow/stalled
peers, cancellation and reconfiguration, not a real diagnostics service.

Shared hardware changes require separate fault-injection tests and retained
replay baselines. Do not mechanically remove the process-wide Soapy mutex or
replace a driver hang with an unbounded GUI-thread join. Record unresolved
hardware acceptance explicitly; successful unit tests are not live RF proof.

Tone TX safety: hardware is off by default. The explicit CLI parameter rf=on
authorizes only that command, never persisted, and is separate from GUI P25
arming. Query the opened driver's actual TX channel/rate/frequency/gain ranges;
reject invalid/unverifiable values before stream activation. Configuration
exceptions or readback mismatch fail the request, never silently fall back to
file output after a requested hardware operation. Readback tolerance is 1 Hz
for rate/frequency and 0.01 dB for gain: a strict configuration confirmation
policy, not a calibration assertion. File-only tone generation stays available.
Hardware CLI tone duration is bounded to 60 seconds and must be positive and
finite. No RF emitted during tests; fake drivers exercise all rejection paths.

Hardware overflow evidence: the fake Soapy readStream emits 1024 samples, -4,
then1024 distinguishable samples. Baseline fails5 assertions: epoch unchanged,
old IQ remains available during the gap and recent windows join both sides.
On SOAPY_SDR_OVERFLOW, publish a loss floor and new epoch under ringMutex
while retaining the shared live-I/O lock. Clear consuming-queue backlog. Keep
absolute received-sample counters monotonic; the missing RF sample count is
unknown and must not be invented. Recent windows cannot cross the loss floor;
chronological cursors use the existing epoch/floor contract. Do not change
normal timeout semantics or normal/retune/P25 filter/audio behavior. Publish
per-device read/loss/timing counters off the RX loop, not per-sample log text.


## DEC-0170 - Ideas are not code provenance findings (2026-10-03)

T-0101, baseline d0443b4. The maintainer clarifies that the P25 implementation
uses principal ideas from SDRTrunk, not copied code. Rechecked the sole helper
history entry (3e9573f), its original header and DEC-0015/0016. They establish
reference formulas/constants and a historical comment saying "copied". The
source also has similar control flow. This is reason to examine provenance,
not enough for this review to establish copying of protected expression or
declare a GPL-derived work. Conversely, the clarification is a maintainer
statement, not an independent certification of all source provenance.

Copyright distinguishes expression from ideas, methods and mathematics:
https://www.wipo.int/en/web/copyright/protection
https://www.copyright.gov/help/faq/faq-protect.html
Using the same principles alone does not import a reference implementation's
license. Actual source reuse/adaptation and linked/bundled libraries remain
separate cases, with their own terms. Translation is not automatically an
independent implementation; avoid either blanket legal conclusion.

Retract DEC-0169's asserted P25 exception and related release block. Record
ISS-0059 as a corrected attribution conclusion, retain the historical evidence
and maintainer clarification, and do not modify frozen P25 code or its history
to make a licensing claim. MIT remains the original-work grant; actual
dependency notices/packaging obligations in ISS-0060 remain unchanged.

## DEC-0169 - MIT for original work, explicit upstream scope (2026-10-03)

T-0100; baseline 2ab78d7. The user explicitly authorizes MIT licensing. The
existing LICENSE.txt contains reception guidance but no copyright permission
grant. Replace it with the unmodified standard MIT terms, retaining 2026 and
crediting SDR Town contributors. Put operational guidance outside the license;
do not attach receive-only or noncommercial restrictions to MIT.
Primary text: https://opensource.org/license/mit .

The grant covers original contributions, not third-party rights. Keep upstream
notices, submodule licenses and per-file copyrights untouched. Separate the
dependency/license inventory from reference-project and tool acknowledgements.
Reference, integration, compatibility and inspiration are not interchangeable.
No blanket license metadata or MIT-only binary claim is appropriate here.

Correction (DEC-0170): the original version of this decision called the P25
tuning helper a confirmed upstream-derived exception. That conclusion is
withdrawn. The header's "copied" wording and similar branching did not establish
copying of protected expression. The maintainer states that only the principles
were used. Record reference provenance without declaring GPL derivation or a
complete clean-room audit. Do not change frozen P25 behavior for this task.

The downloaded v0.2.122 ZIP also lacks several top-level/component notices
(ISS-0060). Documentation must not claim all notices already ship. Publish
this source/documentation correction without a new version or binary release;
track exact-artifact license/source compliance as a separate release gate.

## DEC-0168 - Receive-only DTMF burst and transformation profiles (2026-10-03)

T-0099 / ISS-0058, baseline ab21a4f. Keep the existing Goertzel detector rather
than import LGPL SpanDSP into the MIT core. Reference its published algorithm
and Q.24 timing/frequency rationale, not its implementation text:
https://github.com/freeswitch/spandsp/blob/master/src/dtmf.c
https://www.itu.int/rec/T-REC-Q.24-198811-I
https://users.ece.utexas.edu/~bevans/papers/1998/dtmf/dtmf.pdf
For rectangular analysis, sinusoidal energy is 2*|DFT|^2/N. Report pair purity
against mean-removed sample energy, with row/column dominance and signed twist
(column minus row). Reference limits: normal twist 8 dB, reverse 4 dB,
neighbor dominance 8 dB, nominal frequency tolerance +/-1.5 percent. Scan
nominal and both tolerance endpoints; do not claim Q.24 certification.

Conservative analysis retains a 20 ms window, uses 2.5 ms hops and 37.5 ms
candidate span; qualify 40 ms acceptance and <=23 ms rejection with phase
offset fixtures. Fast analysis is explicitly non-telephone-conformant: 12.5 ms
windows, 2.5 ms hops, 17.5 ms candidate span and 7.5 ms release, qualified
against 20 ms tones / 15 ms gaps. Conservative release is 20 ms; sequence
completion retains 300 ms. These are evidence-gated analysis policies, not
RF protocol requirements. A 0.78 pair purity floor excludes broadband energy;
test noise, single tones, competing pairs, harmonics and short transients.

Audio polarity/phase needs no mode: magnitude is invariant. Explicit spectral
inversion maps f to pivot-f. Known pitch scaling and additive shift are bounded
configuration, never guessed from arbitrary noise. No encryption recovery,
arbitrary scrambling, lost-band reconstruction or universal talk-off claim.
Preserve unclipped discriminator input; remove per-window DC without a speech
band filter that would bias twist or suppress the transformed frequencies.

Time detections by source epoch/sample index; keep monotonic UI timestamps
separate. Bound sequence/history/events and expose rejected-frame reasons,
event overflow, analysis time and profile. Use a mutex for queued options and
published snapshots/events, one producer for DSP state; EOF finishes explicitly
without fabricated silence. File decode is cancellable and remains <=120 s.
Continuous opt-in input-leg processing replaces the every-fourth-block skip;
qualify cost and protect the exact NFM-only edit with positive/negative guard
tests. UI is a separate analysis/settings window and exact menu hook. No P25,
speaker, radio ownership or diagnostics consent behavior changes.

## DEC-0167 - Evidence-scoped HF hardening and receive-only Morse (2026-10-03)

T-0098, baseline badcba4. HfDemod clamps with upper bounds below their lower
bounds when rates are too small; NaN IQ/gain can poison persistent state.
Reject unsupported rates below 8 kHz (the existing decoder audio floor) and
non-finite input, reset on rejection, and publish numeric reset/rejection
diagnostics. Keep the existing filters, AGC constants and 700 Hz CW BFO.
Input upper bound100 MHz and output384 kHz are allocation/resource policies,
not device support claims. Reject extreme rates before resampler allocation.
When the caller supplies an unchanged explicit dataIdentityHz, an NCO target
correction is not a new station: preserve oscillator, FIR, resampler and AGC.
Real source/rate/filter/mode changes and identity changes still reset. Callers
without explicit identity keep conservative tune-reset behavior. Tests must
demonstrate failure before repair and partition invariance after it.

Use GGMorse (MIT), https://github.com/ggerganov/ggmorse at
7b4822a8cfdbb1addfe497f3ae8186f142a4ee79, for pitch/speed detection and Morse
timing, rather than inventing a new core. Vendor only its core with license and
provenance; disable its unsolicited stdout text, retain decoded bytes locally.
Remove the unused STFFT sample counter and saturate Goertzel's startup-fill
counter at its window length: neither needs an unbounded signed sample count.
ITU-R M.1677-1 defines Morse spacing; independent fixtures use those ratios:
https://www.itu.int/rec/R-REC-M.1677-1-200910-I
Validate the library with independent generated signals, not its own encoder.
No RF or decoded text is automatically uploaded. CW reception is experimental
until live keyed RF and hand-sent/noisy cases are independently qualified.

The new window uses a worker-owned demodulator and chronological IQ cursor,
following the existing SSTV RF observer pattern; it never opens, retunes or
takes a lease. Snapshot the active analog receiver, reject P25 and simulated
sources, stop on source/config changes, reset on IQ gaps. Support recorded WAV
and all implemented analog modes. FM requires keyed audio (MCW); an unmodulated
carrier requires CW/SSB to yield a tone. Bound buffers/history, join workers,
keep UI responsive, and show source, estimated pitch/speed and gap diagnostics.
The sole shared MainWindow edit is an exact menu/window hook; update the P25
guard with positive and negative mutation tests, not blanket exclusions.

## DEC-0166 - Usability follows the operator workflow (2026-10-03)

T-0097. Running 0.2.119 / FUBAR 1.1.43 shows the inactive repeater monitor
occupying the central listening area, no direct workspace selector, fixed-size
FUBAR controls without dialog Tab traversal, an always-visible empty web player,
and commands whose feedback is hidden on another tab. Browser reproduction:
with a control lease, select WFM without submitting; the next status poll calls
sdrHighlightMode and replaces the draft with AUTO. Form drafts must survive
polls until submitted or control ownership changes.

Move the existing repeater widget into a dock and expose workspace presets with
a toolbar. This only reparents widgets; receiver connections stay intact. Keep
saved layouts compatible and preserve the P25 protection through an exact,
reviewed presentation-only transformation. Use restrained existing native
controls; add keyboard traversal and scrolling to FUBAR. Web listening and
lease state remain visible across tabs; commands share visible result feedback.
Use semantic tabs, labelled fields, bounded non-overlapping polls, stable map
markers and source-labelled empty states. No DSP or security policy changes.

Actual DLL/web walkthrough exposed a missing `ok` field in the Inmarsat map
snapshot: valid position arrays were rejected as an error. Add the explicit
success contract at the producer and a GUI fixture, not a permissive consumer.
Remove satellite widget-local neon styles so controls inherit the app theme.
No lock, decoder, scheduler, audio, routing or consent logic changes.

## DEC-0165 - FUBAR workspace parity without a second radio pipeline (2026-10-03)

T-0096. FUBAR source is being released as 1.1.42 against Town 0.2.118; Town commit 950ad45.
Confirmed gaps: no Inmarsat web map, 1090 button calls generic NFM tune, satellite
commands check lease but omit admin feature permission, fixed 64 KiB bridge
responses can truncate populated registries, old SSTV UI omits RF selection,
and unescaped aircraft popup fields can interpret received/provider text as HTML.

Keep P25/shared RF/audio unchanged. Add a versioned workspace API at the existing
authenticated loopback boundary. Reuse native hub widgets, engine validation,
tracking model, lookup and aircraft tune path; do not create parallel decoders
or independent internet-position caches in FUBAR. A web map poll counts as an
observer for six seconds (three missed two-second polls), not reception or call
evidence; it only permits existing locally consented lookup work. Browser users
cannot grant operator privacy consent or change home coordinates. Bound public
responses, omit local paths and receiver coordinates, label data source/age and
unidentified calls. Expired/offline responses clear active markers.

FUBAR keeps DLL/loopback transport and server-enforced queued leases. Enforce
admin permissions on every mutating route, use private unexposed per-browser
control credentials, reject cross-origin mutation, and parse bounded JSON with
nlohmann/json (MIT, pinned version already used by Town). No public generic
proxy or filesystem path command. Maps render with escaped DOM content and
stable markers; feature controls follow negotiated capabilities. Build/test
both applications, preserve private local files, publish matched CI packages.

## DEC-0164 - Classic Aero identity and source-owned hybrid map (2026-09-28)

T-0095. ICAO AMCP signal-unit Appendix C section 3 defines AES ID as the
24-bit ICAO address. ICAO Doc 9925 Part III 6.3.1 and Cobham AVIATOR 700
installation manual 98-124743-G table 5-65 confirm Classic Aero uses the same
aircraft address as Mode S. This supersedes the tag-17-only ICAO restriction
in DEC-0138/0162 and the 20260927 audit. Apply only at validated Classic Aero
boundaries; reject zero, all-ones and out-of-range addresses for online lookup.
ADS-C explicit identity conflicts remain rejected, never overwrite the AES.
Sources: https://www.icao.int/safety/acp/Inactive%20working%20groups%20library/AMCP%202/item-2AC.pdf
https://news.ncac.mn/uploads/bookSubject/2022-11/637331ac3d1fe.pdf
https://fcc.report/FCC-ID/2A6TS-AVIATOR700/7186906.pdf

Keep DSP, voice PCM, automatic speaker arbitration and P25 unchanged. Publish
validated 8400 identity SUs to the aircraft store, without fabricating positions
or treating assignments as current speech. The green marker means received
aircraft-associated voice activity, not proof that the pilot is speaking nor
sample-exact physical playback. All observed active channels may be marked;
the selected speaker is independently labelled. Unidentified speech stays
unattributed. No call identity is borrowed from nearest frequency or a stale call.

Use an independently owned live Qt map controller, immutable mutex-protected RF
snapshots, monotonic receipt ages and separate internet records. Offline/replay
remain self-contained; replay never queries online. Online assistance defaults
OFF, with persistent explicit consent: only received ICAO IDs and normal HTTPS
connection metadata go to ADSB.lol, never IQ, speech, raw messages or locations.
Provider: https://api.adsb.lol/docs and adsblol/api README (dynamic rate limits).
Match provider IDs against each request and current received identities. No
redirects, cookies or credentials. One async request, max 100 IDs/20 seconds,
10-second deadline, 512 KiB body, exponential capped backoff and Retry-After.
These are resource policy budgets, not radio protocol constants. Disable clears
online data and aborts/invalidate replies; hidden windows suspend requests.

User-requested RF/identity retention is 20 minutes, not a claim about L-band
ADS-C frequency. Online positions expire at 5 minutes (InmarScope policy),
prefer online only while <=60 seconds old and not older than RF; fall back to RF without modifying it.
Never refresh position age on identity-only traffic or repeated online fixes.
Optional estimates update in 10-second steps only for <=120 seconds using valid
ground track/speed from the same position observation; retain the measured
anchor and source label. No heading/Mach, waypoint or voice-derived position.
The estimate is a bounded spherical great-circle display approximation, not a
navigation or geolocation solution. No new third-party dependency.

Instrument lookup/identity/source/expiry/activity decisions, latency and bounded
counts locally; routine remote telemetry remains a numeric allowlist under the
existing opt-in. No raw aircraft identities/positions in automatic telemetry.
Qualify parser negatives, TTL boundaries, repeated fixes, late replies, source
removal, map render/green state, real reference IQ and byte-identical 8400 PCM.
Existing DEC-0137 busy-traffic GUI test exposed an integration regression:
the 256-entry recent identity registry can evict an aircraft whose independent
position record is still retained. Preserve both registries unchanged and expose
one locked union snapshot (at most 512 distinct aircraft), with the original
monotonic position receipt. Hidden map snapshots still update; only polling and
map logging are suspended while hidden. No fresh timestamps invented on merge.
CI 36398159214 exposed floating subtraction at an exact TTL boundary. Compare
against the receipt-plus-TTL deadline directly, not a rounded elapsed subtraction.
Keep expiry strict; deterministic fractional-clock test required, no epsilon.

## DEC-0163 - Explicitly raise recording allowance to 15 (2026-09-27)

User authorized 15 uploads after the collector's durable ledger confirmed the
tester already had four uploads in the rolling 24-hour window. Raise only the
per-installation recording allowance; keep separate consent/review, authenticated
HTTPS, 1 MiB request size, 128 MiB total recording storage, 30-day retention and
the independent request-rate guard. Do not delete history or change client IDs
to evade the quota. Expose the policy in health for deployment verification and
show a useful quota error instead of only a generic failed upload. No automatic
retry. Test the 15th/16th boundary, restart persistence and rolling expiry.
The resent cfef9adc/cd0e5a9c/570087e1 10500 bundles each have live +260 good/
zero bad CRC units. Cold replay completes ACKs and a ground-service message,
not a position report. Five seconds of cold acquisition cannot establish which
messages the already-locked live decoder completed. Add numeric message-direction,
ADS-C decoded and accepted-position counters to existing reports and reviewed
recording before/after snapshots; never send raw messages/coordinates as routine
telemetry. Keep old recordings compatible. Show an empty-map status when messages
exist without map positions. Test the actual reference IQ-to-GUI map path. Do not
weaken direction, CRC or identity gates or modify the working 8400 path.

## DEC-0162 - Direction-aware Aero applications and 8400 audit (2026-09-27)

User extended T-0092 before publication. libacars v2.2.1 README/API and the
InmarScope acars_apps adapter establish dispatch for ADS-C, FANS CPDLC, MIAM,
media advisory and OHMA. Expand the pinned vendored implementation, including
ASN.1 licence notices; Jansson (MIT) is needed for OHMA JSON. No XML pretty
printer dependency: readable XML does not require one. Preserve input bounds,
application CRC/error status and raw evidence; unknown airline-specific payloads
must remain explicitly unsupported, not described as decrypted/readable text.

Confirmed current defects: the native handler ignores ACARSItem.downlink when
parsing ADS-C, and InmarsatAdsc::parse returns identity only if a basic position
is in that same message. Use validated downlink ADS-C tags for independent
identity/position updates; uplink contracts/waypoints never become map positions.
Preserve AES/airframe mismatch rejection and no AES-to-ICAO guessing. Validate
application CRC before extracting any map fields. Cross-check existing JAERO
position fixtures and independent synthetic corrupt/wrong-direction cases.
Use ACARS block ID (libacars acars.c IS_DOWNLINK_BLK) for application direction:
digits are aircraft-originated messages, letters ground-originated. JAERO retains
the ten-character downlink message-number/flight header in message text; remove
it only with that framing evidence. RF channel direction is not a substitute.

Audit 8400 IQ/channelizer/OQPSK/C-frame/mini-m AMBE/PCM/output with existing
reference fixtures and tests. Do not change vocoder/framing thresholds without
a failing case. Counted PCM or codec silence is not clear-speech acceptance.
P25 remains frozen. Publication follows the expanded tests, not before them.

## DEC-0161 - Evidence-led Aero text and source-owned aircraft (2026-09-27)

Three real submitted five-second IF bundles (3c9e216d, c4d6b99e, 30409b9e)
each record +260 SU CRC successes and +0 CRC failures. This does not prove
ACARS reassembly or text interpretation. Extend the existing reference probe
to replay bounded bundles and report actual messages before selecting a repair;
never tune PLL/FEC thresholds from the description of garbled text alone.

AircraftMapWidget currently retunes without setting sample rate, and reads only
8192 recent samples every two seconds. The main window's 500 kHz control is an
analog channel filter; Mode-S reads raw IQ independently. Requested 20 MHz must
be a capability-qualified capture setting with requested/applied values and
explicit limitations, not an invented hardware bandwidth. P25 remains frozen.

AdsBTrackStore currently overwrites RF track fields with OpenSky data. Preserve
separate source records so disabling internet removes all network-derived fields
without deleting locally received aircraft. Network disable persists, prevents
new requests, invalidates/aborts pending requests and guards CLI/API merges too.
Test malformed records and off/on/stale-response races; do not disable map tiles
under a control labelled only as internet aircraft.

Replay 30409b9e recovers an MA/T single-transfer MIAM message and four empty
ACARS acknowledgements. The MA payload is base85 protocol data, not prose.
Reference: szpajder/libacars v2.2.1, commit
9af09a0121d4ec577339cbd4c7420d7519da48fa, doc/API_REFERENCE.md and
libacars/miam-core.c. Vendor its MIT MIAM CORE decoder and support files only,
using existing zlib for bounded decompression, not a new demodulator/vocoder.
Keep raw ACARS text unchanged for ADS-C and forensic evidence; add separate
application type/status/text. Unsupported segmented MIAM is explicitly pending,
not garbled text labelled as decoded. Application error flags never become RF
CRC failures. Serialize the library's lazy global configuration across workers.
Bound inputs/output and validate base85 before this upstream parser.

Aircraft controls use a dedicated 20 MHz capture request, capped at advertised
rates; SDRplay advertises at most 10 Msps and up to 8 MHz hardware IF, RTL-SDR
typically 2.4 Msps. Display actual capture/IF separately. Preserve analog filter
limits. Keep network source separation and rate selection under fixture tests.

Confirmed reference decode: 30409b9e's MA payload is MIAM CORE v1 ACK, not prose.
Fresh synthetic ACK and DATA fixtures verify uncompressed/raw-DEFLATE content and
CRC rejection independently; field bytes are not checked into tests. Preserve
unknown application formats explicitly. Source schema reference for squawk14:
https://openskynetwork.github.io/opensky-api/rest.html . libacars subset retains
upstream licence; zlib is an explicit vcpkg dependency and shipped runtime.

The 1090 worker uses existing chronological cursor API, 8192-sample work units,
and a one-second maximum backlog budget for interactive use. This is scheduling
policy, not a decoder threshold. Retain only the unexamined 120 us Mode-S packet
tail (extractor's existing 3-sample margin). Reset on sample/epoch/rate gaps and
log counts every 30 seconds. No change to detector thresholds or P25. Synthetic
20 Msps packet correctness is not a maximum-rate real-time throughput claim.

## DEC-0160 - Opted-in session evidence, not only issues (2026-09-27)

Collector inspection found two installations (one local, one synthetic), no
external RSPdx evidence, and an issue-only dashboard. Raw JSONL contains routine
events that are invisible there. Client FIFO drops oldest events during P25
bursts, including startup context. Add an authenticated, paginated event view
and bounded index; retain issue grouping separately. Historical indexing must
not duplicate client/issue counts. Keep 30 days / 20000 indexed events, cap raw
session logs and total storage; state these retention limits in documentation.

Add consent-gated system/build startup, GUI control-action batches and periodic
runtime snapshots through the existing diagnostics slot. Read-only observers;
no RF/audio/P25 algorithm changes. Record numeric controls/indexes, approved
static command captions and structural widget IDs, never arbitrary input text,
paths, credentials or recordings. Extend device inventory using existing cached
and read-only accessors, without probing hardware. One-second observer tick,
two-second action batches (32 actions), thirty-second snapshots/performance are
telemetry budgets, not decoder timing. Queue/coalescing counters must expose
loss. Bounded ingress and priority preserve startup/state/error evidence under
decoder floods; original 64 KiB/min budget and opt-out remain authoritative.

MainWindow changes are limited to read-only audio statistics in the existing
snapshot, opt-out checks in old health timers, and correction of the obsolete
mandatory-sharing notice. Freeze the exact whole-file digest pair in the P25
guard with negative tests; do not exempt the file or any decoder method.

Verify temporary localhost transport, flood/consent/privacy tests, dashboard
authorization/escaping/filter/pagination/retention/migration, full CTest and
frozen-P25 guard. Publish through Actions; confirm public asset and local
collector receipt. Physical RSPdx audio remains unverified without tester data.

## DEC-0159 - Evidence-first P25 regression diagnosis (2026-09-27)

User reports missed grants and previously clear talkgroups showing no voice;
requests diagnosis and a fix plan for the next pass. Preserve current production
code/configuration, inventory recent changes and correlate validation records
before attributing causality. Anomaly-selected JSONL is not an unbiased call or
audio success rate. No threshold, security, slot, timer or demod changes during
this diagnostic pass. Any live/offline checks must preserve encrypted-call mute.

Evidence closure: fresh 061308 GUI capture and same-IQ allocation comparison
isolate stale follow ownership. v113/v114 early replay counters and WAV hash
match. Isolated unchanged production-cpp tests prove wrong-slot END reset,
invalid-VCW activity refresh and sticky-release activity refresh. Next pass
must repair activity ownership/CC scheduling; do not loosen speaker isolation
or adjust timers to hide the failure. See P25_REGRESSION_AUDIT_20260927.md.

## DEC-0158 - Continuous SSTV image archive (2026-09-27)

User requests unattended successive-image saving. Existing live helper stops at
480 seconds/four images; parent caps 540 seconds and retains all images until EOF.
Add explicit stdin-only continuous helper mode, validate and atomically archive
each completion, remove verified temporary RGB, and keep only 64 recent metadata
entries plus current canvas. Keep file-mode budgets unchanged. Live stops on
user finish/cancel, device changes, gaps or I/O errors; no automatic retunes or
invented gap concealment. Remember a Pictures/SDR Town/SSTV root in QSettings;
GUI creates a unique timestamp/UUID session automatically. No overwrites, no
automatic deletion of saved images. Full disk stops with an error. Completed
images survive later cancellation/failure; provisional image is never called complete.

## DEC-0157 - SSTV real-time virtual device evidence (2026-09-27)

User WAV decodes Scottie1 via file and synthetic NFM auto/manual router tests.
Those bypass device buffering and helper streaming. Use the existing test-only
Soapy registry fixture, produce paced NFM at SDR sample rate from recorded PCM,
then invoke the production decodeSstvRfLive on its worker. Keep hardware unopened
and production DeviceManager/P25 unchanged. RF is synthetic: no antenna/USB proof.
Fixture timeouts and initial silence are test budgets, not decoder tolerances.

## DEC-0156 - Inmarsat rejection recovery and identity evidence (2026-09-27)

T-0084 supplied audit confirmed pipeline geometry/amplitude throws, mutation
before Aero passband check, literal codec availability, frame-increment focus,
and disabled-watch staleness dependence. Move all input checks before mutation,
return bounded reason counters on rejection, preserve decoder state, and let
the next noncontiguous accepted sample trigger the existing reset contract.
Known valid 24-bit AES plus existing 24000-sample window qualifies voiceActive;
raw speechActive remains separately observable and PCM with AES zero still flows.
Keep focus hold algorithm; change only its activity evidence. Clear last speech
on a new validated AES so the new aircraft cannot inherit old speech activity.
Extract existing C-frame handler to test its real failure path with an injected
codec call. Negative codec result currently indicates invalid API arguments,
not measured RF BER (AeroCodec.cpp). Reject whole output frame, count and reset;
do not weaken CRC/size predicates. No probe framing, assignment retune or P25
changes. Optional probe switch defaults on; toggling resets probe only.
Use 300 seconds as explicit UI freshness policy with watch disabled, not a radio
timeout. Normal remote telemetry remains numeric/boolean allowlist only; prior
separately consented recording feature is not automatically invoked by errors.

## DEC-0155 - Explicit bounded Inmarsat diagnostic recordings (2026-09-27)

T-0083: user requests short remote evidence recordings. Capture the exact 48 kHz
real IF at the native modem boundary plus its emitted 8 kHz PCM, not wideband IQ.
Also retain the first 16384 original complex float samples, with source clock
and center, per user's IQ+audio choice. Duration is explicit, not five seconds
of wideband IQ. Manual per-recording consent; five seconds maximum, one selected pipeline,
abort on discontinuity/reconfiguration. No rolling recording when disabled.
Review/save/send separately; no automatic send or background retry. Maximum
1 MiB JSON, four submissions per client/day, 128 MiB collector retained ceiling,
30-day expiry. These are explicit product resource limits, not DSP thresholds.
Require HTTPS and collector credential; no redirect or token in logs. Existing
telemetry remains counters-only. Recordings can contain voice/aircraft/location
data even without explicit identity fields. Shared client key does not prove
authenticity; per-install authentication remains a separate open issue.


## DEC-0154 - Inmarsat identity-safe audio and map status (2026-09-27)

T-0082: supplied review at 00cd3a5 confirms AES is discarded by single-channel,
watch speaker callbacks and replay. Pass identity into InmarsatAudio, clear
queued playback on identity transitions (including unknown), and expose bounded
source counters/sample offsets. Keep unknown audio playable, never invent AES.
Map reports active voice without identity or without ADS-C explicitly and rejects
out-of-range AES. Existing focus already retains speaking channel; keep its tests.
Do not change FEC, ADS-C mismatch rejection or enable unvalidated assignment
retunes. Automatic assignment candidates need separate real fixtures.

## DEC-0153 - WFM candidate RDS and level gates (2026-09-27)

Measured outcome: reject the prototype's double-filter RDS route, not the
sharper FIR. The same remodulated recording gives baseline 2/2 groups,
double-filter 1/1, direct retained-FIR discriminator 3/2 (2.4/10 MS/s).
Future integration must branch data before the additional speech channel FIR.
Mandatory tests require direct groups >= baseline and expected PI/PTY. Meter
stationary power agrees <0.1 dB; this is not transient squelch qualification.

T-0081: qualify candidate before live integration. Remodulate the existing
recorded mono MPX fixture as FM IQ, retaining continuous phase and linear MPX
interpolation. Feed retained FIR and real Demodulator multiplex to shipped RDS
decoder. Require CRC-valid expected PI, not just 57 kHz energy. This synthetic
RF conversion is not over-air verification. Separately compare full/retained
candidate-filter power after transients for stationary desired-plus-noise IQ;
report relative dB, never calibrated dBm. Do not change runtime until gates pass.

## DEC-0152 - Retained-output WFM FIR prototype (2026-09-27)

T-0080: implement causal FIR at retained positions 0,M,2M in test support only.
Reuse ordered coefficient accumulation and contiguous history; compare exactly
to WfmSpeechFir followed by downsampling across arbitrary partitions and resets.
Prototype the DEC-0151 2049-tap/10MS/s design with delay scaled by input rate,
80 dB Kaiser beta and unchanged BW/2 cutoff. Feed retained IQ through actual
Demodulator to measure clean/blocker PCM and wall-clock cost. Do not publish it
as production: full-rate power estimates and RDS sharing still need explicit
integration. No changes to P25/NFM/HF or current live DSP.

## DEC-0151 - Isolate WFM leakage with an independent oracle (2026-09-27)

T-0079: independently model the current 321-tap Kaiser coefficients, causal
convolution, retained input positions and phase discriminator using NumPy FFT
convolution. Compare blocker-only IQ before decimation, mixed vs clean
discriminator, and clean vs delayed ideal FM. Candidate 2049/4097 taps at the
same cutoff, beta for 80 dB, are experiments, not adopted settings. Qualify FFT
convolution against direct convolution first; do not silently use installed
SciPy (its version reports incompatibility with installed NumPy). No production
or P25 change. A longer filter must not be shipped without real-time cost and
full MPX/RDS/continuity qualification.

## DEC-0150 - Broaden WFM image characterization (2026-09-27)

T-0078: measure actual demodulated IQ, not a replacement filter model. Sweep
2.048/2.4/10 MS/s, 150/180/220 kHz channels, 50/75 kHz deviation and first
image offsets on both signs with +/-30 kHz folded offsets. +40 dB blocker,
peak IQ <=0.5, 200 ms and final 100 ms comparison against identical clean IQ.
Record signal error and cost; report finite cases without imposing an unproven
universal RF threshold. Preserve existing benchmark and all production DSP.

## DEC-0149 - Verify audit premises before DSP changes (2026-09-27)

AUTO already uses chooseSmartModeAndBandwidth in GUI/CLI; WFM is filtered before
decimation and selects bandwidth-dependent rates. Fresh benchmark has no failure
in its tested WFM cases. Shortening the CTCSS window fails existing neighbour
criteria even on a clean 67 Hz fixture. Record corrections and qualification
plan in AUDIT_0.2.110_FOLLOWUP.md; do not alter accepted DSP or P25 defaults from
these unsupported premises. Wider sweeps and phase-aware fixtures come first.

## DEC-0148 - NFM first-stage decimating FIR candidate (2026-09-27)

T-0074 actual IQ fixture proves +40dB image blocker corrupts speech. Compare
single/double moving-average responses against an independently verified FIR.
Engineering candidate target: <0.1dB ripple through12.5kHz (covers maximum
25kHz NFM channel) and >=80dB stopband from0.75*post-decimation rate to input
Nyquist.80dB is a design target giving40dB margin against the tested40dB blocker,
not a claimed RF sensitivity standard. Initial candidate16*M+1 Kaiser taps,
cutoff0.25/M cycles/input sample, beta from existing80dB Kaiser design formula.
Measure actual response; window parameter alone is not proof. Evaluate only
retained outputs at input indices M-1,2M-1,... to preserve existing sample count.
Persistent ring history, explicit reset; report causal delay8*M input samples.
Use existing windowed-sinc design pattern; reference structure and delay:
https://liquidsdr.org/api/firdecim_crcf/ and https://www.liquidsdr.org/doc/firdes/.
Run actual clean/blocker PCM comparison, tone gates and timing before choosing
default. NFM downstream filter/deviation/de-emphasis, WFM/HF/P25 remain unchanged.

Candidate measured stop grid below-93dB and passband ripple~0.0024dB at common
rates, versus double-boxcar image rejection only~59-60dB at image+6250Hz.
Actual +40dB image blocker difference improved+3.23 to-72.49dB at2.4MS/s;
wanted gain loss removed. Three matrices and20 additional first/second image
cases pass; NFM/CTCSS/DCS gates pass. Adopt FIR subject to full release gates;
keep low-rate path unchanged. Report increased cost and added causal delay.

## DEC-0147 - Optimize WFM FIR without changing its response (2026-09-27)

T-0074 measured >99% cost in WFM channelizer at10MS/s. Current FIR wraps a ring
index per tap/output. Prototype contiguous raw history and SIMD across independent
output samples, not across taps: preserve original accumulation order and every
full-rate filtered sample, hence power/squelch semantics. SSE2-capable builds use
explicit multiply then add (no FMA); other targets retain ordered scalar math.
Compare with old ring implementation using deterministic random/impulse input,
short chunks and tap changes. No NFM/P25/RDS/coefficients/decimation changes.
Accept only measured improvement with passing signal metrics and full tests.

## DEC-0146 - Measure FM blockers before changing accepted filters (2026-09-26)

User confirms0.2.108 WFM sounds good; NFM also accepted. No production changes
in this pass. Measure both demods at2.048/2.4/10MS/s with desired900Hz audio,
blocker1700Hz audio, equal/+20/+40dB RF amplitude ratios. Test adjacent25kHz
NFM/400kHz WFM and first decimation image plus1500Hz, derived from actual
rounding/rate plan. IQ peak bounded to0.5; no ADC/front-end overload modeled.
Use200ms recordings and last100ms audio (4800 samples at48kHz), giving integer
cycles for both measurement tones after startup. Compare against desired-only
at identical desired amplitude. Report tone levels and RMS difference, not BER
or SINAD. Time demod calls only, separately from fixture generation and report
IO; include stage counter deltas. CPU results are host-specific, not CI gates.
Default tests validate measurement math; opt-in benchmark emits structured data
with executable/source provenance. No invented rejection threshold or claim of
hardware sensitivity. Tests/docs-only work needs no new application asset.

## DEC-0145 - WFM speech clocks and causal history (2026-09-26)

DEC-0144 fixture reproduced WFM4800/4805 PCM and0.209476 waveform error.
Source confirms centered FIR reads past available block, decimator restarts at
zero, requested-count cubic repeats last sample, fade completion is blockwise.
Use existing causal FIR routine with independent WFM history and persistent
decimator phase. Reuse tested cubic clock with independent WFM instance and
two-input delay; preserve coefficients, de-emphasis, notch and squelch policy.
Reset on source rate/center changes and explicit reset; defer output-state
reset until first actual PCM, including notch history. Sample-wise fade uses
unchanged6ms coefficient. RDS data branch and NFM algorithms stay byte-identical.
Promote failing fixture and test tiny/regular blocks, rates and reset behavior.

## DEC-0144 - Persistent receiver Auto BW switch (2026-09-26)

User requests one switch covering all modes. Gate automatic channel-width
suggestions from mode changes, classifier, band plan and remote mode defaults.
Keep explicit user widths/saved presets and protocol-required P25 setup intact.
Default enabled preserves existing behavior. Persist switch, expose state in
diagnostics/control snapshots, log transitions only. No DSP algorithm changes.
After this isolated feature, characterize remaining WFM boundaries with tests
before considering a repair; preserve user-confirmed NFM/P25.

## DEC-0143 - NFM PCM clock follows samples, not callback size (2026-09-26)

User reports 0.2.105 NFM crystal clear, including improved auto bandwidth.
No classifier/bandwidth change was made or is authorized by this causal claim.
Preserve that baseline. First reproduce output count/waveform partition errors.
For NFM only, retain the existing cubic interpolation polynomial but wait for its
two real future samples, retaining bounded history across calls. Output counts
follow cumulative discriminator samples; caller counts are hints as for HF.
GUI/CLI use actual vector sizes when pushing audio. No tail repetition or phase
repair to fill a requested count. Document the two-input-sample lookahead latency.
Preserve filtering/de-emphasis/squelch policies; investigate any sample-dependent
postprocessing failure exposed by the stronger tests before altering it.
Keep WFM/HF/P25 processing unchanged. Shared-file acceptance remains exact reviewed
digest pairs, never a path-wide guard bypass. Log source/output count and delayed
history evidence; existing bounded telemetry and opt-in transport remain.

First PCM test run failed all eight rate pairs (e.g. 4800 versus 4667 samples).
After clock repair counts match, leaving <=0.000101 waveform differences. Source
inspection identifies the existing fade-complete threshold applied per block.
Apply that same NFM threshold per sample (same 6 ms time constant) and verify the
remaining error disappears; leave WFM's fade branch unchanged. Causal cubic uses
zero initial history and a fixed two-input-sample delay, not end-of-block padding.

## DEC-0142 - Isolated NFM continuity repair and bounded telemetry (2026-09-26)

User approved implementation of DEC-0141 and instrumentation. First reproduce
actual NFM discriminator tap failures. Preserve existing filter coefficients,
deviation, de-emphasis, squelch and final audio policy in this pass; replace only
NFM block-local FIR application with persistent causal history and preserve
second-stage downsample phase. Verify whole/split sample and waveform equivalence
and explicit reset/rate-change behavior. Causal FIR latency is (taps-1)/2 at its
input rate and must be reported, not hidden by invented samples.

Demod.cpp/Demod.h are protected shared files: permit only final reviewed exact
before/after digests, with negative mutation tests, as prior DEC-0128/0129 do.
Do not remove paths or authorize arbitrary future edits. WFM/HF/P25 algorithms,
Receiver, audio engine and follow orchestration remain unchanged.

Instrumentation uses fixed-size aggregate counters (no IQ/audio/frequency/IDs)
and snapshots outside DSP. Record input/discriminator/output counts, resets,
empty output, resampler unavailable lookahead/phase corrections, processing time
and over-budget blocks. Bounded local rotating diagnostics; remote transport
retains existing consent and byte limits. Logging must not write files/network
or acquire logger locks in the sample processing loop. This does not certify
unrepaired WFM boundary/resampler issues or physical RF acceptance.

## DEC-0141 - Characterize analogue stream boundaries before redesign (2026-09-26)

Review the supplied DSP write-up against source baseline 09cc9dd (application
25236d2). Do not adopt proposed CIC orders, HF tap counts, CTCSS thresholds or
P25 live streaming defaults without measurements. Demod.cpp contains block-local
downsample phases and centred convolution with missing future input. First gate
for a later repair is whole-stream versus irregular-block equivalence, followed
by adjacent-channel rejection and decoder fixtures. This pass is documentation
only: preserve every production/test source and the frozen P25 guard.
See DSP_AUDIT_20260926.md for evidence and limits. Publication of the audit does
not require a new executable or change the current release.

## DEC-0140 - Rotator bridge and honest SWR (2026-09-26)

Use existing Qt Network, no new linked dependency. Connect to a user-installed
Hamlib rotctld bridge; controller model/serial driver is owned by Hamlib.
Protocol reference: https://hamlib.sourceforge.net/html/rotctld.1.html
Use extended newline responses (+p/+P/+S), one request at a time, strict echo,
bounded 8 KiB replies and 1500 ms deadlines. Poll at 1000 ms; position expires
after 2500 ms. These are explicit UI/transport safety policies, not RF constants.
Never reconnect/rearm/move automatically. Movement requires explicit arm and
fresh position; validate configured endpoints/finite angles and soft limits.
Park uses the configured validated absolute target, not an uncontrolled device
park command. Stop is prioritized after the current transaction, never retried
as movement. Network stop is not a physical emergency interlock. Close disarms
and requests stop; hardware limit switches and physical stop remain essential.

SWR is optional read-only rigctld (+t then +l SWR) hardware feedback, not an
SDR receive-power estimate. No PTT/set-level/TX command is implemented. Require
an affirmative PTT observation and valid finite ratio >=1; unsupported, idle,
stale or failed readings are unavailable. Reference rigctld.1 get_ptt/get_level
and Hamlib tests/rigctl_parse.c Level Value/PTT labels. No automatic TLE motor
tracking in this first hardware-control milestone; don't imply it is implemented.
Manual pointing/readback, persisted soft limits and park are testable without
changing the existing satellite RF or P25 paths. Physical acceptance remains open.

## DEC-0139 - Bounded performance diagnostics and build parity (2026-09-26)

User clarification: consent must be a persistent menu choice with no credential
entry. Add a standalone Help-menu integration; explicit CLI off wins and opt-out
discards queued telemetry. Distribution tokens are public-client credentials,
not proof of authentic software (RFC 8252 section 8.5). Never grant them admin
authority. Validate incoming envelopes and bound request time, size and rate
at the collector (OWASP API4:2023). These controls limit junk and resource abuse;
they cannot prove that plausible values from an untrusted client are genuine.
Per-install enrollment/revocation remains a separate security gate until tested
end to end; do not describe a packaged token as tamper-proof authentication.

T-0067. Current release.ps1 injects opt-in collector config, but CMake/Windows
CI portable builds omit it. RemoteDiagnostics config loading also treats false
as no-op, so an earlier enabled file cannot be disabled by a later file. Fix
explicit consent precedence and give every build the same public HTTPS default
endpoint without embedding administrator credentials. Release authentication
must use a restricted collector credential, not administrator authority.

Instrument existing per-block clocks: input validation, reset/setup, legacy
physical probe, native channelizer, modem/codec/callback time, total RF duration,
last/maximum block time and over-budget block count. Timing is observational;
do not change gates or increase queues. Remote summaries retain the five-second
cadence and 64 KiB/min transport cap. Include at most 16 anonymous per-worker
numerical summaries, never frequencies, names, AES/ICAO, positions, IQ or PCM.
Tests must reject wrong types/nonfinite values and verify transport/consent.

Profile before optimizing. The legacy probe repeats full-rate mixing alongside
the native modem, so measure its cost separately; no speculative removal or
NCO arithmetic changes are authorized by timing alone. True multi-device
ownership, physical hardware/speech acceptance and collector-wide retention
remain separate substantive work, not silently declared solved by telemetry.

## DEC-0138 - Inmarsat monitoring without decoder policy changes (2026-09-26)

T-0066. User requests InmarScope-style channel and aircraft windows. Existing
store retains only 500 messages and 256 positions; non-position identities and
message totals are lost. Add an independent 256-aircraft LRU-by-receipt registry
(same bounded policy as positions), accepting validated ACARS/SU/assignment
messages only. Preserve nonempty identity fields, ignore older field updates,
and keep position receipt age separate from last-message age. Clear aircraft
also clears positions, not decoder/message counters or the rolling message log.
No aircraft identities are added to remote diagnostics.

Show actual per-channel protocol lock, bit rate, CRC counts and emitted message
counts, with a 500-line transition log (existing message-log size policy).
Inactive/stopped workers cannot display a live lock. Views are tabs with optional
independent pop-outs, refreshed at the existing 500 ms UI budget, never by DSP
callbacks. Fix combined data+voice phase wrongly being labelled data-only.

Do not copy InmarScope GPL implementation. ICAO is populated only from the
explicit ADS-C airframe identifier already parsed and checked against AES;
do not silently equate every AES to an ICAO address. Country lookup uses the
CC0 Annex 10 Amendment 92 transcription by ibosoftnet, pinned at
2ac0f294274beddb57212eb7531ff87dfd869de5. Preserve full allocation names;
unknown/unassigned codes remain blank. No online registration lookup is implied.
P25, tuning, voice gates and multi-device ownership are unchanged. Physical
satellite acceptance, 10 MS/s headroom (ISS-0032) and multi-SDR (ISS-0031) stay open.

## DEC-0137 - Selective fubarzi PR 33 test adoption (2026-09-26)

T-0065. User authorized selective integration, full regression tests and PR
closure. Retain our newer runtime and release state. Adopt the map-retention
GUI regression from a06413d and the synthetic active-IQ start/stop idea from
aa8c4d5, with attribution. Strengthen map coverage with rendered-image equality,
actual chronological-ring eviction and clear behavior. Strengthen lifecycle
coverage by waiting for processed IQ (bounded 10 s, matching the existing
fixture startup budget), not assuming a fixed 50 ms sleep proves work occurred.
Use silent synthetic IQ, no attached-device open/audio/network requirement.
Five restart cycles follow the contributor fixture; sample delivery pauses
10 ms per block to bound synthetic producer load (test policy, not RF timing).
Do not import obsolete release edits, weaker position storage, or unqualified
third-party DLL search paths; the current release bundles a matched SDRplay
plugin and honors explicit plugin configuration. Tests/docs only, no application
version or runtime change; push and await Windows CI before closing PR 33.

## DEC-0136 - Continuous in-band Aero and one RF viewport (2026-09-26)

T-0064. The planner currently separates data and voice even when all enabled
channels fit its existing 90% passband and decoder budget. Add a persisted,
default-on simultaneous policy: combine only when ALL enabled channels fit;
otherwise preserve the tested data/voice rotation. A combined group never
advances on dwell/idle timers, so modem state and position updates continue.
CRC evidence counts data workers only. Keep single-conversation speaker focus.
Reuse the existing channel-aware physical-center/DC-avoidance planner rather
than hard-code a 2 MHz LO displacement that could clip selected RF channels.
The native modem requires 48 kHz real IF at 8 kHz; do not replace this with an
unproven 12 kHz output. No claim of SIMD/PFB or 10 MS/s capacity without measures.

Spectrum navigation uses one normalized, bounded RF viewport for trace,
waterfall, markers, labels and clicks. Wheel zoom is cursor-anchored, drag pans
only within captured RF, double-click restores full span. Navigation never
retunes hardware or adds a channel on drag. Reset viewport on RF/rate change.
Shared multi-SDR ownership/audio buses remain T-0062, not bypassed here.

Unloaded build/watch-102-idle-benchmark.log measures 10 MS/s load ratios 1.32
(one worker) to 2.29 (sixteen); this is not realtime. First bounded optimization:
replace the 65-tap HalfRate modulo-indexed history with a mirrored ring. Each
sample is stored twice so the dot product traverses contiguous history in
EXACTLY the original tap/accumulation order. Coefficients, decimation phase,
NCO and modem rate remain identical. Compare against the original modulo
reference over many wraps before retaining it; repeat whole-chain benchmark.

## DEC-0135 - Multi-SDR sessions and evidence-based Aero collection (2026-09-26)

User requires independently assigned radios across modes, including P25 and two
Inmarsat roles. Audit: one global DeviceManager lease, one GUI takeover record,
one singleton Inmarsat IQ cursor/worker prevent that. Do not disguise this by
relaxing P25 ownership checks. Design and acceptance: MULTI_SDR_SESSIONS.md.
First independently deliverable step: expose up to 16 concurrent Aero workers
within the existing 32-channel saved-list bound. Existing saved limits remain;
default stays conservative until broad hardware load evidence exists. Capacity
is a user resource budget, not an RF/protocol guarantee. Measure 2/4/8/16 workers.
Early data-to-voice transition needs both distinct validated positions and fresh
CRC-valid data from a strict majority of the current group's channels. The
existing dwell deadline still permits an explicitly partial collection rather
than waiting forever for quiet channels. Never infer the active aircraft from
frequency/nearest position. Shared ownership migration is a separate acceptance
gate, not part of this isolated scheduler change.

Same audit found the live map rebuilt from recent(500), so unrelated message
traffic evicted valid positions. Maintain a separate bounded latest-position
cache keyed by validated AES, reject malformed/older updates, preserve receipt
age and clear with the store. The 256-entry bound matches the existing native
position cache. This is live-session state, not invented aircraft identification.

## DEC-0134 - Constellation identity follows explicit manual tuning (2026-09-26)

T-0061 / ISS-0030. Manual engine snapshots publish an empty decoder ID, while
watch selection pins a saved UUID. Confirmed refreshVisuals exact-ID matching
therefore hides manual symbols after selecting a watch entry. Successful manual
Tune/Start/preset tuning selects current decoder; rate preview alone does not.
Watch selection remains exact-ID: never substitute another channel when inactive.
Show actual decoder frequency/rate and explicit absence status. Validate widget
selection and native continuous-mode scatter callbacks separately; visible dots
do not prove protocol lock or real satellite reception. P25 remains untouched.

## DEC-0133 - Explicit rate-to-survey preset selection (2026-09-26)

T-0060. Rate dropdown previously had no frequency-selection handler. Only user
activation now picks a matching continuous mode/rate from the current satellite
survey (preserving an already matching center); programmatic UI restoration must
not retune or overwrite a saved/manual frequency. Tune/Start still commits RX
changes. Burst and EGC or a plan without that rate keep the frequency and show
that no surveyed preset exists. A rate does not define a universal RF band.
No guessed continuous allocation or obsolete 6F1 voice presets. Regression tests
cover every plan/rate, unsupported choices, GUI reopen and exact Tune settings.

## DEC-0132 - Matched SDRplay package and sourced Aero channels (2026-09-26)

T-0059 / ISS-0027. Build SoapySDRPlay3 48bd8b41072534018de1d74deb3dea5874d9e0e0
against this app's Soapy runtime and API 3.15 development files, matching the
InmarScope approach. Ship the MIT plugin and licence, never the vendor API DLL
or service. Support its explicit SDRPLAY_API_PATH override. Test actual plugin
registration separately from hardware acceptance.

The existing Aero JSON interpolates channel centers and assigns wrong rates.
Replace it with frequency facts from Wilson/Sergi.vdl2 February 2024 survey,
published in thebaldgeek L-Band.md at 828314e0512604879df6828ee5cbda2d02f9ea6d;
6F1 uses that source's April 2025 update, not its explicitly obsolete list.
APAC 4F2 presets are historical regional references, not a current RF survey.
Remove invented missing-data fallback. Keep user-saved watch channels intact.
Display 4 decimal MHz places and bit/s, not rounded 3-place MHz or baud.
Publish 0.2.98 as a regular GitHub release at user request, explicitly testing/
portable only and not Latest, preserving signed 0.2.96 updater discovery.
P25 DSP remains unchanged.

## DEC-0131 - README support appeal with verifiable claims (2026-09-25)

User requested a prominent donation appeal for SDR Town/FUBAR. Root commit
01daa063 dates to 2026-06-05; full local history is not shallow (112 days).
Publisher prices and RBA 2026-09-25 USD/AUD 0.7019 support a labelled budget,
not a claim to have inspected invoices. Ask which Grok/ChatGPT tiers apply.
LICENSE.txt contains a reception notice, not MIT: do not advertise an existing
MIT grant or waive third-party notices. State maintainer intent pending explicit
licence formalisation. Documentation-only change; no binary version/release.

## DEC-0130 - Mandatory CI-built public tester releases (2026-09-25)

User requires pushing working changes and verifying successful Actions and
public release assets on every completed application update. Persist this in
DEVELOPMENT_RULES section 11 and SoT. Existing Windows CI builds/tests/stages a
portable ZIP and publishes release/* branches. Use that actual path for
v0.2.97-experimental rather than calling a local package a CI build. Keep the
last signed installer update intact; this release is a portable prerelease.
Load version-specific release notes, reject mismatched branch/project versions,
and verify public downloaded assets inside the release job as well as locally.
Private signing keys stay local; no driver, DSP or P25 changes in this task.
Pre-publication audit found Windows CI explicitly disabled RDS DSP. Cancelled
the initial runs before publication. Enable the existing MinGW backend, check
the actual compiler target and matching tools, require decoder DLLs in staging,
and run recorded-MPX RDS tests on staged and publicly downloaded executables.
GitHub runner-images Windows2022 documentation lists GCC as preinstalled;
actual tool discovery/target verification remains the gate, not a path guess.
The first complete RDS-enabled run passed build/tests but correctly failed
the new package gate: SoapyRTLSDR.dll was absent. Build the already-used module
from audited upstream commit 6ca357c, linked to this build's Soapy/RTL libraries,
require its bias-T feature test, and include its license/provenance. The runner
also reported VCINSTALLDIR missing. Resolve MSVC with vswhere and stage its
x64 redistributable CRT DLLs so portable operation does not depend on the
runner's preinstalled runtime. Local module configure/build PASS.

## DEC-0129 - Explicit, capability-gated RTL bias-T (2026-09-25)

ISS-0024 / T-0056. Reference SoapyRTLSDR Settings.cpp at
https://github.com/pothosware/SoapyRTLSDR/blob/6ca357c15cbf676ff30eb8eb445d1e1eac17c136/Settings.cpp
advertises biastee only under HAS_RTLSDR_SET_BIAS_TEE, writes true/false and
returns cached state. It does not expose a physical-circuit probe or voltage
sensor and ignores rtlsdr_set_bias_tee's result. Vendor safety guidance:
https://www.rtl-sdr.com/V4/ and the Blog V3 datasheet prohibit powering a
DC-shorted antenna. Do not substitute offset_tune or a model-name guess.

Keep an RTL-only bias state/adapter, discover on full probe and actual open,
default off, persist only explicit user intent by existing stable device key.
Enable requires GUI DC-safety confirmation; CLI on is an explicit power request.
Apply saved intent to the actual handle, report rejected/mismatched writes,
and attempt off before clean close/fault cleanup. Stuck native handles remain
untouched to avoid concurrent USB calls; unplug is required to guarantee power
off after a crash/hang. Do not persist driver observations as user intent.
No sample, tune, demod, P25 or audio algorithm changes. Exact shared-file guard
review covers only the RTL control/probe/start/close integration and GUI wiring.
Use fake-driver on/off tests; local real-device test is read-only discovery.
DC intent requires the exact saved stable key, not the legacy serial-only
fallback. Failed ON confirmation triggers best-effort OFF even if the previous
readback is broken. Stopped devices can clear saved intent if support disappears.
The test fixture registers before module loading, verifies its make function
remains selected, and only then opens fake streams; Soapy 0.8.1 Registry.cpp
rejects duplicate driver registrations. No test ON can reach attached USB.

## DEC-0128 - SDRplay controls use live capabilities and acknowledged writes (2026-09-25)

ISS-0023 / T-0055. Evidence: DeviceManager light discovery uses generic RX;
startStreaming never calls enrichSdrplayDeviceInfo, and live setters swallow
errors after committing requested values. GUI has no successful-driver feedback.
RSPdx datasheet https://www.sdrplay.com/resources/RSPdxDatasheet.pdf specifies
A/B 1 kHz-2 GHz, C 1 kHz-200 MHz and Bias-T on B only. Reference driver:
https://github.com/pothosware/SoapySDRPlay3/blob/48bd8b41072534018de1d74deb3dea5874d9e0e0/Settings.cpp.

Probe the already-open SDRplay handle, publish verified capabilities without
inventing support, migrate generic/invalid saved ports to driver readback,
and refresh the visible panel when the async probe completes. Show antenna
selection in the SDRplay panel as well as the table. Control failures must be
reported and must not be silently persisted as successes. Bias power is never
enabled automatically; port transitions disable it before changing the port.
Keep raw driver identifiers separate from display names. Test calls/order,
failure recovery and UI enablement through a fake Soapy device, not only tables.
No new library, no P25 DSP/audio or shared sample/tune-loop changes. Necessary
SDRplay-only edits in shared files receive exact reviewed-diff guard evidence,
not a general exemption. Physical reception/voltage remains a tester gate.

Reviewed shared-file scope: DeviceInfo capability/result fields; probe RAII;
SDRplay-only live-open capability publication, sample-rate/activation checks,
startup profile catch-up, and serialized acknowledged SDRplay setters.
MainWindow changes are limited to Device Manager controls, SDRplay status/API
validation and selected-RSP gain routing. Existing non-SDRplay paths are retained.
No edits to rxThreadFunc, setCenterFreq, IQ rings, P25 methods, Receiver, Demod,
AudioEngine or vocoder. An exact LF-normalized before/after digest pair per
shared file binds the guard exception to this reviewed patch only.

## DEC-0127 - Aero visual cadence and bounded channel ownership (2026-09-25)

Evidence: InmarsatWidget::showEvent starts its sole display/status timer at
500 ms. WatchSession runs each full pipeline serially. Pre-change Release
benchmark on this host: 1/2/4 decoders take 0.613/1.230/2.463 s per 2.048 s IQ.
Four-channel loadRatio=1.203, so increasing the count alone cannot keep up.

Use a 50 ms UI visual timer (20 Hz display budget, not a radio constant), keep
expensive map/messages at 500 ms. Consume actual latest FFT, not interpolated
RF or repeated waterfall rows; keep a circular image. Global DeviceManager
FFT cadence (currently >80 ms hardware) and P25 remain unchanged.

Each active watch channel owns a persistent worker and complete pipeline.
One immutable IQ block is offered to all workers; drain all completions before
reusing it, publishing ordered messages or selecting one speaker. No unbounded
queue, detached task or cross-thread QObject destruction. Keep the configurable
1-4 active limit and grouped single-tuner scheduling, not a promise that every
saved frequency can be received simultaneously outside the captured bandwidth.
Live watch edits apply at block boundaries, flush old audio and reacquire the
new group; invalid empty enabled watches are rejected without stopping RX.

Use bundled JAERO ScatterPoints after modem timing/carrier recovery (oqpsk,
msk and burst variants), not raw wideband IQ and not fabricated quadrant points.
Keep at most 300 finite points per channel locally, expose one selected plot;
dot appearance never proves lock: AeroL's CRC-backed DCD remains authoritative.
No sample arrays enter diagnostic JSON/remote telemetry. Clear stale/no-input
plots. Enabling this passive feedback must preserve reference PCM bit-for-bit.

Format: ID, date, status, evidence, decision, consequences.

## DEC-0126 - Explicit Inmarsat handover from P25 (2026-09-25)

Evidence: ISS-0021; MainWindow SATCOM_HOST_INTEGRATION rejects any active
P25-marked Receiver; InmarsatWidget::onStart has no exit-P25 route. The existing
applySdrTownControlTune(force=true) already clears CC monitoring, follow, warm
standby and pending grants when a user leaves P25. Reuse that operation, not a
second protocol state machine or a relaxed audio/security gate.

Add a typed, GUI-host preflight, shared with local-control automation. A read-only
API prepare reports requiresP25Stop; Start must supply both force and stopP25
before stopping P25. Force alone retains the refusal. Probe the selected device (including a
configured/stopped CC, since starting RF could otherwise re-arm its monitor).
Ordinary receivers and P25 on an unrelated device need no P25 change. Ask before
stopping a matching P25 session; cancellation is read-only. On confirmation,
recheck ownership, acquire receiver/voice-queue locks with try_lock, quiesce P25
receivers, invalidate pending speaker output, then invoke the existing explicit
leave-P25 operation. Do not resume stopped P25 automatically on Inmarsat Stop or
hardware failure. The existing host guard remains the final check, including for
CLI/API callers that have not confirmed a GUI handover. No P25 decoder, RF math,
FEC, vocoder or speaker policy changes. Additive host wiring remains inside the
existing guarded Satcom integration markers; do not weaken the freeze verifier.

## DEC-0125 - SSTV feed control must quiesce hot publication (2026-09-25)

Release prerequisite ISS-0019: unchanged isolated 100-cycle attach/detach test
previously exceeded 246 s, this pass took 13.797 s even though publication only
uses try_lock. Feed detach waits on the same mutex the tight producer reacquires;
attached_ becomes false only AFTER acquiring that mutex. There is no admission
barrier allowing a waiting control operation to quiesce publication.

Decision: announce pending control operations with an atomic counter before
locking; producers check it before and after try_lock and report a contention
gap when skipped. Existing detach identity, stop and mutex ownership remain.
At most one already admitted publish can precede a pending control operation.
No sleeps or weakened assertions in the producer test. The counter supports
overlapping stats/control callers without a premature boolean clear. Scope is
SstvReceiverFeed only, not P25/analog DSP or the receiver input ring itself.
Repeat the unchanged stress case and full suite; do not infer field SSTV RF
qualification from a lifecycle test.

## DEC-0124 - Saved Classic Aero watch groups and position/voice cycle (2026-09-25)

User requests click-to-place saved 10500/8400/etc decoders and automatic position
collection, voice monitoring, then position refresh. Reference inspected:
SarahRoseLives/InmarScope commit 26ae80af4bcfa4c86ed55f4383d1c95481d3450b,
src/voice/voice_ops.cpp (per-channel decode, off-DC tuning, single monitored
voice, acquisition grace distinct from decoded-voice idle), src/decode/decoder.cpp
and src/voice/ambe_decoder.cpp (Aero AMBE4800x3600). No upstream source copied.
Our existing JAERO/libaeroambe chain stays unchanged. Unlike upstream assignment
follow, this is an explicit user watch-list cycle, not an invented assignment.

Decision: bounded 32-channel saved list, one to four independent decoders per
passband group, DEFAULT TWO. Local Release benchmark: four decoders at 2.048 MS/s
consume 2.381 s for 2.048 s RF (1.163x), so default four would backlog live IQ.
Expose the limit and processing/RF time ratio; do not hide overload with IQ drops.
Chronological IQ is shared within each group. Existing
6.5 kHz channelizer edge margin plus 10% RF edge reserve; prefer InmarScope's
min(200 kHz, Fs/4) center offset, constrained to every channel's passband. If
that overlaps a listed carrier, select the valid center furthest from carriers.
One worker owns all modem state and retunes;
lease/Listen takeover lasts the whole session. Confirm tune, discard old PCM and
advance IQ cursor before decoding a new group. Never mix voice sources. Reset
source state on gaps/retunes, not UI refresh. Keep single-channel/replay unchanged.

Scheduling is a user policy, not a protocol timeout: defaults data dwell 30 s per
group (minimum 10 s), target 10 distinct CRC-validated aircraft positions per
data visit, voice acquisition 12 s and idle hold 6 s (reference above), refresh
after 180 s, defer an active voice until idle but cap visit at 600 s. All are
editable, bounded and persisted. Visit counts exclude stale/duplicate positions;
a timeout explicitly says partial/no positions, never complete aircraft coverage.
Visit every group at least once; busy voice can defer other groups, visibly.
Persist atomically; malformed settings fail validation, not arbitrary RF tuning.
No auto hardware start on application launch. Automatic collection never implies
all aircraft transmit ADS-C or that everything in a saved list fits one SDR.

Tests: deterministic scheduler clock/events, passband/disabled/invalid lists,
duplicate/stale/no positions, idle vs active voice, forced refresh, speaker focus,
GUI selection/persistence and startup refusal. Native replay reference hashes must
remain unchanged. Remote diagnostics retain scalar-only opt-in allowlist; watch
frequencies/aircraft identifiers remain local. Live antenna qualification stays
open until an independent tester supplies matching IQ/logs.

## DEC-0123 - Inmarsat live selection and Listen ownership (2026-09-25)

User reports tone rather than speech. No matching live Inmarsat capture is on
this PC. Confirmed code defects: InmarsatWidget reloadBandPlans calls
selectBandPlan during construction, replacing a saved voice channel with data;
START ignores edited frequency/decoder (only Tune applies them). InmarsatEngine
acquires a tuner lease but never calls the existing SatcomHostServices receiver
takeover, so GUI Listen demodulation can continue on satellite IQ. MainWindow's
existing host callback parks matching Receiver::active flags and refuses P25.
This is evidence of routing/selection defects, not proof of the tester's tone.

Decision: preserve configuration on panel creation, share explicit Tune/Start
selection, use the existing host takeover with paired restore after hardware
restore. Fail before retuning if parking is refused. Display actual decoder,
PCM production and speaker error/state, and record bounded scalar diagnostics.
No raw modem audio, tone filtering, speculative DSP/codec changes or P25 edits.
Pinned libaeroambe frame packing (LSB-first 96-bit input, AMBE4800x3600) matches
our wrapper; public reference PCM parity must remain unchanged. Data channels
do not carry C-channel voice, and automatic voice following remains unavailable.
GUI tests must use isolated settings, no hardware and no remote reporting.

Lifecycle detail: MainWindow's existing host end callback queues off-thread
restoration. Calling it directly from a failed worker could leave a stale end
behind a new Start. Inmarsat queues the inactive-session cleanup onto the app
thread instead; Start joins/restores a failed predecessor before acquisition,
and queued cleanup skips a currently running session. No worker waits on GUI
while GUI may join it. MainWindow and P25 code remain byte-identical.

## DEC-0122 - SDRplay runtime selection and registration proof (2026-09-25)

User reports previously working SDRplay devices unavailable. Package inspection
of 0.2.74/80/89/92 shows no SDRplay API/Soapy module was ever bundled there;
the current code retains the device family. Actual local 0.2.92 CLI loads Pothos
SoapySDRPlay 0.3.0 but stderr reports sdrplay_api_Open failure; this PC has no
SDRplay service or RSP attached. That is local evidence, not the remote diagnosis.

Confirmed code gaps: API preloading only before main (cannot retry after install);
app-local nested modules and per-user conda/environment roots omitted; wrong
architecture paths prepended globally; an empty Soapy loadModule result is treated
as registration success even though Registry.cpp records ABI/duplicate errors
separately in getLoaderResult; streaming then tries every module again.
Reference: installed SoapySDR Modules.hpp/Registry.hpp and upstream
https://github.com/pothosware/SoapySDR/blob/master/lib/Registry.cpp . Official
API layout: https://www.sdrplay.com/docs/SDRplay_API_Specification_v3.15.pdf .

Decision: explicit, serialized SDRplay-only runtime loader before discovery/open;
UTF-8 paths converted to Windows wide APIs, matching architecture API dependency,
factory plus loader-result verification, failed discovery retried on Rescan,
one successful module per process (no unload of a live factory). Report API path,
module path/version and Windows service state; never equate registration with
connected hardware. Do not install/restart services or replace the user's SDK.
No new third-party dependency or vendor redistribution. Tests use isolated fake
DLLs to prove loader failures/recovery, not pretend to receive an RSP signal.

DeviceManager changes are restricted to its SDRplay setup and SDRplay module-open
block; shared receive/tune/audio and P25 remain byte-identical. Record this narrow
exception in the path guard with negative coverage, not a broad shared-file bypass.

Implementation note: upstream Modules.in.cpp disables automatic loading when
any module is loaded explicitly, including our bundled RTL module. Enumerate
SDRplay candidates explicitly on Windows and retain Soapy's platform search
paths on POSIX. Windows Soapy 0.8 uses LoadLibraryA: encode losslessly with an
available short-path fallback, otherwise report an actionable path error.
Never silently substitute characters or preload a module outside Soapy's registry.
Source: https://github.com/pothosware/SoapySDR/blob/master/lib/Modules.in.cpp .
The shared-file exception pins both entire DeviceManager text hashes; any
additional change, including inside the setup function, fails the guard.

## DEC-0121 - Native Aero receive, isolated codec and verified map positions (2026-09-24)

User requests voice and decoded positions on a map, following DEC-0120. Integrate
the MIT JAERO receive subset at 1d4e515921244aec1d85a03f5f38b4e7818fdbe6,
MIT JFFT, BSD libcorrect convolutional decoder, and ISC/MIT libaeroambe mini-m
codec at df7eebf17ca6396cc545bff4869dbeccc1e7dfcd, retaining licenses/provenance.
These implement the actual Aero modem/framing/FEC and AMBE4800x3600, not the P25
vocoder. Keep codec symbols in a separate DLL/shared library; never change P25.
Remove upstream GUI/database coupling, replace mutable function statics with
instance state, and drive decoder liveness by processed samples, not wall time.
Use fixed-size 48 kHz real-IF blocks for the upstream modem, generated from
filtered/resampled complex IQ; live and replay call the identical chain.
Upstream samples/8400bps_ambe_sample.ogg is an independent off-air regression
input, not a fabricated voice fixture. Record results before any acceptance claim.

Reference protocol chain: JAERO oqpskdemodulator.cpp, mskdemodulator.cpp,
aerol.cpp/.h, jconvolutionalcodec.cpp; libaeroambe aeroambe.cpp/.h. Preserve
8400 bit/s vs 4200 symbol/s, 4096 coded bits, 52-bit dual UW, puncturing,
interleave, scrambler and 25 x 96-bit vocoder words per 500 ms C frame.
Only CRC-valid SUs create assignments/call identity. Current-frame valid C
signalling is required for releasing a voice block; do not rely on RF energy.
Keep unsupported encrypted/protocol modes fail-closed. No voice identity from
text regexes. No retuning outside recorded passband during file replay.

ADS-C: implement bounded ARINC622 .ADS application CRC and binary basic-report
coordinates/altitude/time (21-bit signed, 90/2^19 degrees; 16-bit signed x4 ft;
15-bit time x0.125 s), checked against JAERO arincparse.cpp and libacars adsc.c/
arinc.c. Raw printable bytes, waypoints, invalid CRC, out-of-range coordinates
must not become aircraft locations. Retain provenance/age; replay positions are
separate from live state. Use a dedicated offline Natural Earth 1:110m map:
the existing Aircraft Map automatically polls OpenSky and its single callback
would mix replay/live state. Resource pin ca96624a56bd078437bca8184e78163e5039ad19,
public domain per naturalearthdata.com/about/terms-of-use/. Keep identity marker
heading neutral; no extrapolation or invented coordinates.
Map activity means validated received call frames, not proof the pilot is the
speaker: L-band and C-band link directions carry different information.

Include JAERO burst MSK/OQPSK and real FFT wrapper from the same pinned commit
for aircraft-originated R/T data. Explicit burst-mode selection, not guessed
from frequency. Test its public burst samples independently; continuous mode
alone cannot qualify the C-band ADS-C path. Keep codec synthesis PRNG per stream
to make interleaved decoders/replay deterministic; no P25 synthesis changes.

Acceptance: build/failure tests; native replay of upstream reference audio/IQ;
bit/framing/PCM counters and saved WAV for listening; malformed/noise rejection;
chunk-size/paced-fast equality; ADS-C independent example and CRC mutation;
GUI map display. Dad's setup remains required for his RF qualification.

## DEC-0120 - Inmarsat IQ replay and honest protocol diagnostics (2026-09-24)

Release decision: publish the completed replay/diagnostics feature as 0.2.91
experimental, with framing/voice/maps explicitly incomplete. Package a disabled
HTTPS collector config; tester opts in through replay checkbox (disclosure) or
--diag-url. Preserve existing opt-in overrides and --no-remote-diagnostics.
References inspected: JAERO 1d4e515921244aec1d85a03f5f38b4e7818fdbe6;
libaeroambe df7eebf17ca6396cc545bff4869dbeccc1e7dfcd (no code vendored).

Deployment follow-up: user authorized inspecting VM 10.1.1.111. Read-only Apache
inspection confirms gearsqueens.online (not the mistyped domain), with existing
FUBAR/UOW/ereader/PSK routes. Public DNS and external IP agree. Collector health
works from the VM. Add only a vhost Include and a separate exact-path proxy for
/sdr-town-diag/ingest, /client-status and /health; do not expose collector admin.
Back up the original, validate apache2ctl configtest, graceful reload, compare
unrelated routes before/after, verify authenticated synthetic receipt. No secrets
in source, reports, stdout or release notes. TLS remains mandatory for token use.

User confirmed commercial/Inmarsat voice, not the analog satcom scanner. Dad can
provide reference IQ. Implement bounded SigMF (cf32_le/ci16_le/cu8), stereo IQ WAV
(PCM16/float32), and explicitly configured raw replay, with pause/seek/time and
CLI automation. SigMF v1.2.6 (https://sigmf.org/) defines I then Q, sample_start
and capture frequency; reject unsupported layouts rather than guessing. Read at
most 65536 samples per block (existing live consumer bound), reset on capture
boundaries/seek/gaps, and use the same Inmarsat processing class as live input.
One worker owns file/DSP state; GUI observes snapshots. No hardware retune on replay.

Evidence: current InmarsatDemod is a physical probe, deliberately suppressing raw
bytes. JAERO aerol.cpp requires sync, deinterleave, convolutional FEC and CRC before
assignments. jontio/libaeroambe aeroambe.cpp uses a 96-bit LSB-first interleave into
6x24 and mbe_processAmbe4800x3600Frame; our unused 4x24/3600x2400 wrapper is not Aero.
Disable its false availability/decode path; do not reuse P25 codec logic or expose
synthetic voice. Frame/FEC/C-channel/Aero codec integration and real-IQ acceptance
remain explicit blockers (ISS-0016), not a finished decoder.

Diagnostics: session UUID, sample-clock position, format/rate/offset, gap/reset,
physical counters, processing time and clear capability flags. Local JSONL capped
at 8 MiB/session; summary is always retained. Remote allowlist contains numerical
counters and session IDs only, never IQ/audio/filenames/aircraft IDs/locations.
Remote progress at most once per five wall seconds through existing consent,
HTTPS/authentication and global budgets. Do not silently enable reporting.
Existing loopback HTTP/token configuration is not a public deployment (ISS-0017).
Do not invent a server address or publish credentials to Git. P25 stays unchanged.

## DEC-0119 - Publish SSTV source and matching tester assets (2026-09-24)

The user explicitly requires the DEC-0117 SSTV work to reach GitHub download
users, not remain a local build behind the separate CI repair. Release 0.2.90
experimental with all SSTV source, tests, documentation, installer, portable ZIP,
matching control DLL and signed updater metadata. Run the existing release gates,
repeat recorded RF/image regressions, and smoke-test the extracted package.
Confirm remote CI and uploaded asset hashes. Preserve P25 and existing updater
trust. On-air RF qualification and ISS-0013 noise-tail partials remain open;
release notes must give testers exact steps and limitations, not claim certification.

## DEC-0118 - Release verifier fixtures must satisfy the current contract (2026-09-24)

Windows CI run 35979813492 built the app and passed native/SSTV tests, then failed
four packaging tests with `Missing runtime: build-info.json`. The production
verifier acquired source/executable provenance and SGP4 licence requirements in
560cf85, but its disposable test package did not. Reproduced locally: 11 tests,
2 failures and 2 errors. Update the independent valid fixture; do not weaken the
production verifier. Add negative cases for missing provenance/licenses, bad
version/commit/executable hash, plus BOM-bearing JSON. Run this cheap gate before
the expensive CI build and as a local release preflight so the paths agree.
Keep CI artifact provenance's source/hash fields consistent with release.ps1;
retain its existing commit field for consumers. This repair is isolated from
unpublished SSTV application changes and does not modify runtime DSP or assets.

## DEC-0117 - SSTV RF routing is separate from picture-format detection (2026-09-24)

Inspection: SstvWindow Auto only selects the image helper's format. MainWindow
attaches the already-selected NFM/USB/LSB audio tap; it does not identify RF mode.
QSSTV configuration documents LSB/USB/FM data modes
(https://www.qsl.net/o/on4qz/qsstv/manual/config.html). Reuse the independently
tested classic 910 ms VIS detector (DEC-0091), not a frequency-band guess.
An isolated SSTV worker reads chronological IQ without tuning the device or
changing the main receiver. Separate USB/LSB/NFM demodulator states search for
a known, parity-valid VIS. Retain two complete headers plus one bounded input
block of audio; after the first detection allow one header duration for competing
routes to validate. Multiple valid routes are ambiguous: require manual selection,
never silently choose a sideband. Pin the selected route for the session and
replay retained audio into the existing image helper, including its VIS header.
Manual USB/LSB/NFM/AM bypass RF acquisition, not image-format detection. Use existing
demods with SSTV-specific unsquelched/unfiltered decoder taps. No P25 or speaker
processing changes. File decoding already receives audio and has no RF mode to
identify. Satellite pass routing stays explicit from the downlink catalogue with
its continuous Doppler correction; do not start a second, uncorrected RF reader.
Classic VIS auto has no claim to extended-VIS/headerless RF acquisition, arbitrary
carrier-offset correction, or universal modulation identification. Those use a
manual RF route. Prove routes/ambiguity/parity/continuity with synthetic IQ and
run the existing image-worker tests before calling this implemented.

Measured follow-up: synthetic USB initially failed VIS because the existing
HfDemod halves its BW argument. Request 6 kHz for a 3 kHz SSB passband on SSTV
routes (including the satellite route), leaving general HF semantics unchanged.
Retained storage includes one additional bounded block for the tail of the IQ
block in which selection completes. Recorded RF round-trip image test budget:
full row count/completion and <5% full-scale average RGB error. It measures image
fidelity, not RF classifier confidence. The standalone test uses NumPy/SciPy/
soundfile in an isolated development venv; no product dependency was added.

## DEC-0116 - Test the actual talkgroup table without radio globals (2026-09-24)

The sorted-refresh regression initially failed to link: the workspace target
does not own P25TalkgroupRegistry. Adding the whole registry pulled in control
decoder functions and global speaker state. Move only table presentation and
its existing pure label helpers to P25TalkgroupPresentation.cpp, linked by both
the app and workspace tests. Preserve helper bodies exactly; no grant/audio
semantics change. Test sorting, metadata edits, identity selection and deletion
against the production table, not a replacement renderer.

## DEC-0115 - IQ publication must not invert device locks (2026-09-24)

Release review found that serializing read+publish with direct-sampling changes
added a devicesMutex lookup inside the live-I/O lock. Enumeration takes
devicesMutex then stateMutex; mode changes take stateMutex then live-I/O. This
formed a three-thread lock cycle. RX already owns a stable StreamState reference;
pass it to the private append helper instead of looking it up again. The helper
may take ring/queue locks only, never device/lifecycle/state/driver locks. Keep
read+publish atomic relative to a mode switch, without touching P25 decoding.

## DEC-0114 - Satellite Doppler preserves the sample stream (2026-09-24)

Evidence: audit A09; tickPassTrack physically retunes on 50 Hz changes and
processLockedAudio uses that moving frequency as decoder identity. A retune
creates an IQ gap, while HF target changes reset its filters/resamplers. The
satellite consumer also discards already-consumed input below 1024 samples.
Keep RF centered on the nominal downlink after arm; digitally translate each
chronological block by exp(-j*integral(2*pi*(tracked-nominal)/sampleRate)). Carry
oscillator phase across frequency updates and blocks. Demod target/identity
stay nominal. Reject targets whose channel is outside the captured Nyquist
interval rather than silently aliasing or retuning. Only an explicit IQ gap or
receiver reset resets the oscillator. Consume all nonempty IQ and keep data
decode independent of whether that block produced speaker samples. Prove signs,
phase continuity, short/irregular blocks and stable decoder epochs synthetically;
full live pass qualification remains open. This is satellite-only, not P25 DSP.

Release the verified repair batch as 0.2.89 experimental after build/package
gates. Keep hardware/protocol qualifications visible; do not claim they passed.

## DEC-0113 - Decoder transport and AM startup contracts (2026-09-24)

A11-A14: satellite data uses pre-speech discriminator blocks; SSTV live modes
must match the helper's analog mode table. Digital STWN stays file-only and is
not advertised as EasyPal-compatible. Enforce its existing size/time budgets
before allocations/synthesis, require FAC CRC, and emit the same row protocol
as analog images. These are transport/safety fixes, not interoperability proof.
AM startup currently averages an arbitrary first callback. Replace that with
sample-clock priming: discard one FIR length, average the next FIR length,
then use the existing carrier tracker. Muting startup remains explicit. Test
identical whole/irregularly partitioned AM, SSB and CW input at both output rates.
The existing recorded Worker/GUI parity gate also exposed a file-only prefilter:
the identical Robot36 fixture produced two file images versus one live image.
Use the pinned helper's unmodified PCM input contract for both paths. Remove
only the extra file-side two-pole filtering; keep finite checks, clipping and
PCM conversion. Acceptance requires independent pixels plus file/live equality.

## DEC-0111 - Receive-chain repair contracts (2026-09-24)

Evidence: AUDIT_20260924.md reproductions A01-A17. Preserve P25 audio algorithms.
HF optimization retains the existing windowed-sinc response: cache 1024
fractional phases and interpolate coefficients, retain continuous sample-clock
state, and replace per-sample oscillator transcendental work with a normalized
complex recurrence. Verify folding-band rejection against the audit baseline.
The impulse estimator must observe rejected input too, rather than freeze at
the old carrier level. Its existing 50 ms averaging constant remains unchanged.
Use a persistent output-rate converter; targetAudioSamples is not permission to
stretch each HF block. Acceptance includes irregular partition equivalence,
weak-to-strong recovery, alias rejection and measured real-time throughput.
Once the blanker no longer hides sustained overload, the existing overload
recovery test exposes two cascaded AGC release integrators. Keep the envelope's
120 ms release and follow its desired gain on recovery; retain fast gain attack.
Input loops drain pending work before idle waits. Hardware errors must not be
reported as successful tunes. Metadata enrichment must stay within one system.
Every satellite/SSTV change needs independent vectors or transport tests before
being marked resolved; protocol features without independent fixtures stay open.

## DEC-0112 - Complete SGP4 behind the existing satellite API (2026-09-24)

A04 measured errors exceed 1,600 km at epoch. Adopt aholinch/sgp4 C++ core,
commit 552cb1489a52c3023ae70cb6c7e239e84c5950fe, released under Unlicense.
Vendor SGP4.c/SGP4.h and its license; compile the core as C++. Rename its custom
fmod symbol to sgp4_mod (all calls) for MSVC compatibility, without changing math.
Do not use its platform-dependent long-millisecond TLE wrapper. Our wrapper
validates orbital elements, initializes WGS72 improved mode, and uses a fresh
record per call so concurrent/non-monotonic queries share no mutable state.
Keep TEME/ECEF/observer APIs. Gate against independent CelesTrak/Vallado vectors,
including deep-space; finite output alone is not acceptance.

## DEC-0110 - Canonical source and evidence-first repair queue (2026-09-24)

Status: recorded audit/reconciliation decision; implementation deferred.
Evidence: TEST build-info pins a2ac437/0.2.88; original source was 395c59b/0.2.80.
Exact-tree comparison proves 15 recent work branches duplicate master history.
User requested branch cleanup and return to the original development folder.
Local pre-reconcile branch and proxy-change stash retained; master fast-forwarded.
Remote duplicates removed only with local archives and expected-SHA checks.
Unique branches, backups, release refs and PR #32 retained. No blind merges.
Measured PR #32 alias regression blocks accepting its DSP patch unchanged.
Follow AUDIT_20260924.md repair gates; do not alter accepted P25 audio DSP or
publish a release as part of the audit. Future fixes require their own tests.

## DEC-0109 - Analogue spec audit + HamDRM digital SSTV (2026-09-20)

Evidence: Dayton N7CXI paper (Scottie 138.240/88.064/345.6 ms, PD equal Y/RY/BY
scans, SC2-180 320×256); HB9TLK HamDRM page (Mode B, 48 kHz, FFT 1024, Tg/Tu=1/4,
15 symbols, 51 carriers @ 2.5 kHz, 40-bit FAC). The user spec's Scottie 2 69 ms,
PD half-chroma, SC2-180 256×256, and PD290 VIS 103 contradict Dayton/QSSTV.

Decision: keep analogue timings on Dayton/handbook/QSSTV. Interpolated full-period
discriminator stays (not a broken 10 ms PLL). VIS Hamming distance ≤1 on parity
fail. HamDRM is a second engine (Mode B 2.5 kHz, 4-QAM MSC, K=7 conv 133/171,
STWN RGB payload). Not EasyPal file-level RS (undocumented / incompatible with
EasyDRF). Auto file decode tries analogue then HamDRM. Wide 300–2800 Hz prefilter
for auto/hamdrm; 1–2.4 kHz for forced analogue.

## DEC-0108 - Leftover SSTV families from handbook/QSSTV (2026-09-20)

Evidence: SSTV Handbook ch.4 Martin M3/M4, Scottie S3/S4, Wraase SC-1 24/48/48Q/96
scan tables; QSSTV `sstvparam.cpp` FAX480 (VIS 0, 512x500, 133.633 s) and MP/MR/ML
16-bit VIS (`0xNN23`) plus `modePD`/`modeRobot2` scan split. Auto line-sync first-match
would confuse FAX480 (267 ms) with Scottie 2 (278 ms).

Decision: add those layouts to the vendored crate. Auto: 7-bit VIS, then 16-bit VIS,
then closest 1200 Hz line-sync period (not first 15% match). FAX480 has no VIS.
MR175 omitted (QSSTV VIS 0x4A23 collides with MR140). Narrow 2172 Hz modes omitted
(demod is 1500–2300 Hz). FUBAR uses the same mode list and `/v1/sstv/live|finish|cancel`.

## DEC-0107 - Extra SSTV modes from QSSTV/handbook (2026-09-20)

Evidence: ON4QZ QSSTV `sstvparam.cpp` VIS/geometry; SSTV Handbook ch.4 SC-2 and AVT
line times. Dayton crate had no Robot B&W, SC2-30/60/120, or AVT.

Decision: vendor the MIT crate and add those layouts. Auto: VIS then line-sync
period. AVT has no 1200 Hz line sync (handbook); VIS or forced start-of-image.
Robot 24 is QSSTV 160x120 luminance, not a guessed YC table. Do not add FAX480
or narrow MP/MR modes in this drop.

## DEC-0106 - Unlock pinned SSTV Dayton modes (2026-09-20)

Evidence: unexcellent/sstv 16bf34aa implements 18 modes with VIS auto-detect
and per-line 1200 Hz sync re-align (slant lock). The helper previously rejected
everything except Robot36/Martin1 and killed jobs at 120 s (too short for PD).

Decision: expose those 18 modes in helper/CLI/GUI. Auto = VIS. Forced mode uses
the crate's first-line sync lock. 1–2.5 kHz pre-filter on recorded PCM.
Timeout/budget 480 s. AVT / Robot B&W / SC2-30/60/120 stay unsupported (not in
the crate). Do not invent a second decoder.

## DEC-0105 - Inmarsat voice follow stays disabled (2026-09-20)

Evidence: slicer has no unique-word; C-assign is fixture regex; AMBE pack is not
Aero 8400. Retuning on `voiceFollow` would hop L-band without a proven grant.

Decision: `voiceFollow`/`recordVoice` default false, ignored on load/API.
`applyVoiceFollow` does not retune. Status `locked` is always false.
Remaining gaps stay in `docs/INMARSAT.md`. Do not invent FEC/UW to close them.

## DEC-0104 - FUBAR must not receive or set home lat/lon (2026-09-20)

Evidence: FUBAR is a public website. Visitors with Take control could read/write
`/v1/satcom/observer` and aircraft `centerLat/centerLon`, which is the operator
home used for ISS/Doppler. That is not a useful remote control and leaks location.

Decision: Home observer stays in SDR Town GUI/CLI only. Town `publicStatusJson()`
and aircraft HTTP status omit coordinates. FUBAR removes lat/lon UI and the
satcom-observer proxy. Aircraft map fits aircraft tracks, not home. Satcom Start
from Take-control may send `force=true` (radio control, not location).

## DEC-0103 - Tuner lease, observer map, Doppler ECEF, CLI (2026-09-20)

Evidence: 0.2.67–0.2.74 review then tag v0.2.74 at 4f26f2e. Satcom/Inmarsat `setCenterFreq` stole live WFM/P25;
Dual Tuner Soapy make/setup raced `readStream`; diversity did not retarget listen;
CPR used signed `fmod`; TLE used GUI-thread `QEventLoop`; Inmarsat claimed FEC/AMBE
without unique-word. SGP4 look angles treated TEME as ECEF (range-rate missing Earth rotation).

Decision: DeviceManager lease (Listen/P25 > Satcom/Inmarsat/Aircraft; `force=true` to steal).
Doppler via `lookAnglesTeme` (GMST TEME→ECEF + ω×r). Home lat/lon from clickable OSM map
(and `observer set`) is the observer for ISS SSTV, passes, and Doppler. TLE over WinINet
off the GUI thread; `tle load` for fixtures. CLI covers observer/tle/satcom/inmarsat/aircraft.
Inmarsat remains an experimental prototype (no unique-word/FEC claim). P25 DSP untouched.

## DEC-0102 - RadioReference site CSV aliases (2026-09-18)

Evidence: fubarzi/TG-SITES `trs_sites_*.csv` uses RFSS, Site Dec/Hex, Description,
County Name, then ragged frequency columns. SDRTrunk treats site labels as
presentation identifiers attached to a system, separate from decode policy.
Extend the existing system-scoped alias list with optional `sites` entries keyed
by RFSS+site ID (site 0..65535 for RadioReference directories; over-the-air
matching still uses the decoder's reported site). Import via the same CSV action
with header detection; require an explicit destination system. Preserve
talkgroups on site reimport and sites on talkgroup reimport; manual overrides
survive. Ignore NAC/lat/lon/frequencies for RF decisions. Resolve names only in
control-log `site=` text and talkgroup Alpha Tag tooltips when system metadata
is known. Same 1MiB/10000 combined budget and atomic save rules as DEC-0100/0101.

## DEC-0101 - RadioReference-compatible CSV imports (2026-09-18)

Reference: https://trunkrecorder.com/docs/CONFIGURE#talkgroupsFile documents
direct RadioReference CSV headers Decimal, Hex, Alpha Tag, Mode, Description,
Tag, Category. Use Decimal plus Alpha Tag (Description fallback), Category
(Tag fallback) by header, not column position. These files lack WACN/System ID;
require an explicitly selected or newly created destination system. Never infer
it from TGID, filename or receiving frequency. Imported Mode/Priority/NAC must
not change any receive/security policy. Preserve existing manual overrides.
Qt has no CSV parser in the present dependency set: implement a small bounded
RFC4180 state machine with quoted commas, escaped quotes, embedded newlines,
CRLF/LF, UTF-8 BOM and explicit UTF-16 BOM. Reject malformed rows/quotes,
invalid encodings, duplicate IDs/headers and conflicting optional Hex IDs.
No silent skipping. Same 1MiB/10000-entry budget as JSON. Blank rows ignored;
blank headers, empty datasets and conventional frequency exports rejected.
Expose a distinct Import CSV action alongside JSON; destination dialog shows
system hex IDs and final list preview remains staged until Save. Tests use the
published column schema, adversarial CSV and real Qt destination workflow.

## DEC-0100 - System-scoped P25 alias lists (2026-09-18)

Evidence: SDRTrunk Playlist-Editor wiki (accessed2026-09-18), Aliases and
Channel General sections: named alias lists label identifiers independently
of decoding, attached to channels. Local P25TalkgroupEntry already stores
WACN/System ID and manual alphaTag. Implement independent presentation-only
lists keyed by WACN+System ID; never infer a system from TGID or frequency.
Unknown system identity gets no imported label. Existing alphaTag takes priority.
JSON version1 import/export uses Qt's structured parser; no new dependency or
claim of SDRTrunk XML/RadioReference API compatibility. One list per system,
bounded to1MiB/10000 aliases per document, with strict integer ranges, duplicate
rejection, bounded UTF-8 text and explicit source/date. Review before saving.
Editable names/groups become protected manual overrides on subsequent imports.
All imported metadata stays outside scanner/encryption/priority/voice decisions.
Persist atomically via QSaveFile; reject external changes while editing rather
than overwrite. Never erase an unreadable database. Tests cover malformed input,
system isolation, manual precedence, merge, roundtrip, storage and GUI workflow.

## DEC-0099 - Experimental live NFM SSTV session and window (2026-09-18)

Attach one bounded SSTV queue to the main receiver's existing raw NFM tap.
SstvReceiverFeed serializes attach/detach against producer publication with a
short mutex; producer only try-locks and records contention as a discontinuity.
No queue allocation until attach. Detach quiesces publication before stop.
Receiver ownership is captured on GUI start, not accessed through MainWindow
from a worker during teardown. Never change frequency, mode, bandwidth, audio
LPF or squelch to start SSTV; reject inactive/non-NFM/P25 monitor receivers.
Mode/source loss and retunes fail closed through existing metadata/gap checks.
No HF SSB SSTV claim: that requires a separate clean linear-demod audio tap.
Extend recorded window with source selection and Live NFM option. Finish
quiesces publication, drains the existing queue, closes input normally and saves
validated complete/partial images; Cancel and
close invalidate provisional output and join the worker. Live session capped
at six minutes (existing resource budget), no automatic hidden reattach.
Latest-only previews and existing new-directory PNG publication are retained.
Recorded and live controls remain mutually exclusive. Gate: attachment and
teardown tests, actual streamed GUI recording parity, interruption/finish,
small/large screenshots and existing regression suite. An off-air SSTV image
still needs a known transmission; report that acceptance gap explicitly.

## DEC-0098 - Bounded single-stream SSTV worker (2026-09-18)

Compose DEC-0095 events, DEC-0097 conversion and DEC-0096 helper on a single
non-GUI worker thread. Source callback returns data, idle or explicit EOF;
it must not block. Validate source/generation/epoch/rate/frequency/positions
again at the boundary. Initial gap markers are permitted before any samples;
a gap during a stream aborts the provisional image, never joins two sources.
Future live controller may start a new worker after that abort; no hidden retry.
Use a private QTemporaryDir, --stdin --progress and the existing bounded row
parser. Accept results only after EOF, normal zero exit, final row validation
and exact equality with helper RGB files. No output publication in this layer.
Resource budgets: 360 seconds input and 420 seconds wall per session, four
images (existing helper), 128 KiB queued stdin, 64 KiB stderr, 4 MiB stdout.
Poll waits <=5 ms; launch deadline 5 seconds, blocked-pipe deadline 5 seconds,
EOF completion deadline 10 seconds. These are worker resource/failure limits,
not demodulation or synchronization constants. Owner drains stderr/stdout on
each iteration; destructor guard kills/reaps on exceptions/cancellation.
Gate: independent recordings through actual queue/converter/helper match the
direct converted recording, progressive callbacks occur on worker, idle cancel,
explicit gaps/identity faults reject, and startup/EOF errors leave no child.
No GUI or RF wiring until this combined path passes. P25 untouched.

## DEC-0097 - SSTV streaming rate conversion (2026-09-18)

Use the bundled miniaudio linear resampler with its default fourth-order
anti-alias filter; preserve its state across blocks. Source: miniaudio.h
Resampling section and ma_linear_resampler_set_rate_internal, which reduces
integer rates by GCF and derives normalized filter coefficients from them.
Avoid set_rate_ratio's millionth-ratio truncation. Express input Hz rounded
to 0.0001 Hz and 48000 output Hz in the same scaled units (10000); maximum
960000000 fits uint32 and fractional accumulator sums remain below uint32.
At minimum 8000 Hz, 0.00005 Hz rounding error over 360 seconds is at most
0.108 output sample at 48 kHz. 48 kHz is a backend-supported worker format,
not a changed RF setting. Exact 48 kHz input bypasses conversion unchanged.
No AGC, speaker filters, time-based flushing, synthetic tails or padding.
Only a non-real-time worker owns the converter. Invalid input invalidates it;
an explicit start(rate) is required after discontinuity. Input blocks retain
the queue's 8192-sample bound; output allocations are bounded by conversion
ratio, not session length. Gate: chunk-invariant samples, bounded count drift,
tone fidelity, restart/error isolation, and independent image decode checks.
This does not qualify live RF reception or arbitrary weak-signal sensitivity.
Independent image gate: compare full images against the same upstream pictures
before/after conversion; allow at most +1 mean absolute 8-bit RGB level versus
native-rate decoding (engineering regression budget, not a protocol limit).
Partial recordings must remain partial with the same row count. Record measured
errors even on failure; do not loosen the gate to accommodate a poor converter.

## DEC-0096 - Streaming SSTV helper transport (2026-09-18)

Pinned sstv/src/decoder/mod.rs from_samples accepts Iterator<Item=i16> and
events consumes it incrementally. Add --stdin as the input argument using a
bounded buffered little-endian PCM reader, shared with file input. Keep the
same mode, sample rate, four-image and 360-second limits, row protocol and
pixel algorithm. Short reads, split samples and Interrupted reads are handled;
odd EOF, I/O errors and over-budget input cause a nonzero exit. Output remains
provisional until successful exit, as in the existing C++ file wrapper.
Flush metadata as well as rows so a pipe consumer sees completed images promptly.
The session owner must drain stdout/stderr concurrently, close stdin at its
sample budget and kill/reap on cancellation or a wall-time deadline. Blocking
stdin reads run only in this child, never on RX/GUI threads. No detached workers.
Gate: malformed-reader units plus independent Robot36/Martin1 full/partial
file versus pipe RGB/metadata parity and scanlines observed before stdin closes.
This is a transport milestone, not live RF acceptance. Fractional-rate input
conversion and GUI/receiver integration remain T-0031; never round a rate as
a substitute for resampling. No new dependency or P25/audio path change.

## DEC-0095 - Bounded live SSTV ingress before radio wiring (2026-09-18)

MainWindowP25Orchestration's analog branch already exposes FmMultiplexBlock
for NFM before speaker processing; Demod.cpp stamps rate, tuned identity, epoch
and sample position. Do not feed post-squelch speaker buffers or WFM MPX into
SSTV. First implement/test an isolated NFM ingress contract, not a live feature
claim. One producer, one consumer; fixed preallocated 16 slots x8192 floats,
also capped at two seconds of input by sample rate. These are memory/resource
budgets, not latency targets or protocol timers. Producer uses try_lock and
never waits on the decoder; contention, invalid blocks, missing/overlapping
samples, source/rate/tune/epoch changes and queue overflow flush queued input
and emit explicit discontinuity before new data. No sample splicing or padding.
Consumer/control operations may lock; they never run in RF/audio callbacks.
The owning session must detach/quiesce its producer before start, stop or
destruction; the queue alone does not cancel in-flight receiver callbacks.
This explicit lifecycle requirement must be tested again when wiring RX.
Next steps are actual streaming helper input, fractional-rate conversion and
GUI attach/detach/retune integration plus independent replay parity. Keep those
unavailable until proven. No P25, speaker or receiver wiring in this foundation.

## DEC-0094 - Progressive recorded SSTV and 0.2.58 release (2026-09-18)

Pinned backend Decoder::events() emits ImageStart/Row/ImageEnd while consuming
a pull iterator. Complete the recorded GUI feature before live push integration.
Optional helper --progress emits bounded JSONL RGB scanlines; no DSP or pixel
math changes. C++ validates schema, dimensions, image index, unique row indices,
hex length/content and final metadata/row agreement. Compare assembled preview
pixels with final RGB output before publishing PNGs. Limits: <=4096 bytes/line,
4 MiB stdout total, 64 KiB stderr, four images and prior time/input bounds.
These are transport/resource budgets, not protocol sensitivity constants.
Preview snapshots at eight-row intervals go into one mutex-protected latest
image, sampled by a GUI timer; never queue a full image per scanline. Clear
preview on failed/cancelled work. Saving remains final validated output only.
Test fragmented/malformed/duplicate/out-of-order row transport and independent
recordings, retaining exact GUI/direct output parity. Publish GUI+progressive
recorded reception as 0.2.58 with tested signed assets. Live RF is explicitly
not in this release and remains T-0022's next stage; P25 remains untouched.

## DEC-0093 - Recorded SSTV GUI on the shared file decoder (2026-09-17)

T-0022 continues with a nonmodal Tools window, one offline job per window and
one window per main application. Reuse DEC-0092 decoding unchanged; add an
optional cooperative cancellation callback, checked during PCM loading and
helper waits. Kill/reap the owned helper on cancellation. Cancellation ends
before output publication; once saving begins finish the bounded four-image
transaction rather than presenting a half-written result as cancelled.
QThread owns file decode and QProcess, never GUI or receiver/DSP callbacks.
GUI receives completion on its owner thread, keeps original-resolution images
and scales only previews. Open source, new output directory, mode selection,
decode/cancel, result list and output-folder action. Partial and no-image states
remain explicit. Close while running defers until worker stops; destruction
requests cancellation and joins, never terminates a thread.
Test real Qt widgets with a deterministic worker for single-job/close/error
cases, and the actual pinned decoder with independent recordings. No live SSTV
or P25 modification, no fabricated percentage progress or quality score.

## DEC-0092 - Isolated recorded SSTV image backend (2026-09-17)

T-0022 next gate: evaluate MIT-licensed unexcellent/sstv commit
16bf34aac81b0041f5fdce52a1aef64eea0d5f6e as a pinned Rust helper, rather than
copy GPL reference implementations or invent image demodulation. Its event API
reports actual rows and completeness; retain those distinctions. Only std and
libm dependencies are needed with C++ handling audio-file loading and PNG saving.
Development toolchain Rust 1.88 is installed inside build/toolchains only, without
altering PATH or unrelated projects. Pin Cargo.lock and retain dependency notices.

Recorded-only CLI first: bounded mono WAV/FLAC, <=360 seconds/128 MiB, temp PCM,
argument-array QProcess launch of application-adjacent helper, 120-second worker
deadline, capped metadata and <=4 images with bounded dimensions. No shell,
network, live radio changes or unbounded output. Save complete and partial
results explicitly, never infer protocol correctness from a plausible picture.
Compare a real off-air Robot36 recording and independent Martin recording before
shipping image modes; unsupported modes stay unavailable. Image GUI/live routing
remain separate. Release 0.2.57 includes only gates that actually pass.

## DEC-0091 - Bounded SSTV VIS inspection first (2026-09-17)

T-0022 begins with native recorded-audio header inspection, not image reception.
QSSTV 8c27d6d169d8c6c197eb47c2089870e39bc06a02 sstvtx.cpp/sendPreamble/sendVIS
defines 300 ms 1900 Hz, 10 ms 1200 Hz, 300 ms 1900 Hz, 30 ms start,
eight 30 ms LSB-first data/parity symbols (1100=1,1300=0), 30 ms stop at 1200.
sstvparam.cpp lists the parity-inclusive mode codes. Independent colaclanth/sstv
3e556eee8ad4c4425799cb652bac26ee58f8e113 supplies m1.ogg and its expected mode.
Both projects are GPLv3; no source is copied/linked and their audio is kept in
build-only QA, not redistributed without a separate fixture rights review.

Implement the published tone/framing facts, with a fixed 1 ms search grid and
10 ms rectangular Goertzel probes. This diagnostic requires >=75% normalized
tone energy and a 2:1 winning tone ratio: deliberately conservative engineering
acceptance gates, tested on synthetic tones and the independent recording, not
claimed as RF sensitivity specifications. Probe the leader throughout, require
break/start/stop and even parity; unknown seven-bit IDs remain unknown, not a
guessed mode. No extended/narrow VIS or image success claim. Bounded 910 ms
history, <=8192 samples/call, explicit reset on source loss, finite samples,
mono WAV/FLAC 8..96 kHz and 120 seconds/file. Output events/counters in CLI JSON.
No dependency added, no P25/analog routing changes, no pretend image adapter.
Follow-up gate reproduced Unicode recording-path failure: the existing narrow
fopen path uses Windows code-page semantics. Convert CLI UTF-8 to a native
filesystem path and use miniaudio's wide-file API on Windows for this new loader.
That alone did not pass: CLI batch echo already contained question marks before
file loading. Build batch strings from QCoreApplication::arguments() after Qt
initialization instead of the CRT narrow argv. Keep flag parsing/DSP unchanged;
rerun existing ASCII CLI regressions alongside the Unicode recording test.

## DEC-0090 - Checked release publication (2026-09-17)

T-0027: release.ps1 currently ignores native failures and pushes master although
the working branch is fix/acch-rescue-clear-grant-mac. Require an explicit version,
default to experimental, check each native command, run CTest before packaging,
and push the actual attached branch. Require committed source before packaging;
release metadata is committed only after successful packaging/signing. Validate
portable contents, installer/manifest hashes and detached signature before upload.
Publish 0.2.56 with the accumulated tested decoder/workspace/runtime work; do not
claim SSTV or satellite reception implemented. Those reference gates remain open.
No P25 tuning/security changes in this release-hardening pass.

## DEC-0089 - Deploy the configured RTL runtime (2026-09-17)

T-0026 evidence: CDB shutdown_probe_05 captures AV in libusb control transfer
under RTL/Soapy unmake. Standalone probe_rtlsdr_lifecycle.py (no Qt/Soapy/DSP)
reproduces close AV with shipped rtlsdr.dll on cycle 2; the configured vcpkg
rtlsdr 2.0.2 package passes 10 cycles. Both libusb DLL hashes are identical.
The executable folder contained a legacy v0.7.0-190-gdfd8 DLL not the manifest's
configured dependency. Make executable builds stage the imported rtlsdr target
deterministically, retain the old binary under build for forensic comparison,
and include its dependency licence in staging. No driver API, gain, DSP, slot,
queue or teardown timeout changes. Validate deployed native probe and actual
GUI under CDB, then reception. Physical Blog V4/RSP testing remains unperformed;
do not equate the Generic R820T acceptance with all hardware certification.

## DEC-0088 - Debugger-backed GUI acceptance (2026-09-17)

T-0026 intermittent shutdown AV has no root-cause stack yet. Extend existing
RDS GUI acceptance with an explicit CDB executable option, first-chance AV
stack logging and a strict failure if any AV occurs, even if application code
catches it. No system-wide debugger settings, registry edits, driver replacement
or speculative teardown patch. Retain normal real-hardware/station gates and
bounded process waits. Debugger exit alone is not application acceptance.

## DEC-0087 - Independent RTL capture comparison (2026-09-17)

DEC-0086 established native/adapter parity but not RF acquisition. Existing
rtl_sdr CLI can capture the same device without SDR Town/Soapy. Compare short
98.1 MHz captures at requested 40 and 20 dB, with identical offline analysis.
These are controlled diagnostic gain settings, not new app defaults or a
presumed fix. Extend the diagnostic to explicit unsigned 8-bit IQ, centered
at 127.5 and scaled by 128, with size validation and independent tests. Preserve
the sample rate/gain/tool output and decoded groups. No P25 changes.

## DEC-0086 - Same-input live RDS parity diagnostic (2026-09-17)

Two DEC-0085 live runs failed PI/PS identification while recorded adapter/native
parity passed. Add an opt-in SDR_TOWN_RDS_PARITY_LOG diagnostic wrapper: feed
identical borrowed MPX to the native backend and adapter, compare all published
decode fields except wall-clock timestamp values, count disagreements and
write one JSONL summary per exercised receiver at destruction. Reset/source
semantics match the adapter's documented contract. No sample logging, queues,
threads, DSP threshold changes or default extra decoding. Diagnostic overhead
is explicitly double RDS decoding and is not a performance benchmark. Never
treat agreement alone as successful reception. CLI fixture tests must validate
the report before the actual GUI live test uses it.

## DEC-0085 - Shared receive decoder contract, RDS-first adoption (2026-09-17)

T-0021: existing RdsMpxDecoder, CtcssDecoder and DcsDecoder already have bounded
synchronous input and thread-safe native snapshots. Wrap them behind a small
versioned receive interface; immutable registry lists only these implemented
decoders. Explicit float-domain (raw MPX vs discriminator), source ID, sample
clock, frequency identity, epoch, absolute cursor and gap flag accompany every
borrowed block. No retained spans, added queue, hidden resampling or new thread.
Reject wrong domain/version/invalid metadata, clear backend state, and force
reacquisition. Source changes force discontinuity even if epochs/cursors match.
Concrete snapshot variants preserve existing typed fields; factory selection
is allowlisted. Registration means compiled adapter, not guaranteed DLL/RF
availability. Backends still perform their own content validation and loading.
Adopt RDS in both GUI and CLI MPX file replay only after direct/adapter parity
on the independent recorded MPX fixture and synthetic tone adapters. Existing
tone file paths remain intact; P25 is not registered or migrated. No nominal
SSTV/satellite support entry until a real decoder exists. New libraries are
internal CMake organization, not new third-party dependencies.

## DEC-0084 - Experimental receive-only DCS (2026-09-17)

ETSI 103236 section 4.2 specifies 23 bits, 134.4 baud, LSB-first and physical
deviation polarity. test_dcs_reference.py independently checks codewords and
Golay polynomial 0xC75. Use SDRTrunk's enumerated payload values as protocol
data, not its reversed-bit I labels. Generate words algebraically and index
cyclic alignments and actual complements, retaining ALL equivalent labels.
Engineering profile, not certified squelch: three identical words spaced 23
bits confirm; expire after 46 bits without repeated-word evidence. Exact parity
only, no error correction. Test every code/polarity/rotation and malformed input.
Experimental recovery: eight staggered integrate/dump phases at nominal 134.4
baud, 2 Hz DC removal, two 300 Hz low-pass poles. Require two agreeing timing
hypotheses and a unique vote winner. These design choices require shaped/noisy/
clock-offset fixtures; no RF performance claim. Preserve all speech and P25 DSP.
Known-radio acceptance remains deferred by user; informational output only.
Catalogue test found 105 values in both our list and the reference, despite the
reference comment claiming 104; exact list comparison showed no differences.

## DEC-0083 - SSTV and public satellite receive roadmap (2026-09-17)

User requests SSTV, public satellites and weather satellites in the expansion
plan. Track per-downlink capabilities and hardware/coverage requirements, not
an unsupported promise of every spacecraft. Preserve the working P25 path and
finish DCS continuity/validation before adding another live decoder.
Sources reviewed: SatDump pipeline documentation and GPL-3.0 license;
gr-satellites supported-satellite documentation; ON4QZ/QSSTV; NOAA POES status
page search result (direct page fetch returned 403). Links and gates are in
SATELLITE_AND_SSTV.md. These references identify candidates, not dependencies
approved for bundling or proof of current transmitter operation.
Prefer a proven backend where appropriate, with pinned versions, explicit
input/output contracts, license review, bounded worker queues and cancellation.
An external process can isolate faults; it does not waive license obligations.
No network catalogue import may execute arbitrary commands. No automatic
transmit, decrypt, or private-traffic collection is part of this milestone.

## DEC-0082 - Preserve NFM data history during same-rate bandwidth updates (2026-09-17)

Live CTCSS GUI QA failed: 22 resets, zero complete windows at exit, with
automatic bandwidth 7605.46875 Hz. Inspection found exact bandwidth inequality
restarting the data stream even when its sample clock and FIR length agree.
FIR delay stores input IQ, not coefficient-dependent output: retain that history
on NFM coefficient updates when rate and length are unchanged. Continue resetting
on source gaps, rate/length/mode/center/identity changes and explicit DSP resets.
Do not change WFM behavior or speech DSP. Require varying-bandwidth regression,
bit-identical tapped/untapped speech, and repeated live GUI measurement.
User deferred independent known-tone RF acceptance until their radio is available.

Follow-up measurement: build/ctcss_reset_qa/run.log shows mid-stream reset
reason 2 only (explicit/automatic speech DSP reset), unchanged 12500 Hz bandwidth,
unchanged IQ epoch and exactly adjacent cursor. Demod's cumulative >5 kHz AFC
target test resets its speech oscillator. Isolate the NFM data mixer phase;
its nominal identity, source cursor and explicit resetState() define continuity,
not the speech-only automatic target threshold. Retain WFM behavior. Regression
must cross that threshold while proving unchanged speech output.

Final live isolation run passes with eight current-stream windows, but logs
three bandwidth-only GUI resets jumping source cursors (21:01:09/11/18).
syncMonitorVarsToReceiver conflates analog NFM bandwidth adjustments with a
retune, resetting the input cursor and audio ring. Preserve analog NFM stream
when only bandwidth changes; Demod already rebuilds coefficients and resets
data history when its clock/FIR length changes. Keep old reset behavior for
actual frequency/mode changes, P25 voice/control and all other modes.

## DEC-0081 - Receive-only CTCSS identification (2026-09-17)

Next user-approved decoder milestone. Start with informational CTCSS; do not
gate or reshape working audio before independent RF validation. GNU Radio's
gr-analog/lib/ctcss_squelch_ff_impl.cc documents the classic 38-tone set and
adjacent/edge guard-frequency approach. TI SPRA096 documents Goertzel energy
evaluation. Implement our own bounded streaming bank using that mathematics,
not copied GNU Radio code. Link references in docs/NFM_TONES.md.
Engineering acceptance profile (not a certification claim): one-second Hann
windows resolve the closest supported tones, two matching windows confirm,
DC removal and four cascaded 300 Hz low-pass poles suppress voice; >=65% tonal
purity, 4:1 strongest/runner-up energy and +/-1% guard comparisons reject
ambiguous/noisy/off-frequency windows. Test all supported frequencies, gain,
speech interference, noise, missing samples, switches and arbitrary partitions.
Tune/rate/mode/source-gap reset all data-only state. No invented DCS decoder
or tone squelch UI: defer these until reference codewords and RF captures exist.
Nominal channel identity is explicit for the NFM tap so phase-continuous AFC
updates do not erase a tone window. Preserve source-epoch and true retune resets.
GUI tone freshness is two seconds without input (presentation only).

## DEC-0080 - Live RDS and waterfall interaction (2026-09-17)

User requests automatic WFM RDS display, frequency-aligned band sections and
drag tuning while preserving improved audio. Use existing chronological IQ
window provenance (DeviceManager's vector wrapper previously discarded it),
reset only MPX/RDS state on gaps, and run the bounded decoder on the receiver
DSP owner. No RDS processing in P25 branches. UI reads short snapshot locks;
hide identity on inactive/non-WFM/retuned receivers and mark no recent RDS
after five seconds (presentation policy only, never a decoding/audio gate).
Draw disjoint visible band sections using existing priority/ambiguity lookup.
Drag previews on a frozen frequency axis, commits once on release: existing
frequencySelected handler retunes hardware, so emitting on each mouse movement
would queue repeated retunes. Click still commits on release; squelch stays
independent and only owns its spectrum region. Test actual mouse events,
section clipping and metadata lifecycle, plus recorded DSP and full regressions.

## DEC-0079 - RDS DSP integration and measured live gate (2026-09-17)

Partition gate reproduced: [mpx-partition] fails at the second 137-sample
block. Inspection also finds the shared audio FIR reads future input and
zero-pads block tails. Rather than alter audible analog output in this task,
the optional RDS tap will use a separate causal FIR/decimation state over the
same downmixed IQ and existing channel coefficients. Preserve phase across
all chunks; no future samples, no guessed gap filling. Disabled path remains
unchanged and enabled/disabled audio must still compare bit-for-bit.

Use pinned redsea subcarrier/liquid wrappers, not a newly invented carrier or
clock recovery. Existing liquid-dsp submodule is 9e00870e25ce9ecf473b7474875a19a3dfc52ce9;
main CMake explicitly disables MSVC integration. Probe installed GCC 11.3
MinGW to build an isolated DLL with a versioned C ABI: opaque handle, floats
in and byte bits out, caller-owned buffers, exceptions caught at the boundary.
Never pass std::complex, STL objects or ownership of allocations across CRTs.
If successful, reproducible CMake helper build and local absolute-path loading
are required; static MinGW runtime linking avoids hidden runtime DLLs.

Vendor pinned redsea DSP with documented minimal adaptations only: extract
MPXBuffer from file-reader dependencies and widen stream counters to avoid
the documented seven-hour timing jump. Full recreation on discontinuity resets
all DSP state, not just the upstream partial reset. Test real upstream MPX
fixture PI 0x6201, chunk invariance, reset/rate errors, noise and bounded input.

Live GUI integration is gated on those tests. Keep decoded metadata separate
from speech PCM and P25; bounded work, receiver identity and stale-state rules
must be explicit. Repair WFM decimation timing only after a failing partition
test, preserving original aligned-block audio. Do not claim RF validation from
synthetic or MPX replay alone. No P25 timeout, gate or vocoder changes.

## DEC-0078 - RDS foundation without changing P25 (2026-09-17)

User explicitly defers further P25 optimisation and approves the decoder
roadmap. Reuse redsea BlockStream/group at commit
7555c9f6259d50718697ee8c9f218ea012c6892c (windytan/redsea), retaining upstream
license and file notices. These modules provide bit sync, CRC and burst FEC
without the full executable's liquid-dsp/libsndfile/iconv dependencies.
No new RF decoder is claimed: this milestone consumes already-demodulated
MSB-first RDS bits, with a CLI fixture path and strict complete-group metadata.
Incomplete groups cannot publish station text; retune/reset clears all state.
Use bounded assembly and explicit PI / radiotext A-B lifecycle.

Add an opt-in WFM multiplex output before audio LPF/de-emphasis/squelch with
actual rate, frequency and reset/overwrite provenance. Disabled by default;
one retained block, no background thread and no audio mutation. First prove
audio equivalence enabled/disabled. Live 57 kHz extraction, carrier/timing
recovery and GUI metadata are next and require reference RF tests. A decoder
registry should follow a working MPX consumer, not list unavailable decoders.

## DEC-0077 - Partition-invariant P25 PCM interpolation and feature QA (2026-09-17)

Live GUI capture 20260917_084229 has five follows, zero IQ overruns and zero
producer drops, but underrun rises and rejected/missing VCWs. These are not
proof that interpolation causes all gaps. Source inspection separately finds
resampleDecodedP25PcmWithState reads idx+1/idx+2 and clamps them to the current
block tail. Therefore identical PCM split into frames differs from one batch.
Extract this function unchanged for a failing partition-invariance test. If
reproduced, use a two-input-sample causal delay so its cubic stencil uses only
available samples, retaining phase/DC state and exact frame counts. No new
vocoder, smoothing, gates or buffering thresholds. Test 8 kHz to 48/44.1 kHz,
20 ms and irregular partitions. Delay is stencil support, not guessed jitter.

Visual QA also found dB tick labels mapped upside-down over the entire widget
instead of using the spectrum curve transform. Correct that mapping. Band-plan
Qt tests must use explicitly selected settings format: the org/app constructor
does not follow setDefaultFormat, so it persisted the test's US selection.
Repair test isolation and restore AU. Report coverage/remaining RF gaps honestly.

## DEC-0076 - Explicit receive-band profiles, not inferred protocols (2026-09-17)

Replace the mixed-country first-match table with immutable selected profiles
and value-returning lookup. Region/country/location are explicit metadata;
local profiles can be imported from bounded validated JSON. Use half-open
intervals, priority then narrowest span; equal-rank incompatible overlaps must
not force a mode. Most-specific mixed/data entries block broader analog hints.
Band-derived defaults apply through existing AUTO paths, not forced changes to
manual modes or P25 follow. No plan grants encryption/decoder trust. Label
decoder hints separately from decoder availability. No IP/geolocation lookup.

Primary references checked 2026-09-17: ACMA Australian spectrum plan and CB
class licence, Ofcom UKFAT/PMR446 guidance, CAA aeronautical stations, USCG
marine channel table, NOAA NWR frequency list. Sources and partial coverage
travel with each profile. Receive bandwidths are application defaults, not
channel spacing or regulatory limits. New dependencies: none.

## DEC-0075 - Presentation-only workspace foundation (2026-09-17)

User approved the next-feature roadmap. MainWindow currently stacks receiver,
saved-frequency, P25, TX and capture controls in one QVBoxLayout. Reparent the
existing widgets into named Qt docks; retain all signal handlers and DSP paths.
Use a small WorkspaceLayout owner for presets, versioned QSettings state,
visibility/lock actions and reset. No custom docking dependency, decoder rewrite,
or pretend RDS implementation. Keep runtime-automated sessions from overwriting
the normal saved layout. Expose preset and screenshot arguments for GUI QA.
Gate: Qt interaction/persistence tests, real GUI screenshots at different sizes,
full existing tests, unchanged P25 reference replay. This presentation work is
independent of unresolved all-call P25 audio acceptance.

## DEC-0074 - Serialize exceptional audio cursor mutation (2026-09-17)

AudioEngine callback loads r, copies PCM, then stores advanced r. clearBuffers
stores w to that same read cursor without excluding the callback; bridge
discard also writes it. An in-flight callback can overwrite a clear/discard
with its stale r. Producer reuse can then race with its sample reads.
Use a per-ring consumer lease for the callback and exceptional clear/discard.
The callback only tries once and emits silence if control owns the lease:
it never waits, allocates or logs. Non-RT clear/discard waits for an active
consumer to finish. Normal SPSC pushes remain concurrent with consumption.
Do not attribute recorded live gaps to this race without capture evidence.

## DEC-0073 - Walk this-window validated block tails (2026-09-17)

`[block-tail]` fails on physical burst 12: with one permitted lock and two
complete valid superframes in one block, the second frame's A/B bursts are
lost. Uncovered-sync recovery only sees C/D S-ISCH. Permit the existing
bounded complete-burst walk for block input only when a lock in this very
commit established the current anchor. Never extend a retained previous-eye
anchor in block mode. Use the existing two-dibit local sync tolerance for
that current block. Preserve selected-slot/mask/security processing.

## DEC-0072 - Reject nonphysical Phase 2 quadrant permutations (2026-09-17)

Complete 060515 trace at 06:57 shows burst absolute dibit 57777 decoded
twice: the bad eye swaps every 0/2 (74 symbols) and preserves all 1/3 (86)
relative to the correct eye. I-ISCH goes from zero errors/location 0 to six
errors/location 2; codec repeats follow. The search admits all 24 arbitrary
quadrant permutations. SDRTrunk DQPSKGardnerSymbolEvaluator maps cyclic
angles -135,-45,+45,+135 to 3,2,0,1. Rotation/conjugation preserve opposite
dibit pairs (XOR 3); a 0/2-only swap does not. Restrict Phase 2 traffic search
and remembered candidates to these eight physical rotations/reflections.
Leave Phase 1 search unchanged. Verify all permutations, reference IQ, and
latest IQ before judging improvement; do not relax security to get audio.

## DEC-0071 - Complete opt-in replay provenance (2026-09-17)

Explicit validation still throttles pre-gate records to 250 ms and final
records to 100 ms, hiding frames in 80 ms replay hops. Permit unthrottled
records only with explicit validation plus SDR_TOWN_P25_VALIDATION_ALL=1.
Keep default/automatic logging bounded and existing rotation. This is an
offline forensic tool, not a live performance benchmark or gate relaxation.

## DEC-0070 - Explicit replay end-of-stream drain (2026-09-17)

GUI reference replay 103841 decoded 404 PCM frames but left 5760 samples
pending until its eight-second drain timeout: the empty speaker ring required
a 240 ms startup prime, while the entire remaining tail was 120 ms. At an
explicit end of stream no more samples can satisfy that threshold. Bypass
startup priming only for this drain, retaining whole-frame output, capacity
limits and already-applied security/slot gates. Live startup is unchanged.

## DEC-0069 - Resolve slot ownership before selecting session state (2026-09-17)

Reproduced by `[slot-session]`: I-ISCH location 2 rebases local C to absolute
burst 10 / slot 1, but its caller passes slot 0's state. With explicit starting
I-ISCH in the fixture, the original helper loses the known slot-1 talkgroup;
the corrected helper preserves 30302 and its own encryption state.
Use the same I-ISCH resolution and actual
burst position for both session selection and burst labelling in all paths.
Keep missing-I-ISCH fallback, mask phase, and security acceptance unchanged.
Reference: SDRTrunk `SuperFrameFragment` constructs C/D timeslots with the
final-fragment ownership swap before their messages reach per-slot modules.

## DEC-0067 - Exact RS arithmetic caching (2026-09-17)

Accepted after exhaustive arithmetic tests and same-IQ comparison. Replace
repeated GF64 polynomial multiplication/inversion and unit-symbol syndrome
construction with immutable lookup tables. Keep polynomial 0x43, decoder
search order, FEC/CRC criteria, and security gates unchanged. Capture 060515
commit time falls from 4423 ms to 421 ms over 92 windows; reference 103841 WAV
SHA256 is unchanged. No timeout/queue-size tuning is authorized by this result.
Full evidence and remaining gaps: `P25_AUDIO_FORENSICS_20260917.md`.
A decision is recorded **before** code that depends on it is written.

---

## DEC-0066 — Dead unknown-grant parks must return in ~8s (not ~45s)

- **Date:** 2026-09-15
- **Status:** accepted
- **Evidence (capture 20260915_131458):**
  - Return-to-CC stick fixed (0065); monitoring continued.
  - **Zero** `P25 audio output` lines; live_speaker.wav ~30 KB empty.
  - Unknown-enc follows TG12068/10326/… sat **~45s** each with `drop=A`
    `no-vcw-from-live-window` until ACQ watchdog.
  - FSM used `waitingUnknownClearGrant` no-VCW **45000/40000** ms.
- **Decision:**
  1. Unknown-grant + cold dead (no bursts/VCW/speech): **8s tuned / 6s silence**.
  2. Unknown-grant with some structure but still no VCW: **12s / 8s**.
  3. Clear-grant cold dead: **10s / 7s** (was 45s/30s).
  4. Keep long holds only when call was already live (continuation / speech).
  5. Hard-timeout also covers unknown/cold dead (~12s), was excluded before.
- **Out of scope:** why those grants had zero VCWs (RF/slot/enc) — next after
  hangs stop starving CC of follow opportunities.
- **Consequences:** Dead parks free the tuner quickly so the next clear grant
  can be followed. Expect ACQ watchdog ~8–12s on empty voice, not ~45s.

## DEC-0065 — Return-to-CC must follow physical RF, not retunesPrimary flag

- **Date:** 2026-09-15
- **Status:** accepted
- **Evidence (capture 20260915_125341 TG10120 @421.350):**
  - Live clear emits for ~few seconds (CADENCE duty≈0.7–1.0) then drop=A /
    no-vcw; ACQ watchdog returned.
  - Return logged: `continuing muted control-channel monitor on 420.350 …
    without RF retune` while cf was still **421.33875** (voice low-IF).
  - `retunesPrimary=false` because select thought CC "fits" on voice-park LO,
    but start still moved primary LO off CC → P25 log went quiet (no CC in
    passband).
- **Decision:**
  1. On independent-traffic release: if primary RF center is >75 kHz from CC,
     take one-RTL return/warm-standby path even when `retunesPrimary` was false.
  2. When start moves primary LO away from CC (DEC-0015 align path), latch
     `p25IndependentTrafficRetunedPrimary=true`.
- **Out of scope:** sparse mid-call feedRatio/absDup (separate audio duty track).
- **Consequences:** Return after one-RTL voice must retune home and keep watching
  CC. Prove: no `without RF retune` while cf≠cc; expect retune/warm-standby +
  continued stage-lock lines.

## DEC-0064 — Warm-standby must not kill CC monitor on return

- **Date:** 2026-09-15
- **Status:** accepted (amends DEC-0063 idle early-out)
- **Evidence (bridge Monitor-CC → follow → return):**
  - Voice ends; one-RTL warm-standby holds RF on voice while follow flags clear.
  - CC decode/validation resumed off-frequency (`p25CcInPassband` true at 2.048 Msps).
  - After follows >30s, ~28 bad windows → `disableP25ControlMonitorDueToValidation`
    zeros `p25MonitoredControlFreqHz` → P25 log stops; not watching CC.
  - DEC-0063 idle same-CC early-out could skip recovery re-arm while RF still on voice.
- **Decision:**
  1. Pause CC decode + validation while `warmStandbyUntilMs` is active (and while
     one-RTL traffic retuned primary).
  2. On real RF return to CC (immediate / follow / warm-standby expire): reset
     validation + reseed analyzer (same spirit as Monitor CC button).
  3. Failed retune must **not** claim `p25MonitoredControlFreqHz`.
  4. Idle arm early-out only if RF is physically on CC and not in warm-standby.
  5. Status exposes `warmStandbyActive`; analog tune / FUBAR refuse during it.
  6. Bridge P25 control **persists** `autoFollow` (no restore of stale GUI config).
- **Out of scope:** RID/TG decode quality; streaming DDC; DEC-0012.
- **Consequences:** Return-to-CC after bridge follow must resume muted CC watch
  like the normal GUI path. Prove via warm-standby log then
  `P25 control validation armed` / continued CC NAC lines (no disable).

## DEC-0063 — Idempotent P25 control arm + refuse analog tune while follow live

- **Date:** 2026-09-15
- **Status:** accepted
- **Evidence (FUBAR ↔ SdrTownControl.dll):**
  - Follow snaps back to Monitor CC **420.350** mid-call; P25 receive stops.
  - `armGuiRuntimeP25Control` always retuned to CC, cleared
    `p25AutoFollowVoiceFreqHz` / follow flags, and reset the live decoder —
    so a repeated `/v1/p25/control` (or Monitor-CC re-arm) killed the grant.
  - FUBAR analog `Tune` posts `/v1/tune` with `p25AutoFollow=0`, which took
    the non-P25 path and could retune RF off the voice channel.
- **Decision:**
  1. Same-CC re-arm while follow/traffic/voice-freq live → **idempotent**
     (refresh autoFollow + return-CC only; keep RF/voice/decoder).
  2. Same-CC idle re-arm with matching autoFollow → no decoder wipe.
  3. Analog `/v1/tune` while follow live → **409** unless `force=true`.
  4. Status exposes `voiceFrequencyHz`; log control tune requests.
  5. FUBAR bridge refuses Tune when status shows follow/traffic active.
- **Out of scope:** RID/TG decode quality (DEC-0062+); streaming DDC; DEC-0012.
- **Consequences:** FUBAR Monitor-CC / poll must not snap RF to CC mid-grant.
  Prove with `p25-control-arm-idempotent` / `tune-refused-follow` log lines.

## DEC-0062 — Mid-grant talkspurt must reset mbelib (225923 RID garble)

- **Date:** 2026-09-13
- **Status:** accepted
- **Evidence (capture 20260912_225923 TG30304 clear @421.225 slot=1):**
  - Live WAV CLEAR ~24 s; CADENCE ok=18/71 (TG30304 mean duty 0.55).
  - `src=unknown` on all emits — grant SRC never painted; cannot split RIDs in log.
  - GOOD emits (41): feedR≈1.0, absDup=0, push p50=**360 ms**.
  - BAD emits (37): raw feedR≈0.30 but **uniqueFreshR p50=1.0** (absDup==ctxDrop
    always) with fresh_tgt p50=**6** (~120 ms) — overlap tax, not fresh kill.
  - Operator: one RID perfect, other talk on same grant unintelligible.
  - Vocoder / preferred-AMBE / sequencer reset only on call/slot/freq change
    (`p25Phase2SyncAmbeEmitDedupeCallContext`); MAC_PTT only cleared pending
    AMBE queue. Predictor state survived mid-grant talker changes.
  - DEC-0061 240+280 held (workers ctx=280); not a thin-overlap regression.
- **Decision:**
  1. On target-slot **MAC_PTT** after this call has spoken: reset selected
     mbelib + resampler + audioTail + frame sequencer + preferred AMBE
     variants (**keep** abs-dibit de-dupe + security latch + opposite module).
  2. On target **END_PTT / IDLE / HANGTIME** after spoken: set pending flag;
     reset on next target voice or on following MAC_PTT.
  3. Debounce 400 ms against retransmitted MAC_PTT.
- **Out of scope:** inventing MAC_PTT source-address offsets; thinning overlap;
  streaming DDC; DEC-0012.
- **Consequences:** New talkspurts on the same TG grant should not inherit the
  prior RID's AMBE predictor. Re-prove live; expect `VOCODER_RESET
  why=mac-ptt-*` between talkers when MAC is visible.

## DEC-0061 — Restore catch-up overlap 280 ms (153932 jitter)

- **Date:** 2026-09-13
- **Status:** accepted (amends DEC-0060 sizes; restores DEC-0024 overlap)
- **Evidence (capture 20260912_153932 after DEC-0060 280+80):**
  - Live WAV CLEAR but **97** active islands, len p50=**40 ms**, mean≈94 ms;
    short≤80 ms=**75**; chop pairs (island≤120 & gap≤120)=**76**.
  - Worker shapes dominated by **160+80 / 280+80** (ctx=163840); empty-audio
    majority on those eyes; sustain 80+280 almost absent while speaking.
  - Audio top-up **68/69 bridge** (960/1920) at ringFill≈10–12%; underrun
    path inventing continuity between short emits.
  - pcm_vs_wall p50≈0.70 — pace improved vs 152348 half-audio, but thin
    overlap recreated the DEC-0024 eye-death / stutter class.
- **Decision:** Speaker `backlogCatchUp` → **240 ms fresh + 280 ms overlap**
  (minFresh 160). Idle sustain stays 80+280. Dual-slot latch (0058.2) unchanged.
- **Out of scope:** streaming DDC default-on; DEC-0012; absDup budget waste
  (still B-0001).
- **Consequences:** Catch-up keeps DEC-0009 lock surface while advancing ≈
  emit wall (~200–230 ms). Re-prove live duty; expect ctx=573440 on catch-up.

## DEC-0060 — Speaker backlog catch-up must pace RF (152348 half-audio)

- **Date:** 2026-09-13
- **Status:** superseded by DEC-0061 (thin overlap caused 153932 jitter)
- **Evidence (capture 20260912_152348 TG11108 RID 0x243754 clear):**
  - Live WAV CLEAR but longest active run **0.84 s**; active_ratio≈0.52.
  - Emit islands **pcm_vs_wall ≈ 0.45–0.65** (literally ~half wall speech).
  - SRC 0x243754 feed_ratio **0.624** with absDup=184 ≈ missing feed; median
    targetVcw=18 fed=10–12.
  - Worker shapes **99× fresh=160 ms + ctx=280 ms**; emit dsp p50≈**220 ms** >
    fresh 160 ms on **20/28** emit jobs; budget trips while ctxDrop/absDup
    dominate (fed/tgt collapses to ~0.22 on high-absDup hops).
  - pcm_per_fed median **960** — vocoder fine when fed; failure is pre-vocoder
    under-advance + budget spent on context that absDup discards.
  - DEC-0059 ReturnEncrypted only on enc TGs 12068/12069 — not this bug.
- **Decision:** Speaker `backlogCatchUp` geometry → **280 ms fresh + 80 ms
  overlap** (360 ms total, same surface as sustain 80+280). Idle sustain stays
  80+280. Dual-slot latched continuation (DEC-0058.2) unchanged.
- **Out of scope:** streaming DDC default-on; DEC-0012; inventing PLC.
- **Consequences:** Catch-up advances ≥ emit dsp p50 so RF backlog drains and
  CQPSK budget lands on new voice. **Superseded:** 153932 proved 80 ms overlap
  → 40 ms WAV islands / bridge top-ups; DEC-0061 restored 280 ms overlap.

## DEC-0059 — Companion ESS must not abort clear follows (145139)

- **Date:** 2026-09-13
- **Status:** accepted
- **Evidence (capture 20260912_145139):**
  - Live WAV ~14.3 s CLEAR / fragmented (longest run 1.32 s); CADENCE ok=7/60;
    primary FEED_GATE; TG30302 mean duty ~0.25.
  - Clear TG **30302** RID **0x2391D7** emitted ess=clear / targetEss=clear, then
    `ReturnEncrypted trustedEss=yes … ess=enc macCrc=2` while grantLatch=no —
    abandoned mid-call. Encrypted hold then blocked re-follow; RID **0x2353DA**
    later recovered better (absDup=0).
  - Same RF dual-slot: encrypted TG **12068** companion on opposite slot.
  - Root: sticky session-painted ESS / `trafficStatus.diag.encrypted` ORed into
    follow ESS, plus monotonic `RecentTargetEssEncrypted` OR that never cleared
    on later clear observations. Pending drain also released opposite-only
    (targetVcw=0 pendingRel=8).
- **Decision:**
  1. Traffic processor security flags only from **this-burst** ESS/traffic-SO.
  2. Voice target ESS encrypted only from observed this-burst paint; sticky
     clear may keep known, never promote encrypted.
  3. Recent target ESS: this-window clear **clears** sticky encrypted.
  4. MainWindow follow ESS uses target-slot fields; do **not** OR traffic
     `diag.encrypted`. Do not Clear→Encrypted promote while latch is Clear.
  5. Refuse pending AMBE drain on opposite-only windows (targetVcw=0, opp>0).
- **Out of scope:** DEC-0012 softening; streaming DDC default-on; budget/worker.
- **Consequences:** Clear RIDs should survive companion encrypted ESS on the
  opposite timeslot. Still open: absDup/ctxDrop underpush, budget trips (B-0001).

## DEC-0058 — Speaker backlog catch-up + latched dual-slot continuity (142104)

- **Date:** 2026-09-13
- **Status:** accepted
- **Evidence (capture 20260912_142104 TG10301 clear @417.675 ~57s):**
  - Live WAV listen=CLEAR but only **~15 s** / 436 s; longest active run **2.24 s**.
  - Worker windows **63×** iq=737280 fresh=163840 context=573440 (80+280).
  - dsp p50≈**198 ms** / p90≈**585 ms**; worker-busy **56**; budget trips **154**.
  - Emit gaps mean≈**0.9 s** max≈**3 s** — weak/strange choppy islands.
  - Mid-call waiting clear grant with p2ess=clear / callClearTrusted=yes on
    dual-slot MAC-dead hops (target≈opp, mac≈0) after Clear latch.
  - DEC-0032 absolute ban on backlog overriding speaker 80+280 under-advanced RF.
- **Decision:**
  1. When speaker-sustain/active-clear **and** acklogCatchUp: keep **280 ms**
     overlap; advance **160 ms** fresh (minFresh 80). Idle sustain stays 80+280.
  2. Latched/spoken clear + selected-dominant + companion accounted + target ESS
     known clear → do not dual-slot-garble-drop / feed-starve (DEC-0012 companion-
     louder PostEmitMixedMacDead unchanged).
- **Consequences:** Catch-up while lagging without shrinking overlap; fewer
  waiting-clear holes after Clear latch. Budget/worker cost still open under B-0001.
  **Amended by DEC-0060 then DEC-0061:** catch-up sizes are now **240+280**.

## DEC-0057 — Wrong-TDMA status on clear immutable grant

- **Date:** 2026-09-13
- **Status:** accepted (amended after Catch/forensic)
- **Evidence (capture 20260912_135857 TG30003):**
  - Listen WAV **29.2 s CLEAR**; CADENCE ok=16/83; **wrong_tdma=47**; 75 follow
    lines Phase 2 wrong TDMA slot with p2vcw&gt;0 decoded=0 (no concurrent emit).
  - CC grant **SLOT=1** immutable; emit avg targetVcw≈13.7 opp≈6.9 — dual-slot
    RF with companion talker. Slot probe correctly refuses flip when mask known.
  - Logic: phase2WrongSlot set whenever opposite has VCWs and selected has none
    — brands normal companion dwell as wrong slot.
  - Catch with planted final-fragment I-ISCH: lock slips (sfOff=180) and absolute
    index for phys C is **10 → grantSlot 1** (standards C/D swap). Lock-relative
    parity for index 6 would have labelled TS1 and fought the CC grant.
- **Decision:**
  1. Do not set phase2WrongSlot when p25Phase2GrantedSlotImmutable (companion
     dwell / opposite-only silence on our timeslot). Pending drain same rule.
  2. Keep DEC-0055.3: **grantSlot** from I-ISCH absolute index when A/B agree;
     else lock-relative %12. Do **not** force lock-rel-only (that mislabels
     final-fragment C/D and does not stop companion-dwell status spam).
- **Consequences:** Status should stop spamming wrong-TDMA on clear grants while
  slot labels stay standards-aligned when I-ISCH is present. Remaining continuity
  holes still FEED_GATE/budget/worker.


## DEC-0056 — Clear follow hang + WFM default BW

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_134135`):**
  - Enc TGs return in &lt;1 s (`ReturnEncrypted`). Clear TG30302 hung ~57 s after
    speech; live WAV only **5.64 s** CLEAR; CADENCE ok=2/64, budget_trip=47,
    worker_busy=63, primary FEED_GATE.
  - Meta: NFM **12.5 kHz**, `audio_lpf_enabled=false` — P25 speaker is AMBE PCM;
    Demod LPF/WFM BW are **not** on this path.
  - Root hang: `clearTrustedHold` extended 40 s speaker grace with no traffic;
    structure-only `lastActive` refresh; clear no-VCW timers 45–60 s; carrier
    acquire hold after speech.
- **Decision:**
  1. Extended speaker grace requires **current** traffic evidence (not grant-clear alone).
  2. After clear speech, no-VCW return in **12 s tuned / 6 s silence**.
  3. Carrier-acquire hold only in early acquire (`lastActive` ≤ tune+2.5 s).
  4. After speaker/clear latch, do not refresh `lastActive` on structure-only.
  5. WFM/AUTO default BW **220 kHz** (snap 180–250); separate from P25.
- **Not fixed here:** clear mid-call blocky islands (dual-slot/epoch/budget/worker)
  — still open under B-0001; BW/LPF not the cause on Phase-2 follow.

## DEC-0055 — Epoch identity + dual-slot keep-selected PCM

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:** Code audit of IQ→mbelib→speaker: (1) DualSlot MAC-dead
  `audio.clear()` punched holes after Clear latch with labelled selected VCWs;
  (2) `epochTrusted` soft arm `establishedClear && xorApplied && grantSlotKnown`
  fed wrong sticky phase as continuous garble; (3) grantSlot used lock-relative
  `%12` only — I-ISCH flip rejected (20260720) but absolute **origin** unused.
- **Decision:**
  1. **Dual-slot:** Clear latch + labelled selected (`targetVcw>0`, fed,
     strong/trusted) → keep PCM (`dual-slot-untrusted-keep-selected-pcm`); else
     still `audio.clear()`. Feed mute / DEC-0012 companion-louder unchanged.
  2. **epochTrusted:** remove bare establishedClear+xor+grantSlot. Continuity via
     this-burst SF/mask/MAC or `forceEstablishedFeed` only. Prefer hole over garble.
  3. **I-ISCH origin:** when A/B I-ISCH agree, rebase absolute index
     `location*4+local` for grantSlot/mask; missing/disagree → lock-relative.
     Do not flip via I-ISCH alone without origin rebase.
- **Out of scope:** streaming DDC default-on (DEC-0038); DEC-0012 softening;
  invent PLC; budget DEC-0052–0054.
- **Consequences:** Fewer post-clear dual-slot silence holes; wrong-phase feed
  rejects instead of garble; mid-lock slot labels track standards when I-ISCH
  present. Live 100% still needs B-0001 start/stop + listen harvester.

## DEC-0054 — Restore cold full-commit; sticky cheap only (`081416`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:**
  - `061217` (pre-0052): ~**92 s** CLEAR WAV, many emits — almost fully clear.
  - `064509` (0052 skip): ~0.6 s golden then silence.
  - `081416` (0053): **0.36 s SILENT** WAV, **1** audio emit, cheap-commit
    log hits **0**, budget trips generic only, worker-busy 147.
  - Root: CQPSK half-budget headroom stopped search before
    `realtimeBudgetExceeded()`, so forceCheap never re-armed; when cold
    forceCheap did run it poisoned first eye (phase0 / no deep rescue).
- **Decision:**
  1. **Remove** DEC-0052 CQPSK commit-headroom reserve.
  2. **Cold / non-sticky:** never forceCheap; re-arm
     `kP25LiveColdCommitAllowanceMs` (**200**) and full annotate.
  3. **Sticky only:** cheap-commit + re-arm
     `kP25LiveStickyCheapCommitAllowanceMs` (**120**).
  4. Never skip-commit (064509). Keep DEC-0051 mid-loop aborts.
- **Consequences:** Aim to restore 061217-class continuous CLEAR follows.
  Worker may again run >80 ms on cold acquires — acceptable vs silence.

## DEC-0053 — Sticky budget path cheap-commits (not skip) (`064509`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_064509`, ~257 s, post–DEC-0052):**
  - First follow emit ~0.6 s **CLEAR**, then permanent silence; only **2** emits
    total (WAV **1.24 s**) vs `061217` **91.96 s** CLEAR / **478** emits.
  - CADENCE ok≥0.65=**0**; no_vcw **470**; budget_trip **4**; emit p50 **170**
    (improved) but continuity collapsed.
  - eye-lost **0** — not DEC-0048. Skip-commit after sticky ready zeroed VCWs.
  - Cheap-commit with already-expired CQPSK deadline also emits 0 VCW (annotate
    loops break on entry) — need a short re-arm allowance.
- **Decision:**
  1. **Reject** sticky skip-commit. Sticky+budgetGone → **cheap-commit** on
     sticky lattice (same forceCheap path as cold).
  2. Re-arm `kP25LiveCheapCommitAllowanceMs` (**50**) before cheap annotate.
  3. Prefer `cheap-commit` tags in p25_log budget-trip lines.
  4. Keep CQPSK headroom (DEC-0052); do not soften DEC-0012; streaming DDC off.
- **Consequences:** Expect multi-second CLEAR follows again with bounded dsp;
  logscan should show `cheap-commit sticky-sustain` not silence after first emit.

## DEC-0052 — Close mustAnnotateCommit budget hole (`061217`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_061217`, ~713 s, gapless, SNR≈17.9):**
  - Live listen overall **CLEAR** (3 brief GARBLED 2 s islands) — almost fully
    clear subjectively.
  - CADENCE n=676: A=446 D=197 ok=**20**; duty mean 0.135 max 1.038.
  - Emit dsp p50≈**223** / p90≈**562** (worse than 044651’s 212); worker-busy
    **690** (~0.97/s); rolling busy max still **~15.9 s**; wall-timeout **0**.
  - DEC-0051 budget-trip log hits: **0** — early-out never ran.
  - Root cause: `mustAnnotateCommit = phase2CqpskTrafficDemod || …` is always
    true on live Phase-2 follow, so processIq still ran full annotate/commit
    after the deadline; CQPSK also consumed the whole 80 ms budget before
    commit.
  - File bars on clear islands: duty 0.67–0.78 listen=CLEAR; LIVE_WORSE is
    continuity (busy cliffs), not RF/mbelib.
- **Decision:**
  1. Sticky sustain + budget gone → **skip full commit** (free worker; next hop
     continues lattice). Log `[p25][budget][dec0052] skip-commit…`.
  2. Cold first-eye + budget gone → **cheap commit** only (no 12-phase /
     deep rescue); flag `m_phase2ForceCheapRealtimeCommit`.
  3. Reserve ~half of realtime budget as CQPSK→commit headroom (25–60 ms).
  4. Surface budget trips into p25_log (`P25 budget trip:`) + logscan
     `budget_trip` signature.
- **Consequences:** Expect emit p50≪120, busy/sec down, ok→D cliffs fewer;
  listen should stay CLEAR. Do not soften DEC-0012; streaming DDC stays off.

## DEC-0051 — Cooperative mid-decode realtime budget abort (`044651`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_044651`, automated full forensic):**
  - Live CADENCE A=86 D=35 ok=5; TG10120 max duty **0.909**; TG20202 max
    **0.639**; worker-busy **135**; rolling grew to **~15.9 s** while busy.
  - Worker dsp: emit p50≈**212 ms** / p90≈**491 ms**; empty p90≈**228 ms** /
    max **642 ms** under healthy budget **80** / eye-lost **120**.
  - Wall-timeout **0** — DEC-0046 post-hoc wall never fired; jobs still held
    single-flight for hundreds of ms (CQPSK stop ≠ commit/mask abort).
  - File path still recovers RF/mbelib; PRIMARY class FEED_GATE + slow worker.
- **Decision:**
  1. Arm a processIq-scoped deadline (`armRealtimeDecodeBudget`) shared by
     CQPSK search, Phase-2 sync scan, lock walk, 12-phase mask hunt, and
     sticky burst walk.
  2. Abort those loops cooperatively when exceeded; keep best-so-far; still
     run annotate/commit when Phase-2 traffic requires it, but commit itself
     is budget-gated (fixes “mustAnnotateCommit always unbounded”).
  3. Do **not** clamp decode wall (DEC-0046 stands). Do **not** soften
     DEC-0012. Streaming DDC stays default-off.
  4. Catch `[p25][cqpsk][budget]` ceiling **350 ms** (was 2500); verifier
     `verify_p25_phase2_cooperative_budget_abort.py`.
- **Consequences:** Live worker should release near budget so the next 80+280
  eye can run; expect fewer worker-busy cliffs and less rolling explosion.
  Operator still resets PPM≈−2 after DEC-0049; re-prove with start/stop +
  DEC-0050 listen classifier.

## DEC-0050 — PCM listen classifier (CLEAR / GARBLED / SILENT) for live vs file

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:** File IQ replay is repeatedly “pretty good” (duty≥0.65 / PASS) while
  live same-day captures are islands then nothing / subjective garble. CADENCE
  duty, drop A–E, and STT chars/words do **not** score perceptual clear speech;
  duty can pass on blocky/garbled PCM.
- **Decision:**
  1. Add `p25_pcm_listen_classify.py` — frame RMS / ZCR / spectral flatness /
     envelope CV → **CLEAR | GARBLED | SILENT** (orthogonal to duty).
  2. Dump `*_live_speaker.wav` during start/stop IQ capture (speaker-push PCM).
  3. Wire classify into `run_p25_capture_full_forensic.py` +
     `run_p25_listen_bar_harvester.py`; CLI `p25 listenclassify <wav>`.
  4. Fixtures: file-replay WAV goldens + live/file mismatch flag
     `LIVE_WORSE_THAN_FILE`.
- **Consequences:** Automation can fail a “PASS duty” bar when listen=GARBLED,
  and prove live-path regressions without relying on operator ears alone.

## DEC-0049 — Harden Auto PPM after 1250 Hz overshoot (`044651`)

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence (capture `20260912_044651`, ~268 s):**
  - Live: TG10120 ok duty up to **0.909** (5 windows); TG20202 max **0.639**
    drop D; CADENCE A=86 D=35 ok=5; worker-busy **135**.
  - Auto PPM applied **twice** from AFC=**1250.0**Hz conf=**0.45**:
    ppm −1.93 → −4.91 → **−7.88**. Summary residual AFC ~874 Hz. CC TSBK
    showed high dibit corrections / CRC notes after overshoot.
  - File voicetest TG10120 duty **0.64**; TG20202 **0.615** — RF+mbelib OK;
    live “nothing” after good islands is eye/worker, not codec.
- **Decision:**
  1. Reject AFC samples on soft-probe rail ±1250 (±5 Hz).
  2. Min conf **0.55**, max |AFC| **2000**, max step **1.5** ppm, cooldown
     **120 s**.
  3. Apply only from **trusted CC offset** — never fall back to
     `gLastAfcOffsetHz` after Phase 2.
  4. `p25AutoPpmAfcSampleAcceptable` + Catch `[p25][ppm][dec0049]`.
- **Consequences:** Stops LO walk-off. Operator should reset device PPM
  near **−2.0** before next listen. Worker-busy / feedRatio still open.

---

## DEC-0048 — Escalate eye-lost cand=16 on first post-emit miss (`041612`)

- **Date:** 2026-09-12
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence (capture `20260912_041612`, ~215 s, DEC-0046 exe):**
  - Live CADENCE n=100: drop A=82 D=13 B=5; duty max **0.399**; ok≥0.65 **0**;
    worker-busy **103**; Auto PPM **0**; wall-timeout **0**.
  - **Same IQ file** `p25 voicetest` TG20202 slot1 8s:
    `PASS_PARTIAL` **duty=0.805** drop=ok ambe=322/322 — RF+mbelib fine.
  - Live emits briefly then permanent `no-vcw` under healthy cand=4; eye-lost
    waited streak≥2 before cand=16.
- **Decision:** `kP25LiveEyeLostReplayCandStreak = 1` (first post-emit eye-lost
  hop uses cand=16 / 120 ms). Keep healthy cand=4/80. No mbelib-neo, hop/TTL,
  DEC-0012 soften, or streaming default-on.
- **Consequences:** Faster live re-lock toward file duty. Worker emit dsp
  p50≈227 ms still open (cooperative abort).

---

## DEC-0047 — Automated `p25 logscan` deep forensic CLI

- **Date:** 2026-09-12
- **Status:** accepted
- **Evidence:** Live “no audio” invisible to voicetest; need CADENCE /
  worker-busy / wall / Auto PPM / reject-before-feed rollup from startstop logs.
- **Decision:** `python src/tools/p25_logscan.py` + CLI `p25 logscan <dir>`
  prints primary failure class and mbelib-neo advice (only when CADENCE ok).
- **Consequences:** Every listen with startstop is automatable.

---

## DEC-0046 — Do not clamp live decode wall / wipe pending on wall stamps

- **Date:** 2026-09-12
- **Status:** accepted (reverts DEC-0045 clamp; hardens publish path)
- **Evidence:**
  - Operator: after DEC-0044/0045 Release, audio “really bad” again
    (almost-worked → next gap-fix kills it).
  - Wall is checked **after** `decodeP25VoiceAudioBlock` returns — it does
    not cooperatively abort. Jobs can still run ≫ budget.
  - Publish path: `decode-wall-timeout` without keepEvidence called
    `p25Phase2ClearStaleResultSpeakerPending` → wiped playout mid-call.
  - Clamping healthy wall to **105** made empty eyes (>105 ms) hit that
    path constantly → continuous audio death.
- **Decision:**
  1. **Reject** DEC-0045 wall clamp; healthy/eye-lost keep global wall **320**.
  2. Wall stamps (`decode-wall-timeout` / `overbudget-kept`) must **never**
     clear speaker pending; empty overruns publish diags only.
  3. Keep DEC-0044 auto PPM (return-to-control only).
  4. No hop/TTL / DEC-0012 soften / streaming default-on.
- **Consequences:** Restores pre-0045 pending continuity. Worker-busy when
  dsp ≫80 remains open — needs cooperative budget abort, not wall stamps.
  Catch `[p25][dec0046]` + verifiers lock the invariant.

---

## DEC-0045 — Cap live healthy/eye-lost decode walls (`032907`)

- **Date:** 2026-09-12
- **Status:** **rejected** (superseded by DEC-0046)
- **Evidence (capture `20260912_032907`):** emit-gate dsp p50≈451 vs budget 80;
  wall 320. Intent was to yield single-flight sooner.
- **Decision (original):** clamp healthy wall 105 / eye-lost 145.
- **Why rejected:** wall is post-hoc; clamp increased empty-timeout pending
  wipes without shortening jobs. See DEC-0046.

---

## DEC-0044 — Auto PPM from sustained CC AFC (return-to-control only)

- **Date:** 2026-09-12
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:** `032907` / prior captures: ppm device **0.0** while CC AFC
  sits near **~884 Hz**. Soft-AFC cold seed exists; live still acquires then
  cliffs on worker throughput. Manual PPM only today (`setFrequencyCorrection`
  / CLI `ppm`). Baking CC AFC into LO mid-voice fights DEC-0016 park.
- **Decision:**
  1. On **return-to-control** (not warm-standby / not mid-voice), optionally
     apply `estimatePpmCorrectionDelta(CC AFC, CC Hz)` via
     `DeviceManager::setFrequencyCorrection`.
  2. Gates: |AFC| 200–3500 Hz, conf ≥0.45 (or trusted CC offset ≤5 min),
     |Δppm| ≥0.40, step clamp ±5, cooldown 30 s.
  3. Prefer `gP25LastTrustedControlOffsetHz` over live AFC globals after
     Phase 2 park.
- **Consequences:** First follows start closer to LO; does not by itself fix
  worker-busy drop D (that is DEC-0045).

---

## DEC-0043 — Post-speak opp-dominant sticky wipe debounced (twin rescue reverted)

- **Date:** 2026-09-12
- **Status:** accepted (debounce kept; ±1 twin rescue reverted after `024000`)
- **Evidence:**
  - `20260912_020758` TG20201 clear: post-speak opp-dominant sticky invalidate
    wiped proven XOR/epoch → permanent `no-vcw` (file duty ~0.80).
  - `20260912_024000` live: clear TG30003 @421.975 file
    `PASS_CONTINUOUS duty=0.705` but live max **0.649**, wrong-TDMA /
    `no-sf-mask` / worker-busy. Soft DUID ±1 twin rescue + debounce stuck
    bad epochs — twin path reverted.
- **Decision:**
  1. After speak, opp-dominant sticky invalidate requires streak **≥3**.
     Pre-speak keeps immediate invalidate.
  2. **Do not** prefer ±1-burst lock twins via soft DUID scores on block path.
  3. No hop/TTL / DEC-0012 soften / streaming default-on.
- **Consequences:** Live clear continuity still open (worker drop D). Re-prove
  on ENC=clear follows after rebuild.

---

## DEC-0042 — Healthy live sustain uses cand=4 / 80 ms (002128 forensic)

- **Date:** 2026-09-12
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence (capture `20260912_002128`, ~54 min, SNR ~15 dB, DEC-0041 exe):**
  - CADENCE n=2731: mean duty **0.115**, ok≥0.65 only **99**; drops
    A=1891 / D=650 / B=91 / ok=99. worker-busy **2782**.
  - **Same failure on every TG** (30017/30302/30003/10301/10330/20201/…):
    not a one-site grant quirk.
  - Drop D feedRatio **0.445** ≈ ok feedRatio **0.427** — playout duty is
    low because fewer VCWs/windows complete per second (tv 50 vs 117,
    windows 6.4 vs 10.2), not because the feed gate newly blocks.
  - Hard cliffs (duty≥0.50→&lt;0.15, n=27): **every** example co-timed with
    worker-busy in the prior 2.5 s; then targetVcw collapses → drop A.
  - Emit-gate WORKER dsp p50 **199** / p90 **535** ms on 80+280 eyes while
    sustain needs ~80 ms hops under pending=1.
  - **File voicetest** same capture TG **30017** slot1 skip=0 8s:
    `PASS_CONTINUOUS duty=0.83` — RF/MAC are continuous; live extract starves.
  - DEC-0041 eye-lost budget cap alone did not move the mean-duty class vs
    `234224` (0.134 → 0.115 on a longer sample).
- **Decision:**
  1. When post-emit eye is **healthy** (`!eyeLost`): live hot caps
     **cand=4 / budget=80** (`kP25LiveHealthySustain*`).
  2. Keep DEC-0041: first eye-lost miss cand=8/120; streak≥2 → cand=16/120.
  3. No hop/TTL change. No DEC-0012 soften. No streaming default-on.
- **Consequences:** Re-prove file bars 060036/095846. New live CADENCE must
  raise ok seconds / cut worker-busy without collapsing any TG’s peak duty.

---

## DEC-0041 — Live eye-lost re-lock keeps cand=16 inside hot 120 ms wall

- **Date:** 2026-09-12
- **Status:** accepted (implemented; file bars + live re-prove pending)
- **Evidence:**
  - Capture `20260911_234224` (~372 s gapless CC IQ, SNR ~12.5 dB) on ACCH
    branch Release: CADENCE drop **D=101**/338 with `worker-busy` while
    `pendingJobs=0` / `busy=yes`; operator heard short &lt;1 s islands after
    brief emit.
  - WORKER (rate-limited): dsp p50 **69.5** / p90 **461** / max **1515** ms;
    emit-gate dsp p50 **202** / p90 **590**; submit interval p50 **159** ms
    vs DEC-0009 sustain fresh **80** ms.
  - DEC-0035/0039 live eye-lost used full replay caps **cand=16 / budget=240**.
    That matches CLI/voicetest width but on the single-flight GUI worker the
    240 ms wall + block-channelize produced multi-hundred-ms jobs → scheduler
    skips → drop D death spiral. Healthy eye correctly stayed cand=8/120
    (DEC-0019 / 060221).
  - Do **not** soften DEC-0012. Do not invent hop/TTL. Do not default-on
    streaming DDC (DEC-0038).
- **Decision:**
  1. Keep DEC-0039 definition of eye-lost (post-emit no-target counts).
  2. First consecutive post-emit eye-lost hop stays **cand=8 / 120** (cheap
     challenge). Streak ≥2 escalates to **cand=16** (replay width).
  3. Live escalate budget is **`kP25LiveEyeLostReplayBudgetMs` = hot 120**,
     not `kP25ReplayHotBudgetMs` (240). CLI/voicetest may still use 240.
  4. Realtime unknown-mask deep ACCH rescue stays alt-kind capable but is
     bounded to top **2** phases × deep budget **1** (was 4×2 on the ACCH
     branch) so acquisition cost cannot re-feed drop D.
- **Consequences:** Re-prove file bars 060036 / 095846. Live CADENCE on a
  new GUI follow should cut worker-busy drop D without losing DEC-0035-class
  re-lock width after a short miss streak.

---

## DEC-0040 — Mechanical split of `main.cpp` (ISS-0004 / T-0009)

- **Date:** 2026-09-09
- **Status:** accepted (complete 2026-09-10)
- **Evidence:**
  - `src/main.cpp` was ~35k lines: shared P25 helpers, voicetest, `MainWindow`,
    `runCLI`, and `main()` in one TU (ISS-0004). After Phases 0–8: ~2k leftovers
    + `main()`; orchestration in dedicated TUs.
  - String-lock verifiers use `src/tools/p25_orchestration_sources.py`.
  - ISS-0004: must not mix a rewrite with feed-gate behavior changes.
- **Decision:**
  1. Mechanical move-only split into focused TUs:
     `P25AppGlobals`, `P25TalkgroupRegistry`, `P25VoiceTiming`, `P25RollingIq`,
     `P25VoiceDecode`, `P25VoiceTest`, `CliApp`, `MainWindow`, `AppBootstrap`,
     thin `main.cpp`.
  2. No hop/TTL/CADENCE/feed-gate/audio behavior changes in split commits.
  3. Verifiers read concatenated orchestration sources via
     `src/tools/p25_orchestration_sources.py`.
  4. Build + `sdr_town_tests` + verifier sweep after each extract phase.
  5. Branch `refactor/iss-0004-split-main`; push + PR when green.
- **Consequences:** Maintainability for P25/GUI/CLI; follow-up may further
  split the `MainWindow` constructor after this lands.

---

## DEC-0039 — Post-emit no-target counts as eye-lost (DEC-0035 refine)

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Operator: latest live not like DEC-0035 / 095846 (~95%). Capture
    `20260909_110941` TG 10301 slot 1: max duty **0.452**, **0×** ≥0.65
    (095846 same TG/slot max **0.947**, 10× ≥0.65).
  - 110941: brief emits then long `no voice sync` / drop A; 80+280 held.
    Some hops had structure/companion bursts with `targetVcw=0` — DEC-0035
    required bursts==0, so those stayed cand=8 and starved re-lock.
  - Streaming default-on still rejected (DEC-0038 duty 0.23). Product path
    remains block + DEC-0035-class live re-lock.
- **Decision:**
  1. Keep DEC-0035 replay caps (cand=16/240) on eye-lost after speak.
  2. Expand eye-lost: after `hadSuccessfulEmit`, `targetVcw==0` and
     `decodedFrames==0` counts as lost even if companion/structure bursts >0.
  3. Healthy target eye still cand=8/120. No hop/TTL change. No streaming
     default-on.
- **Consequences:** Live should recover toward 095846 continuous clear after
  first emit; prove with a new start/stop capture.

---

## DEC-0038 — Streaming sticky Gardner; do not default-on (060036 still 0.23)

- **Date:** 2026-09-09
- **Status:** accepted (implemented; default-on still gated)
- **Evidence:**
  - Operator ask: match SDRTrunk sticky HDQPSK so live is continuous like
    replay. File bar: 060036 TG 10301 skip=261000 center=421.96375.
  - Block (unset): `PASS_CONTINUOUS duty=0.705` essKnown=yes p2macCrc=156.
  - Stream env=1 before this DEC (DEC-0033): duty **~0.25**, cqpskLock=0.
  - Root in tree: unlocked CQPSK search set `candidateTiming.cqpskValid=false`
    every candidate, cold-acquiring Gardner on each 80 ms eye and defeating
    streaming Costas continuity (SDRTrunk keeps Gardner across chunks).
  - Tried post-commit discrete CQPSK lock create / soft-latch: duty **0.12**
    (worse) — froze a weak map while search stop engaged.
  - After sticky Gardner + warm differential-only shaping: stream duty
    **0.23**, targetVcw=132, p2macCrc=4, essKnown=**no**, still ≪0.65.
- **Decision:**
  1. Under `enableStreamingChannelDdc`, do **not** clear `cqpskValid` during
     unlocked CQPSK candidate search (keep sticky Gardner/Costas).
  2. When streaming Gardner is already warm, skip absolute/conjugate CQPSK
     maps and limit dibit permutations (π/4 DQPSK-shaped like SDRTrunk).
  3. Do **not** create/freeze discrete CQPSK lock from soft structure or
     post-commit alone on streaming (measured regression).
  4. **Do not default-on** streaming (DEC-0014 gate). Env=1 still fails
     duty ≥0.65 + essKnown on the continuous-clear file bar.
- **Consequences:** Streaming mechanics closer to SDRTrunk timing continuity;
  product path stays block-channelize (live DEC-0035 class) until env=1 hits
  ≥0.65. T-0010 / ISS-0001 remain open for streaming default-on.

---

## DEC-0037 — Restore clear-eye rolling hold; never purge on hold

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - After DEC-0036 always-advance, capture `20260909_100909`: operator heard
    little chirps instead of ~95% continuous. CADENCE max duty **0.40**,
    **0×** ≥0.65 (was **0.947** / 10× on 095846). emit>0 36/77 but duty
    mostly 0.04–0.16.
  - DEC-0036 fixed unknown-grant start silence but broke the clear sustain
    path that needed a one-hop MAC/ESS retry on the same RF.
- **Decision:**
  1. Restore `return false` (hold) for normal clear eyes that have selected
     VCWs neither fed nor queued.
  2. Advance only for `waitingForClearGrant` / late-entry waiting /
     `WaitingForClearGrant` diag (095846 TG 30302 class).
  3. On hold: keep `holdDecodeAbsolute` but **do not** purge newer publish
     results or worker jobs (that purge was the start-silence killer).
- **Consequences:** Expect clear follows back near 095846 continuity; unknown
  starts should not wipe the pipeline if a hold still occurs.

---

## DEC-0036 — Do not hold rolling cursor when VCWs were not queued

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Capture `20260909_095846` after DEC-0035: later TG 10301 clear follow
    excellent (max duty **0.947**, 10× ≥0.65). Operator: ~95% good with small
    gaps/lag; **quiet follows at start**.
  - First follow TG **30302** ENC=unknown: window with `targetVcw=14` fed=0
    pendingQueued=0 → `rolling cursor hold` + purge newer jobs → then
    permanent `p2bursts=0` / drop A for ~60 s until TG 10301.
  - Root: `p25Phase2RollingDecodeWindowConsumed` returned false when selected
    VCWs were visible but neither fed nor queued (waiting MAC/ESS). Hold
    re-decodes the same RF and **purges** newer live work.
- **Decision:**
  1. If selected VCWs were not fed and not queued, **advance** the rolling
     cursor (return true). Overlap still re-sees recent dibits next hop.
  2. Do not soften DEC-0012. Keep DEC-0035 eye-lost replay caps.
- **Consequences:** Unknown-grant starts must keep extracting after the first
  eye instead of holding silent. Mid-call small gaps (ctxDrop / feedRatio~0.3
  from 280 ms overlap) remain a separate tighten if still audible.

---

## DEC-0035 — Live eye-lost re-lock uses replay CQPSK caps

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live CADENCE re-prove pending)
- **Evidence:**
  - Operator: live still one-emit cliff; file/GUI replay almost continuous.
  - Capture `20260909_094846` on DEC-0034 exe: live CADENCE drop **A=60**/62,
    emit>0 **5**/62, max duty **0.338**. TG 30302 @ 417.675: brief VCW →
    emit → permanent `p2bursts=0` while hops stay 80+280.
  - **Same IQ** CLI voicetest TG 30302 skip=1300 center=417.66375 slot 0:
    `PASS_PARTIAL_AUDIO duty=0.43` with `targetVcw=652` / `p2bursts=711`
    (RF has continuous clear; live extract starved).
  - Live GUI worker after speak: hot **cand=8 / 120 ms**. CLI voicetest +
    GUI IQ-replay hot path: **cand=16 / 240 ms** (`kP25ReplayHot*`).
  - DEC-0034 (keep block CQPSK hint) did not change 094846 — wrong lever for
    this live/replay split.
- **Decision:**
  1. On live block-channelize `hotPhase2TrafficJob` after speak: if the
     previous hop has no Phase-2 eye (`p2bursts=0` and no target VCW/mask),
     use **replay** CQPSK caps (16/240). If the eye is present, keep
     DEC-0019 cand=8/120 (060221 worker-busy).
  2. Do not soften DEC-0012. Do not invent hop/TTL. Do not default-on
     streaming.
- **Consequences:** Reopen GUI on new exe. T-0010 prove = live CADENCE must
  keep extracting after first emit on 094846-class follows (not only file).

---

## DEC-0034 — Post-emit empty eye must keep block CQPSK hint

- **Date:** 2026-09-09
- **Status:** accepted (implemented; live re-prove pending)
- **Evidence:**
  - Capture `20260909_092250` on DEC-0033 exe (~139 s): emit>0 **9**/131;
    drop **A=116** / B=11 / D=4; max dutySec **0.416**; fresh **80+280** held.
  - TG **12014** slot1: ~10 s of strong extract (`targetVcw` 12–72) with
    `fed=0` (dual-slot / ESS unknown gates), then one `gate=emit`, then
    immediate permanent `p2bursts=0` / drop A while hops stay 80+280.
  - DEC-0032 post-emit empty-eye streak≥2 called `clearBlockCqpskHint()`.
    Block-channelize has no sticky Costas — the hint is the only carrier
    continuity across hops. Clearing it after the first speak matches the
    one-emit-then-silence operator report.
  - DEC-0033 companion-only sticky fallthrough was **not** streaming-gated
    and could thrash dual-slot sticky epochs on the default path.
  - File bar after fix: 060036 TG 10301 skip=261000 still
    `PASS_CONTINUOUS_AUDIO duty=0.705`.
- **Decision:**
  1. Post-emit empty-eye soft rehunt keeps `ForceMaskEpochRehunt` only —
     do **not** call `clearBlockCqpskHint()`.
  2. Companion-only sticky fallthrough stays behind
     `enableStreamingChannelDdc` (DEC-0033 streaming path only).
  3. Do not soften DEC-0012. Do not invent hop/TTL/PLC. Default-on
     streaming still gated (DEC-0033).
- **Consequences:** Reopen GUI on new `SDR_Town.exe`. T-0010 live CADENCE
  must not cliff to all-A after first emit while RF continues (092250 class).

---

## DEC-0033 — Sticky HDQPSK: use persistent framer; stop hop CPR

- **Date:** 2026-09-09
- **Status:** accepted (implemented; default-on still gated)
- **Evidence:**
  - Capture `20260909_083254` (DEC-0032 exe): post-emit hops stay **80+280**;
    rolling **4 s**; CADENCE drop **A=147**/152, emit>0 **5**/152. First TG
    30302 emit then ~0.6 s `no voice sync`. ForceMask never logged. Dominant
    failure is extract after emit, not hop geometry.
  - SDRTrunk `P25P2DecoderHDQPSK.receive`: continuous DDC → sticky Costas +
    Gardner → `P25P2MessageFramer` for channel life. OP25 CQPSK: sticky FLL/
    Gardner/Costas; hop resets frame sync only, not Costas.
  - Our default block path clears Costas every hop (`P25LiveDecoder.cpp`
    channelize). Streaming DDC (`SDR_TOWN_P25_STREAMING_DDC=1`) keeps Costas
    but still failed 105622 duty ~0.09–0.16 (DEC-0014/0018): after first emit,
    wrong-slot / `p2bursts=0` alternation.
  - Root in tree: `P25Phase2Framer` is fed and `m_pendingFramerBursts` filled,
    but commit only consumed them when `demodState >= TrackingSoft`. Carrier
    confidence only rose on `cqpskCarrierLoopApplied`, so state often stayed
    **Cold** → pending bursts cleared → sticky lattice walk early-returned on
    companion-slot bursts. Always-queue without anchor / empty annotate /
    clearing anchor on one misaligned burst regressed 060036 env=1 to duty
    **0.02**; those holes are fixed below.
  - **Measured after fix (060036 TG 10301 skip=261000 center=421.96375):**
    block 80+280 `PASS_CONTINUOUS duty=0.705` (held). env=1 stream
    `PASS_PARTIAL duty=0.25` (was ~0.09–0.16 class on 105622; no wrong-slot
    islands). 105622 IQ not on disk. Default-on still rejected.
- **Decision:**
  1. Stop hop/TTL/grace/PLC CPR for ISS-0001 continuous clear. Do not soften
     DEC-0012. Do not default-on streaming until env=1 hits duty ≥0.65 on a
     continuous-clear file bar + live CADENCE.
  2. Streaming DDC queues persistent-framer bursts for commit; commit consumes
     them only when a superframe anchor exists. Annotate with source dibits.
     Misaligned framer bursts skip, do not wipe the anchor. Carrier/timing FSM
     notes structure under streaming.
  3. Sticky lattice early-return must not stick on opposite-slot-only VCWs
     when preferred TDMA slot is known — fall through to re-lock (Costas stays).
  4. GUI IQ-replay must not prepend overlap context when streaming DDC is on.
  5. Voicetest prints cqpskLock / residHz / framerBurst / demodState when
     stream contiguous DDC is on.
- **Consequences:** Block 80+280 remains shipped default. Env=1 improved but
  still partial (0.25 on 060036). T-0010 stays open until live prove.

---

## DEC-0032 — Post-emit keep DEC-0009 80+280; soft empty-eye rehunt (revert DEC-0031 planner)

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_081701` (DEC-0031 exe, ~264 s, SNR ~8.1 dB). Operator:
    single little emit at follow start then hang on `no voice sync`.
  - Live CADENCE: drop **A** 234 / D 4 / B 3; emit>0 only **7**/241 s.
  - TG **10120** @ 421.975: cold eye emit=12 duty 0.239, then immediately
    worker hops `fresh=245760` (**120 ms** backlog catch-up) with
    `diag=no voice sync` for tens of seconds. DSP median ~70 ms.
  - Root: DEC-0031 planned backlogCatchUp **before** speaker-sustain. Cold
    emit dsp~428 ms left backlog → next hop left DEC-0009 80+280 → eye death.
  - Second hole: every post-emit empty eye raised `MaskEpochRepairHoldWindows`,
    which makes the planner skip speaker-sustain (steal geometry).
- **Decision:**
  1. Restore speaker-sustain / active-clear **before** backlogCatchUp
     (DEC-0009 80+280 after speak). Catch-up 120+280 only off that path.
  2. Post-emit empty-eye streak≥2: `ForceMaskEpochRehunt` +
     `clearBlockCqpskHint()` — do **not** set MaskEpochRepairHoldWindows.
  3. Keep DEC-0031 once-clear `requireFedAudio=false` continuation.
  4. Do not reopen post-emit cold 240/64, streaming default-on, or DEC-0012.
- **Consequences:** Live prove = after first emit, hops stay fresh=80 ms
  context=280 ms and CADENCE must not cliff to all-A `no voice sync` while
  talk continues (T-0010 / 081701).

---

## DEC-0031 — Once-clear: backlog catch-up before speaker-sustain; continuation without fed chicken-egg

- **Date:** 2026-09-09
- **Status:** accepted (planner part **superseded by DEC-0032**; once-clear continuation kept)
- **Evidence:**
  - Capture `20260909_062006` (DEC-0030 exe, ~46 s, SNR ~13.8 dB). LO correct
    (`rfCenter=421.96375` = voice 421.975 − 11.2 kHz). Rolling reaches
    **8192000** (4 s) — DEC-0030 held.
  - Live CADENCE: drop **A** 41 / **D** 1; one emit island duty **0.139**; then
    `no voice sync` with windows still running. DSP median ~**70 ms** on 80 ms
    fresh (drain OK; absStart “jumps” were rate-limited log artifacts).
  - File voicetest same IQ TG **30003** slot 1 center 421.96375: 
    `PASS_PARTIAL_AUDIO drop=D duty=0.46`. Emit islands alternate with
    `unknown-waiting-clear` / companion-louder (`oppVcw > targetVcw`, fed=0)
    — DEC-0012 isolation class, not missing RF.
  - Planner: `p25Phase2PlanVoiceDecodeChunk` early-returned DEC-0009 80+280
    whenever `activeSpeakerClearPath`, so DEC-0024 backlogCatchUp (120+280)
    was unreachable after speak.
  - Security gate called `SameCallSelectedTimeslotContinuationSafe(...,
    requireFedAudio=true)` — chicken-egg vs dual-slot mute clearing audio
    before continuation can keep trustedClear (DEC-0003 once-clear).
- **Decision:**
  1. Plan backlogCatchUp **before** speaker-sustain early-return (still
     DEC-0024 120 ms fresh / 280 ms overlap; no 180 ms live-edge catch-up).
     **Superseded by DEC-0032** (caused 081701 post-emit eye death).
  2. Once latch Clear / hadSuccessfulEmit / callHadSpeakerAudio, evaluate
     continuation with `requireFedAudio=false` so dual-slot untrusted does
     not collapse once-clear into `unknown-waiting-clear`. **Kept.**
  3. Do **not** soften `PostEmitMixedMacDead` (DEC-0012 / 041716). Do not
     reopen streaming default-on, hot cand=3, or post-emit cold.
- **Consequences:** Re-measure 062006 file duty (expect ≥0.46, hope toward
  0.65 if continuation recovers selected-dominant mixed hops). Live prove =
  after first emit, CADENCE must keep extracting when RF has voice (T-0010).
  Live eye vs file extract gap on 062006 remains open if still drop A.

---

## DEC-0001 — Copy Athanor method, not Athanor product law

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Operator asked to apply SovereignFoundry / Athanor rules and
  methodology to `maulaudio_pro` and not to edit Athanor. SDR Town already
  depends on Qt 6, SoapySDR, mbelib, miniaudio, spdlog, nlohmann/json.
- **Decision:** Binding process is `DEVELOPMENT_RULES.md` (never guess,
  trackers, comments cite evidence, definition of done). Product law is
  `SOURCE_OF_TRUTH.md` for this receiver. Zero-external-library / Knox /
  ML-KEM rules stay in Athanor only.
- **Consequences:** New third-party deps still need a DEC. Existing deps stay.

---

## DEC-0002 — Classify every Phase 2 audio hole as A–E before changing gates

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** `docs/p25_phase2_regression_tracker.md` and README v0.2.50
  show months of contradictory hotfixes (open security → garble; close →
  silence; overlap decode → dup starve; bridge → fake continuity).
  CADENCE already counts `targetVcw`, `fed`, `emit`, `dups`, `dutySec`.
  Voicetest already defines continuous as duty ≥ 0.65 (`src/main.cpp`).
  Late-entry strong selected VCW bar already exists as 8 codewords
  (`kP25Phase2LateEntryStrongTargetVoiceCodewords`).
- **Decision:** Dominant drop bucket is computed by `classifyP25AudioDrop`
  (earliest pipeline stage wins):

  | Bucket | Label | Rule (scaled by `windowSeconds`) |
  |---|---|---|
  | E | follow | `followReturned` |
  | A | extract | unique selected VCW (`max(0, targetVcw − dups)`) below 8 per second, and emit below the continuous bar |
  | B | feed | unique ≥ 8/s and `fed == 0` |
  | C | emit | `fed > 0` and `emittedPcm == 0` |
  | D | playout | `emittedPcm > 0` and duty (`emittedPcm * 0.020 / windowSeconds`) < 0.65 |
  | ok | none | unique ≥ 8/s, fed>0, duty ≥ 0.65. Idle all-zeros is A, not ok. |

  CADENCE (1 s) and `p25 voicetest` print `drop=`. No timeout/gate/hop
  change until ISS-0001 cites a run.
- **Consequences:** T-0003…T-0007 stay blocked until a capture names a bucket.
  8/s cites the existing late-entry constant, not a new magic number.
  0.65 cites voicetest `continuousOk`.

---

## DEC-0003 — Selected-slot speaker policy follows SDRTrunk P25P2AudioModule

- **Date:** 2026-09-07
- **Status:** accepted (policy; code changes wait for measured bucket B or C)
- **Evidence:** SDRTrunk: one AudioModule per timeslot; queue Voice2/4 until
  PTT/ESS establishes clear/enc once; play that slot until squelch; never mix
  opposite slot. Our dual-slot MAC-dead mute (beeaea8 / 034136) stopped
  wrong-epoch garble and also punched holes in already-proven selected-slot
  audio (004206 duty drop). Sticky ESS alone is not epoch proof (034136).
- **Decision:** When we touch feed/emit (REQ-P2.2 / P2.3):
  1. Companion slot never feeds the speaker mbelib instance.
  2. Selected slot: after **this slot** has clear proof (this-window MAC CRC
     or this-window ESS clear on that slot, or MAC_PTT/ACTIVE clear), keep
     feeding descrambled Voice2/4 until END/HANG/squelch/encrypted.
  3. Dual-slot MAC-dead mutes **unproven** frames and pending drain. It does
     not mute selected-slot frames that already have this-slot proof for the
     current call key.
  4. Sticky grant-clear / latch without this-slot proof must not open dual-slot
     MAC-dead windows (034136 stays forbidden).
- **Consequences:** No feed-gate patch until REQ-P2.0. Isolation (SoT L1–L2, L7)
  outranks duty.

---

## DEC-0004 — PASS_PARTIAL_AUDIO is not done

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Voicetest distinguishes `PASS_PARTIAL_AUDIO` vs
  `PASS_CONTINUOUS_AUDIO`. Roadmap had marked continuous “done”. Field ~50%.
- **Decision:** SoT L3. Roadmap item 1 is open. String verifiers are not the
  gate.
- **Consequences:** ISS-0005 closed by retracting the roadmap claim.

---

## DEC-0005 — Do not invent PLC to hide missing Voice2/4

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** SoT L5. Prior silence-bridge work was an underrun guard
  (`p25Phase2PlayoutBridgeAllowed`, max ~4.5 s). Users heard blocky/partial
  speech, not a missing 20 ms click.
- **Decision:** No dummy speech PCM. Bridge may only fill true ring underrun
  on an already-open clear call. Concealment stays mbelib repeat/erasure
  already counted in voicetest (`concealmentOk` ≤ 25%).
- **Consequences:** REQ-P2.4 must raise unique emit rate, not lengthen bridge.

---

## DEC-0006 — Overlapping traffic eyes rewind the dibit stream cursor

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** `alignPhase2AbsoluteDibitCursor` comment already required the
  cursor to be the start of the current chunk. Contiguous and gap/rewind
  paths did that; overlapping lookback (chunkStart < previous end < chunkEnd)
  did not. Voicetest 20260905_105622 TG 30003 slot 0 skip=97334: hop 1
  0–720 ms then hop 2 160–880 ms then 80 ms sustain eyes with 80 ms context.
  Cursor stayed at the previous end; sticky lattice walk decoded 80–560 ms
  off the bursts. Hop lines: p2sf/p2mask high with p2vcw=0 or
  `wrong TDMA slot` islands; `PASS_PARTIAL_AUDIO drop=D duty=0.28`
  (ISS-0001, REQ-P2.1).
- **Decision:** If the new chunk overlaps the previous stream, set
  `m_phase2StreamDibits = chunkStartAbsolute` without clearing sticky
  epoch/tail. True ring-reset rewind (whole chunk behind the cursor by more
  than one burst) still clears. Sticky walk covers every complete timeslot
  in the working window (no 540 ms cap on a 720 ms cold eye).
- **Consequences:** Overlap Voice2/4 is de-duped by existing abs-dibit
  session IDs, not by leaving the cursor past the IQ. Not a hop/TTL tweak.

---

## DEC-0007 — Context IQ must not discard never-emitted selected Voice2/4

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** After DEC-0006, voicetest 105622 slot 0 duty fell 0.28→0.125.
  Hop startMs=98294: `targetVcw=2 ctxDrop=2 fed=0 dup=0` — unique selected
  frames a prior 80 ms hop missed (`p2vcw=0`) were hard-dropped because they
  sat before `freshStart − 240 dibits`. Abs-dibit `ShouldEmit` never ran.
- **Decision:** Pre-fresh Voice2/4 may be suppressed only when
  `p25Phase2ShouldEmitAmbeFrame` says they were already emitted (or 12-dibit
  start match). Do not hard-continue on `codewordIsContextOnly`.
- **Consequences:** Overlap still cannot replay (ShouldEmit / lastAbs). Missed
  RF in the lookback can reach mbelib after the call is proven clear.

---

## DEC-0008 — Sticky superframe lattice is contiguous-dibit only

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** Voicetest 20260905_105622 TG 30003 slot 0 skip=97334 8 s
  (HEAD with DEC-0006/0007): hop 2 (720 ms block eye) `targetVcw=24 fed=30`;
  hop 3+ 160 ms sustain `p2sf/p2mask` with `p2vcw=0` or
  `Phase 2 wrong TDMA slot`; `PASS_PARTIAL_AUDIO drop=D duty=0.28`.
  Block-channelize clears CQPSK/Gardner/framer every hop
  (`20260729_114627`) then the sticky stream-space walk / anchor-aligned
  lock reused the previous eye's dibit lattice. Slot labels and Voice2/4
  DUIDs followed that stale grid. Streaming DDC keeps a real contiguous
  dibit stream; that path may keep the lattice.
- **Decision:** `findPhase2AnchorAlignedSuperframeLocks`, the hot sticky
  burst walk (early return), and the continuous sticky walk run only when
  `enableStreamingChannelDdc` is true. Block-channelize hops re-lock the
  superframe on this window's syncs. Sticky XOR mask phase stays.
- **Consequences:** Default CLI/GUI traffic (streaming DDC opt-in) extracts
  from each independent eye. Not a hop/TTL/minFresh change. Isolation
  unchanged (grant slot still authoritative).

---

## DEC-0009 — Locked block-channelize sustain eye is one superframe

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** After DEC-0008, voicetest 105622 slot 0 skip=97334 8 s duty
  0.40; 5 s talking slice duty 0.51 (`fed=128`). Sustain hops used 80 ms
  overlap → 160 ms eyes; many `p2bursts=1`. Hop 2 (720 ms) still recovered
  24 selected VCWs. A Phase 2 superframe is 12 × 30 ms = 360 ms (SDRTrunk
  SuperFrameFragment). Hop/minFresh stay 80/40 ms (not a TTL tweak).
- **Decision:** `kP25Phase2VoiceDecodeSustainOverlapSeconds` and
  `kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds` are 0.280 so
  overlap+fresh = 360 ms. Streaming DDC overlap remains 0.
- **Consequences:** More IQ per live tick than 160 ms eyes; still half the
  720 ms cold eye. Abs-dibit de-dupe still owns overlap.

---

## DEC-0010 — Deep overlap is lock-only after first emit

- **Date:** 2026-09-07
- **Status:** superseded by DEC-0013 (105622 duty 0.735→0.35; missed-eye catch-up needed)
- **Evidence:** Live capture `20260907_073304` (146 s gapless IQ). Operator:
  some audio better, still garbled + repeats.
  Repeats: TG 10330 slot 1 seq=386 emit=18 then seq=389 ctxVcw=10 ctxDrop=0
  (200 ms already-played overlap pushed again). CADENCE 17:34:54–58:
  dups=78–100/s, feedRatio=0.29, dutySec=0.72–0.92. Independent CQPSK eyes
  (DEC-0008) shift recovered abs by more than the 12-dibit ShouldEmit wobble,
  so DEC-0007's "context is playable if not an abs duplicate" replays speech.
  Tried also muting mixed MAC-dead continuation (TG 30302 seq=9 opp=16
  p2mac=0/2 gate=emit): 105622 voicetest duty 0.735→0.35. Reverted that
  half (T-0004 stays open).
- **Decision:** After `lastAbs != 0` or `hadSuccessfulEmit`, Voice2/4 that end
  more than one sustain hop (80 ms / 480 dibits) before `freshStart` are
  lock-only. The last hop of lookback still uses ShouldEmit (DEC-0007
  missed-eye). Dual-slot continuation escape is unchanged.
- **Consequences:** Overlap stays 280 ms for superframe lock (DEC-0009). Not a
  hop/TTL change. Mixed MAC-dead garble remains T-0004 / DEC-0012. Last-hop
  80 ms leak on independent eyes is DEC-0011.

---

## DEC-0011 — Block-channelize post-emit context is all lock-only

- **Date:** 2026-09-07
- **Status:** superseded by DEC-0013 (105622 duty 0.735→0.11 with DEC-0011)
- **Evidence:** Capture `20260907_073304` TG 10330 slot 1 after DEC-0010's
  480-dibit floor: seq=401 `ctxVcw=4 ctxDrop=0` (exactly one 80 ms hop of
  lookback still replayed). Independent CQPSK eyes shift recovered abs past
  the 12-dibit ShouldEmit wobble, so those four frames are treated as new.
  Same capture later TG 30302 CADENCE `dutySec=1.28–1.36` with `dups=0`.
- **Decision:** When streaming DDC is off (default live/voicetest block
  eyes), after `lastAbs != 0` or `hadSuccessfulEmit`, every Voice2/4 that
  ends before `freshStart` is lock-only. Streaming DDC keeps the last-hop
  ShouldEmit catch-up (contiguous abs, DEC-0007).
- **Consequences:** Overlap remains 280 ms for lock/MAC/ESS. Not a hop/TTL
  change. First-emit missed-eye catch-up unchanged (`lastAbs == 0`).

---

## DEC-0012 — Post-emit mixed MAC-dead mutes feed, not the hop PCM

- **Date:** 2026-09-07
- **Status:** accepted (feed-only; 2026-09-08)
- **Evidence:** Capture `20260907_073304` TG 30302 seq=9 `oppVcw=16 p2mac=0/2`
  `gate=emit` `action=explicit-clear-grant-traffic-clear-release` after a
  clean seq=1 `opp=0 p2mac=11/13`. Capture `20260908_041716` TG 10330 slot 1
  seq=131 `opp=0 p2mac=5/6` `trusted-clear-pending-release` then seq=134
  `targetVcw=6 oppVcw=12 fed=4 emit=4 p2mac=0/0 ess=clear`
  `explicit-clear-grant-traffic-clear-release`, then `Phase 2 wrong TDMA slot`.
  Operator: one good emit, then garbled wrong-slot audio. Call 1 on the same
  file already mixed-emitted (seq=27/36/47 `opp=8 p2mac=0/x ess=clear`).
  `DualSlotUntrustedGarbleWindow` stayed false because this-window ESS clear
  + companion accounted + strong selected (overlap Voice ESS / 004206 escape).
  Muting via that helper + `audio.clear()` dropped 105622 duty 0.735→0.38
  because it discarded already-proven PCM in the same hop.
- **Decision:** After the call has already spoken (`hadSuccessfulEmit` or
  sticky `p25Phase2CallHadSpeakerAudio`), if both slots have voice, this
  window has no selected MAC CRC, **and the companion is louder**
  (`oppVcw > targetVcw`), do not feed bursts that themselves lack MAC CRC.
  Same-call continuation and this-window ESS clear must not escape that
  skip. Do not brand the hop as `dual-slot-untrusted-garble-drop` and do
  not `audio.clear()` proven PCM. Selected-dominant mixed ESS
  (`targetVcw >= oppVcw`) still feeds (105622 startMs=99134 target=18
  opp=8; 004206). This-window selected MAC CRC still authorizes
  companion-louder hops (105622 startMs=100254 slot0Mac=2). Selected-only
  MAC-dead (`oppVcw=0`) still feeds.
- **Consequences:** Blanket mixed-MAC-dead mute after emit dropped 105622
  duty 0.685→0.27 (too many selected-dominant ESS hops). Companion-louder
  is the 041716 seq=134 / wrong-slot pattern. T-0004 first-hop
  `target=6 opp=28 fed=0` stays a separate cold-eye case. Equal mixed
  (041716 call 1 seq=27 target=8 opp=8) is still ESS-authorized.

---

## DEC-0013 — Overlap replay uses ISCH lattice identity, not lock-only

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:** DEC-0010/0011 lock-only after emit dropped 105622 TG 30003
  slot 0 skip=97334 duty 0.735→0.35→0.11 (missed-eye catch-up, DEC-0007).
  Live 073304 TG 10330 seq=389/401 still replayed overlap because recovered
  abs moved more than the 12-dibit ShouldEmit wobble (DEC-0008 independent
  eyes). `Phase2VoiceFrameKey` equality includes that jittered abs / window
  local superframe anchor, so sequencer recentKeys also miss.
- **Decision:** After a selected-slot Voice2/4 is actually fed, remember
  `(slot, superframeBurstIndex, voiceIndex)` plus recovered abs. A later hop
  with the same lattice identity inside 1800 dibits (~300 ms, overlap 280 ms,
  superframe 2160 dibits) is a replay and is not fed. Next superframe keeps
  the index but is ≥2160 dibits later. Context Voice2/4 that were never
  emitted still use ShouldEmit only (DEC-0007). Mixed MAC-dead feed mute
  (DEC-0012) is feed-only after the call has spoken; do not hop-PCM clear.
- **Consequences:** Not a hop/TTL/overlap change. Requires ISCH superframe
  lock (`burstIndex < 12`); unlocked bursts still use 12-dibit abs.

---

## DEC-0014 — Independent Phase 2 traffic is SDRTrunk HDQPSK.receive

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - SDRTrunk `P25P2DecoderHDQPSK.receive(ComplexSamples)`: one Costas +
    Gardner + MessageFramer on a contiguous channelized stream; `main()`
    consumes 2048 samples at 25 kHz ≈ 81.92 ms. Two `P25P2AudioModule`s,
    `if (message.getTimeslot() == getTimeslot())`; queue Voice2/4 until
    PTT/ESS; PTT `clearPendingVoiceTimeslots()`; then 20 ms frames as they
    arrive. No overlapping independent CQPSK eyes.
  - Live capture `20260907_095450`: dual grant TG 30013 slot 1 + TG 30003
    slot 0 on 420.225 MHz. First eye `iq=1474560` (720 ms) `dsp=327 ms`
    `targetVcw=4 opp=0 p2mac=1/13`; seq=2 `decode-wall-timeout`. Later hops
    `fresh=80 ms context=160–280 ms` `dsp=159–573 ms` (wall 320 ms). seq=21
    `targetVcw=10 oppVcw=10 fed=10 emit=10 ctxDrop=0 p2mac=0/2` (200 ms PCM
    on an 80 ms hop = overlap replay + mixed-slot feed). First CADENCE
    `windows=593 dutySec=2.320` at 19:55:47 — 53 s of hops dumped as `1s`
    because `lastLogMs==0`.
  - Capture `20260830_064319` / `20260829`: auto streaming DDC into
    **20–40 ms** context-free slices on an unlocked eye → `p2bursts=0`.
    That was slice length, not the DDC. Cold eye stays 720 ms (DEC-0009).
- **Decision:**
  1. **Tried** independent realtime Phase 2 traffic enabling streaming DDC
     by default (SDRTrunk `receive`). Voicetest 105622 TG 30003 slot 0
     skip=97334 8 s fell `PASS_CONTINUOUS_AUDIO duty=0.685` →
     `PASS_PARTIAL_AUDIO drop=A duty=0.095` (`emptyWindows=80/89`,
     `targetVcw=56` vs 396). Same eye-loss as 20260830, not slice length
     alone — the first hop was already 720 ms. **Default stays
     block-channelize.** `SDR_TOWN_P25_STREAMING_DDC=1` still opts in.
     Do not hand off via `stickyReady`.
  2. When streaming DDC **is** on, locked hops are **80 ms** fresh-only
     (`overlap=0`), not 40 ms. Cite SDRTrunk 2048@25 kHz and 20260830.
  3. Hard reacquire `reset()`s Costas/DDC/framer. It must not flip an
     operator-opted-in stream back to block-channelize (`env=1` keeps DDC).
  4. CADENCE `1s` starts its bucket on the first diagnostic tick
     (`lastLogMs==0` discards, does not print) and uses elapsed time as
     `windowSeconds`. Capture 095450 `windows=593 dutySec=2.320` was a
     53 s backlog labeled as one second. Pending AMBE stays PTT-clear /
     ESS-drain (DEC-0003).
  5. DEC-0012 mixed MAC-dead is feed-only after emit (041716 seq=134).
     Slot bleed on 095450 (`targetVcw≈oppVcw`, `p2mac=0/x`, `ctxDrop=0`)
     remains independent-eye ISCH + overlap on the first hop (T-0004).
- **Consequences:** Live/voicetest default is still 80/40/280 block
  eyes (DEC-0009). CADENCE is honest. Streaming DDC remains the SDRTrunk
  stream experiment, not the proven continuous path on 105622.

---

## DEC-0015 — Tuner center is SDRTrunk CenterFrequencyCalculator

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - Operator: some follows look off-center.
  - Our Phase 2 follow parked `voice ± 250 kHz`
    (`kP25Phase2TrafficLowIfOffsetHz`) and reused a CC-centered tuner
    whenever `|voice−center| ≤ min(0.42*sr, max(250 kHz, 0.25*sr))`
    (~512 kHz at 2.048 Msps). Capture 095450 (CC 420.475, voice 420.225)
    therefore kept the tuner on the control channel: the grant sat 250 kHz
    off spectrum middle, and the CC channel overlapped the LO/DC spike.
    The 250 kHz offset was a local note (419.375 decoded from a 419.125
    ring, poorly when parked exactly on voice). That is “don’t sit on DC”,
    not SDRTrunk’s formula. Comment in `main.cpp` that claimed the 250 kHz
    park “matches SDRTrunk” was wrong.
  - SDRTrunk `CenterFrequencyCalculator.getCenterFrequency`
    (`source/tuner/manager/CenterFrequencyCalculator.java`), called from
    `HeterodyneChannelSourceManager.updateTunerFrequency` when
    `!tunerController.isTunedFor(channels)`:
    - One channel: `minFrequency − middleUnusableHalfBandwidth + 1`.
    - Two+ channels with span `≤ usableHalfBandwidth`:
      `minChannelFrequency − middleUnusableHalfBandwidth`.
    - `TunerChannel.getMinFrequency()` = `frequency − bandwidth/2`.
  - `DecodeConfigP25Phase2.getChannelSpecification`:
    `ChannelSpecification(50000.0, 12500, 6500.0, 7200.0)` → channel
    bandwidth **12500 Hz**.
  - R820T/R820T2 (typical RTL-SDR): `R8xEmbeddedTuner`
    `DC_SPIKE_AVOID_BUFFER = 5000`, `USABLE_BANDWIDTH_PERCENT = 0.98`.
    One traffic follow: `center = voice − 6250 − 5000 + 1` =
    **voice − 11249 Hz**. Wiki: do not park a decode channel on DC or on
    filter roll-off; recompute tuner center from the sourced set.
  - `TunerController.isTunedFor`: every channel inside
    `center ± usableBandwidth/2`, and none overlapping
    `[center − dcHalf, center + dcHalf]`.
- **Decision:**
  1. Replace the 250 kHz low-IF helper with SDRTrunk’s calculator
     (`include/P25SdrtrunkTune.h`). Keep the name
     `p25Phase2LowIfTrafficCenterHz` as a wrapper so existing string
     locks still see it.
  2. **Traffic-only** (CC paused, dedicated SDR, CLI waitgrant):
     single-channel center = `voiceMin − 5000 + 1`.
  3. **CC still live on the same tuner:** compute center for
     `{ccHz, voiceHz}` and **nudge** the tuner if `!isTunedFor`. Do not
     leave CC sitting on DC while DDC’ing traffic 250 kHz away. Do not
     pause CC for that nudge (`retunesPrimary` stays false).
  4. Reuse vs physical retune uses `canTune` (span ≤ usable bandwidth
     and a valid center exists), not the 250 kHz / 0.25*Nyquist quality
     clamp. Capture 005246 (750 kHz from a CC-at-DC tuner) was that
     geometry; after this calculator the lower channel sits just right
     of DC.
  5. No hop/TTL/feed-gate change. Do not re-enable default streaming DDC
     (DEC-0014).
- **Consequences:** Waterfall follows sit ~11 kHz right of center, not
  250 kHz off. File voicetest of 105622/073304 still uses capture
  geometry (`--center`), not this live LO. 095450 live should log
  `rfCenter≈420.21375` for CC 420.475 + voice 420.225 if both stay
  sourced. Isolation on 095450 dual-TG remains T-0004.
- **Superseded in part by DEC-0016:** two-channel `getCenterFrequency`
  on `{cc, voice}` is not the follow LO. It parks the *lower* channel
  off DC, which put 421.975 MHz at 997 kHz offset on capture 115315.

---

## DEC-0016 — Follow LO parks the granted voice, not the CC+voice set

- **Date:** 2026-09-07
- **Status:** accepted
- **Evidence:**
  - Capture `20260907_115315` (162 s gapless, SNR ~17 dB). Operator:
    audio really bad. DEC-0015 two-channel calculator:
    - TG 30003 420.725 + CC 420.475 → `rfCenter=420.46375` **offset=261.3 kHz**
      (CC is the lower channel, so CC sits off DC and traffic is 261 kHz up).
    - TG 30003 421.975 + CC 420.475 → span 1.51 MHz ≤ usable 2.007 MHz so
      `canTune` was true. Set calculator then places highest at the high
      end: `rfCenter=420.97773` **offset=997.3 kHz**. Voice channel max
      equals `center + usableHalf`. SDRTrunk wiki: filter roll-off at
      spectral edges is unusable for decode. Capture 005246: 750 kHz
      offset left `p2vcw≈0`.
    - CADENCE after that hop: a few seconds `drop=D dutySec=0.22–0.45`,
      then `drop=A` `reject` high / `no-sf-mask` / `no-vcw-from-live-window`
      for the rest of TG 30003 and almost all of TG 30302 at 421.225
      (reused the 420.97773 LO, `kind=existing-wideband-source-low-if`).
  - SDRTrunk `CenterFrequencyCalculator` optimizes the *set* because
    every sourced channel is a first-class decode. This GUI has one
    selected speaker follow. The channel that must sit just right of
    the R820T DC hole is the **granted voice** (single-channel formula,
    DEC-0015). CC stays only if that same LO still `isTunedFor` CC.
- **Decision:**
  1. `p25SdrtrunkTunerCenterHz(voice)` is always the single-channel
     park (`voiceMin − 5000 + 1`). Do not pass CC into
     `getCenterFrequency` for the follow LO.
  2. Keep CC on the same tuner only when
     `isTunedFor(voiceParkCenter, {voice, cc})`. Otherwise one-RTL
     physical retune (pause CC) to that same voice park.
  3. Same-call hops use the voice park, not `{cc, voice}`.
  4. Two-channel `getCenterFrequency` stays in the header as the cited
     SDRTrunk routine; it is not the follow LO. 250 kHz stays gone.
  5. No hop/TTL/feed-gate change. Streaming DDC stays default-off.
- **Consequences:** 115315’s 421.975 follow would be `rfCenter≈421.96375`
  (offset ~11 kHz) with CC paused. 095450 voice 420.225 is still
  `≈420.21375` and CC still fits. File voicetest `--center` unchanged.

---

## DEC-0017 — Locked block-channelize hop is one superframe of fresh IQ

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - Capture `20260907_123525` live: 80+280 ms jobs (360 ms IQ / 80 ms hop
    = 4.5× CQPSK), `dsp=72–134 ms`, `worker-busy`, CADENCE duty **0.34**.
    File voicetest skip=20000: duty **0.64**.
  - Tried `chunk=minFresh=0.360 overlap=0`. Voicetest 105622 skip=97334
    `PASS_CONTINUOUS_AUDIO duty=0.685` → `PASS_PARTIAL_AUDIO drop=D
    duty=0.35`. 123525 skip=20000 duty 0.64→0.325. Independent 360 ms
    eyes lose DEC-0007 overlap catch-up.
  - Streaming DDC still fails 105622 duty 0.095 (DEC-0014 re-measured).
- **Decision:** Do not ship 360 ms fresh-only block hops. Restore
  DEC-0009 80+280. Next live hole is worker-busy skipping IQ, not hop
  size. Isolation (L2) and streaming-DDC-off unchanged until DEC-0018.

---

## DEC-0018 — Streaming DDC must not jump the dibit lattice to RF-sample time

- **Date:** 2026-09-08
- **Status:** accepted
- **Evidence:**
  - DEC-0014: `SDR_TOWN_P25_STREAMING_DDC=1` on 105622 skip=97334
    `PASS_PARTIAL_AUDIO drop=A duty=0.095 emptyWindows=80/89`. First hops
    extracted; later hops `p2bursts=0`. Locked hops were already 80 ms
    overlap 0 (not the 20260830 40 ms slice).
  - `decodeP25VoiceAudioBlock` maps `iqStartAbsolute * 6000 / inputSr` and
    calls `alignPhase2AbsoluteDibitCursor` before `processIq`. Streaming
    DDC FIR/resampler output lags that RF-time prediction. The old
    streaming branch treated a small forward gap as harmless and **set
    `m_phase2StreamDibits = chunkStartAbsolute`**, jumping the sticky
    HDQPSK lattice off the actual dibits. Block-channelize overlap still
    needs that rewind (DEC-0006).
  - DEC-0017 360/0 independent block eyes failed 105622 duty 0.35: overlap
    CQPSK is required unless the Costas/framer stream is truly contiguous.
- **Decision:**
  1. When streaming DDC is on, do not align the dibit cursor from input-IQ
     sample indices. Contiguous hops append actual DDC/CQPSK dibits.
  2. If align is still called, a forward gap ≤ 8 bursts must **not** jump
     the cursor; leave the actual dibit count. Larger gaps still reset
     (retune / dropped RF).
  3. Re-measured 2026-09-08 after this skip: 105622 env=1 still
     `PASS_PARTIAL_AUDIO` (duty 0.16 at 80 ms, 0.09 at hopms=160, 0.045 at
     hopms=360). Default-on stays off (DEC-0014). The jump was real; it was
     not the whole eye-loss. Block 80+280 remains the proven file path.
- **Consequences:** Block 80+280 (DEC-0009) unchanged. Isolation (L2)
  unchanged. Env=1 becomes the experiment to re-measure, not a hop/TTL
  guess.

---

## DEC-0019 — Block CQPSK hint that framed Phase 2 stops the grid

- **Date:** 2026-09-08
- **Status:** accepted (hard early-stop only; cand=3 rejected)
- **Evidence:**
  - SDRTrunk `P25P2DecoderHDQPSK.receive`: one Costas + Gardner stream;
    `main()` feeds 2048@25 kHz ≈ 82 ms. Two `P25P2AudioModule`s filter by
    `message.getTimeslot()` — no companion-louder mute, no per-hop CQPSK
    re-search.
  - Our block-channelize path clears Costas every hop but keeps
    `m_blockCqpskHint`. Capture `20260908_060221`: dsp median **161 ms**
    on 80 ms fresh (14% ≥360 ms), **100** worker-busy, CADENCE talk median
    duty **0.30**, emit islands median length 1. Same class as 053241.
  - File voicetest of 060221 TG 30302 slot 1 skip=128500 center=420.21375:
    `PASS_CONTINUOUS_AUDIO duty=0.922` — IQ has continuous voice; live
    hole is drop **D**, not missing RF.
  - Soft-evidence early-stop trial: 105622 duty **0.645→0.305**, 041716
    **0.87→0.5**. Rejected (20260729 wrong-hint freeze).
  - Hard early-stop + file cand=16: 105622 duty **0.645** (kept).
  - Hot cand=**3** after speak (voicetest mirror of live): wall 17→7 s but
    105622 duty **0.645→0.055** drop=**A**. Same eye-loss class as DEC-0020.
    Cand=3 rejected; live hot stays **8**.
- **Decision:**
  1. After evaluating the block CQPSK hint, if it already produced
     **hard** Phase 2 lock evidence this window, set `stopCqpskSearch`.
  2. Do **not** cap hot CQPSK candidates at 3 after emit.
  3. Do not default-on streaming DDC (DEC-0014). Do not change
     hop/TTL/overlap (DEC-0009). Do not reopen DEC-0012 hop-wide mute.
- **Consequences:** Live drop D still open (T-0010). Shrinking eyes (DEC-0020)
  and cand=3 both buy wall clock by destroying extract. Next measured path
  must keep duty ≥0.65 while cutting per-hop DSP another way, or fix
  streaming DDC eye loss (DEC-0014/0018).

---

## DEC-0021 — Streaming lock-only cand=1 is premature on our Costas

- **Date:** 2026-09-08
- **Status:** rejected (insufficient; eye dies with cand=16 too)
- **Evidence:**
  - env=1 105622: after first emit, cand=1 → wrong-slot / p2bursts=0.
  - Disabling lock-only (always hot cand=16): window 5 still wrong-slot;
    windows 6+ stuck `p2bursts=1 p2vcw=0`. Duty **0.01**. Not cand=1 alone.
  - Transition window 4→5 is also **160 ms → 80 ms** fresh. Windows 2–4 at
    160 ms extracted voice; 80 ms sustain did not.
- **Decision:** Do not treat lock-only disable as a fix. Next trial is
  DEC-0022 (160 ms streaming sustain). Restore prior lock-only code shape
  only if a later DEC needs it; leave disabled while measuring 160 ms.
- **Consequences:** Streaming still not default-on (DEC-0014).

---

## DEC-0030 — Active rolling clamp must honor DEC-0023 4.0 s (not 2.048 s)

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_060036` (~392 s gapless, SNR summary ~6.8 dB, DEC-0029
    exe). Eyes 80+280. Quiet-returns down (DEC-0029 held). Still sparse voice.
  - CADENCE: emit>0 only **10**/215 s; max duty **0.639**; drop **A** 209;
    worker-busy **216**. Total speaker PCM ~**2.24 s**.
  - TG **10301** @ 421.975: cold `context=0` emit=32 duty **0.639**, then
    rolling stuck at exactly **4194304** samples while worker-busy; all later
    hops `p2vcw=0` / drop A / bridge top-ups.
  - **File voicetest** of the same IQ (skip≈261000, slot 0, center 421.96375):
    `PASS_CONTINUOUS_AUDIO duty=0.705` (5.64 s audio / 8 s span). Live silence
    after the first island is starvation, not missing RF.
  - Root: DEC-0023 set `kP25Phase2VoiceDecodeActiveRollingSeconds = 4.000` but
    both GUI/CLI rolling clamps still used `4194304` (= **2.048 s** @ 2.048
    MHz). Soft-trim could not keep the 4 s window DEC-0023 required.
- **Decision:** Route live rolling sizing through
  `p25Phase2VoiceRollingMaxSamples` with hard cap **16 s** (4× active per
  DEC-0023). Eye/emit backlog allowance matches the 4.0 s active window (was
  1.2 s / 4194304). Do not invent hop sizes, reopen streaming default-on, or
  post-emit cold escalate.
- **Consequences:** Live prove = after a cold emit island, rolling may exceed
  4194304 and CADENCE must keep extracting (not cliff to duty 0). Re-check
  105622 when available.

---

## DEC-0029 — Clear-trusted follow hold + structure exits coldAcquire

- **Date:** 2026-09-09
- **Status:** accepted (pending live measure)
- **Evidence:**
  - Capture `20260909_053448` (~308 s gapless, SNR ~17 dB, DEC-0028 exe).
    Eyes OK (0× 40 ms; 1365× 280 ms). **0** ReturnEncrypted.
  - Operator: very little / sparse voice — one small emit every few minutes.
  - CADENCE: emit>0 only **16** of **176** s; **0×** duty≥0.65. Drop **A**
    dominant (162), **D** (12). Block mostly `no-vcw-from-live-window`.
  - Timeline TG **30302** @ 421.225: cold mega-eye finds VCWs; emit island
    (emit=8 duty **0.151**); then empty eyes; at **+5 s**
    `ended or went quiet` releases traffic source while `callClearTrusted=yes`;
    next grant cold-rearms `context=0` / generation++ → another sparse island.
  - Follow SM: after emit, `kSpeakerImmediateGraceMs=2500` required current
    traffic structure; empty eyes dropped speaker grace, then
    `activitySilenceLimitMs=3500` fired ReturnActivityGone. Clear-trusted
    calls need the full 40s speaker grace (field 032428) without a live VCW
    window, plus 15s activity silence (same as
    `kP25Phase2ClearTrustedUnacquiredDwellStealGraceMs`).
  - DSP: emptyEye med ~67 ms (DEC-0028 helped); structureNoVcw med **~484 ms**
    — coldAcquire still required target VCW, so structure-only peaks kept
    burning 240/64.
- **Decision:**
  1. Clear-trusted / traffic-audio-open holds: full 40s speaker grace without
     current VCW/structure; activity silence 15s (not 3.5s).
  2. Exit `coldAcquireJob` once sustain has sf+mask ≥4 (epoch-grade
     structure), not only target VCW / hard acquire.
  3. Same-call metadata commit must not wipe traffic-proven clearKnown on
     unknown OP=0x02 grants (mirror in-place follow).
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = no quiet-return
  within ~15s of clear emit while clearTrusted (T-0010 / 053448 pattern).
  Do not reopen streaming default-on, hot cand=3, or post-emit cold escalate.

---

## DEC-0028 — Never cold-escalate CQPSK after first emit (remove emptyStreakReacq)

- **Date:** 2026-09-08
- **Status:** accepted (pending measure)
- **Evidence:**
  - Capture `20260908_115603` (~487 s gapless, SNR ~17.8 dB, DEC-0027 exe).
    Eyes OK (0× 40 ms). 0 ReturnEncrypted.
  - Operator: first emit perfect (response to missed prior), then rest of
    conversation unheard.
  - Timeline TG **30302** @ 421.225: 21:58:53 emit=28 duty **0.553** (good);
    next workers dsp **605 / 470 / 481 / 472 ms** on structure/wrong-slot;
    then drop **B** (tv=16 fed=0) on companion-louder mixed window (DEC-0012
    isolation — correct). CADENCE peak still **0.599**; 0× ≥0.65.
  - structureNoVcw still med **465 ms** after DEC-0027 — emptyEye streak≥2
    armed cold 240/64 on the *prior* hop; the next structure/voice hop ran
    that cold grid. emptyEye itself is ~102 ms on hot.
  - `emptyStreakReacq` required `hadSuccessfulEmit`, so it **only** fired
    post-emit (pre-emit already uses `coldAcquireJob`). DEC-0026/0027 could
    not make post-emit cold safe.
- **Decision:** Delete post-emit `emptyStreakReacq` cold path. After speak,
  always hot cand=8 / 120 ms (DEC-0019). Soft mask rehunt + coldAcquireJob
  remain. Do not change hop geometry / TTL / DEC-0012 companion-louder bar.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = CADENCE after
  first emit without 400+ ms structure/opp hops (T-0010).

---

## DEC-0027 — Cold CQPSK escalate only on true emptyEye (not structure/opp)

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0028 on 115603)
- **Evidence:**
  - Capture `20260908_112922` (~483 s gapless, SNR ~14.4 dB, DEC-0026 exe
    mtime after BN-0015). Eyes OK (0× 40 ms; 2805× 280 ms). 0 ReturnEncrypted.
  - CADENCE: **0** seconds ≥0.65; peak **0.639** drop=D. worker-busy **417**,
    wrong TDMA **141**. Talk emit>0 median duty **0.318**.
  - DSP by eye class (112922):
    - emptyEye (b=0,tv=0): med **101 ms**, 0× ≥200 ms
    - oppOnly: med **202 ms** (DEC-0026 helped vs prior p90 burn)
    - **structureNoVcw** (b>0,tv=0,ov=0): n=61 med **462 ms**, 40× ≥400 ms
    - target>0: med **241 ms** (worse than 110146 202 ms) — starved by above
  - Soft mask-epoch rehunt already exists for structureNoVcw
    (`p25Phase2StructureNoTargetVoiceWindows >= 3` →
    `invalidatePhase2StickyMaskEpoch`). Cold 240/64 was stacked on top.
  - `emptyStreak >= 3` alone also cold-escalated during opp/structure silence
    (re-opened DEC-0026 hole without needing structureNoTarget).
- **Decision:** `emptyStreakReacq` = hadEmit && emptyEye && emptyStreak≥2 &&
  !oppositeSlotOnlyEye. Remove structureNoTarget and
  StructureNoTargetVoiceWindows from the cold CQPSK path. Keep soft mask
  rehunt. Keep hot cand=8 (DEC-0019). Do not change hop geometry / TTL.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  (T-0010). StructureNoVcw dsp should fall toward hot budget (~100–200 ms).

---

## DEC-0026 — Opposite-slot-only eyes must not cold-escalate CQPSK

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0027 on 112922)
- **Evidence:**
  - Capture `20260908_110146` (~445 s, DEC-0025 exe): eyes OK (0× 40 ms;
    2472× 280 ms). No `ReturnEncrypted` (DEC-0025 held). Companion-louder 0.
  - Only **12** CADENCE ok seconds all session. Dominant live: drop **A/D**,
    `worker-busy` **382**, wrong TDMA **238**.
  - Best IQ (TG **20202** RID 0x1F95EB @ 421.975): file
    `PASS_CONTINUOUS duty=0.85`; live **5** ok seconds, worker-busy 54,
    wrong-TDMA follow ×69. Wrong-slot worker hops dsp med **~173 ms**
    p90 **~434 ms**.
  - TG **30017** @ 420.225: file duty **0.74**; SNR p10 **13.3**; live
    talkMed **0.303** / dsp med **203 ms**.
  - Hot path after emit treated `structureNoTarget` (bursts>0, targetVcw=0)
    + empty feed streak as cold reacq (**240 ms / 64 cand**) — that is
    normal opposite-slot TDMA silence, not a lost eye.
- **Decision:** Exclude opposite-slot-only eyes (`Phase2WrongSlot` diag or
  oppVcw>0 with targetVcw=0) from `structureNoTarget` cold escalate. Keep
  DEC-0019 hot cand=8; do not reopen cand=3. Do not change hop geometry.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  (T-0010). Weak-RF calls (SNR p10 ~5) remain hard. **112922:** opp-only
  improved, but structureNoVcw + bare emptyStreak≥3 still cold-burned
  (DEC-0027).

---

## DEC-0025 — Clear→Encrypted latch needs MAC/PTT bar (match follow trusted ESS)

- **Date:** 2026-09-08
- **Status:** accepted (110146: zero ReturnEncrypted)
- **Evidence:**
  - Capture `20260908_103955` after DEC-0024: `context=81920` **0**; 280 ms
    eyes held. Operator 50/50; same TG **20202**, different RIDs.
  - RID **0x1F83FF** @ 420.725: live choppy; file voicetest duty **0.46**
    `PASS_PARTIAL` / no voice sync. Ring SNR p10 **7.3 dB**.
  - RID **0x1F95EB** @ 420.225: live better (7× CADENCE ok); file
    `PASS_CONTINUOUS duty=0.83` essEncrypted=no. SNR p10 **12.8 dB**.
  - Same “good” call ended `ReturnEncrypted` at 20:42:17 while last follow /
    DEEP DIAG still `p2ess=clear` / `encrypted=no` and PCM emit 18 ms prior.
  - Follow SM `trustedEncryptedEss` already requires `phase2MacCrcValid > 0`.
    Security-gate `thisWindowObservedEncrypted` could Clear→Encrypted latch
    (then `grantProvesEncrypted`) without that MAC bar.
- **Decision:** Treat this-window target ESS encrypted as latch/follow proof
  only with target/any MAC CRC **or** PTT-derived security state — same bar
  as `trustedEncryptedEss`. Log which ReturnEncrypted clause fired. Do not
  change hops/settle/DEC-0024. Do not invent TTL.
- **Consequences:** Re-measure 105622 ≥0.645; re-file both RID windows; live
  prove = new GUI capture (T-0010). Weak-RF RID remains drop D until Costas
  continuity improves (not this DEC).

---

## DEC-0024 — Backlog catch-up must keep DEC-0009 overlap (280 ms), not 40 ms

- **Date:** 2026-09-08
- **Status:** accepted (file 105622 ok; live 103955 proves eye fix)
- **Evidence:**
  - Capture `20260908_101644` (desktop exe after DEC-0023): soft-trim
    `context=573440` dominant (~1000 hops); old 80 ms protect almost gone.
  - Operator: start/middle BAD; last voice ~90% good.
  - First clear TG **30302** slot 0 @ 420.225: cold 720→280 ms eyes, then
    from 20:16:59.378 for ~8 s every hop was `context=81920` (**40 ms** =
    `kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds`). CADENCE dutySec
    **0** / drop **A** / `diag=no voice sync` until 20:17:07 when eyes
    returned to 280 ms and duty briefly hit **0.681**.
  - Late TG **10330**: almost all hops `context=573440` (148 vs 2–3 smaller)
    — matches the “last voice good” report.
  - SDRTrunk `P25P2DecoderHDQPSK`: continuous Costas+Gardner traffic source;
    no shrink-context catch-up when the worker lags.
- **Decision:** Backlog catch-up may still take **120 ms fresh / 80 ms
  minFresh**, but overlap is **280 ms** (DEC-0009), not 40 ms. Do not
  lengthen `kP25Phase2PostArmSettleMs` (80) without a settle-specific
  measure — 101644 cold eye already produced audio before the 40 ms spiral.
- **Consequences:** Re-measure 105622 ≥0.645. Live prove = new GUI CADENCE
  on desktop exe (T-0010). Do not reopen 80/0 or streaming default-on.

---

## DEC-0023 — Rolling soft-trim must protect DEC-0009 sustain overlap (280 ms)

- **Date:** 2026-09-08
- **Status:** accepted (superseded hole: DEC-0024 on 101644)
- **Evidence:**
  - Capture `20260908_095936` (desktop `SDR_Town.exe`, TG 30302 slot 0 @
    421.225): promising start (dutySec up to **0.60**, gate=emit,
    companion-louder **0**), then collapse. Talk median dutySec **0.40**
    still drop **D**; cliff ~20:00:18 → drop **A** / return-to-CC.
  - Worker eyes: early **360 ms** (`fresh=163840 context=573440`), then
    dominant **160 ms** (`context=163840` = 80 ms) — 110 submitted hops.
    DEC-0009 already measured 80 ms overlap → duty 0.40/0.51.
  - Rolling backlog hit **2.6–2.9 s** while worker-busy; soft-trim constant
    `kProtectedOverlapSamples = 163840` (**80 ms**) matches the clipped
    context. Comment in `RollingIqWindow::append` already names this as the
    20260901_042425 stutter path; protect depth was never raised to DEC-0009.
  - Voice-period ring SNR median **16.7 dB** (summary 5.2 dB is late CC).
    Not an RF fade for the good-start window.
- **Decision:** Soft-trim protected pre-roll is **280 ms** (573440 samples @
  2.048 MHz = DEC-0009 speaker-sustain overlap), not 80 ms. Active rolling
  window while speaker-live is **4.0 s** (was 2.0 s) so soft-trim can keep
  that overlap under the lag seen on 095936. Hard-cap must not jump the
  decode cursor while undecoded RF is protected; emergency cursor jump only
  above 4× rolling. Do not invent a new hop size. Do not reopen DEC-0012.
- **Consequences:** Re-measure 105622 block duty (must stay ≥0.645). Live
  prove = new GUI CADENCE on desktop exe after this change.

---

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - env=1 after 160 ms sustain: 105622 duty **0.125**, 073304 **0.22**,
    041716 **0.365** (was ~0.01 / 0.125 / 0 with 80 ms). Better, still
    far below PASS_CONTINUOUS 0.65. Wall ~7 s / 8 s span.
  - Root: after first emit, sticky HDQPSK still goes wrong-slot /
    `p2vcw=0`; longer slices only delay the starve.
- **Decision:** Revert to DEC-0014 80 ms streaming slices. Do not default-on
  streaming. Block 80+280 remains the file-proven path.
- **Consequences:** Live drop D needs a Costas/channelize fix that keeps
  duty, not another hop-size guess.

---

- **Date:** 2026-09-08
- **Status:** rejected
- **Evidence:**
  - Capture `20260908_082235` (stock 0.2.51): CC OK; talk median dutySec
    **0.189**; worker-busy; dsp median **185 ms** on 80+280 eyes; drop **D**.
  - File voicetest with DEC-0009 80+280 waits (~16.6 s wall / 8 s span) so
    duty stays 0.645 while live cannot wait.
  - Trial: after emit, block eyes **80 ms fresh + 0 overlap** (voicetest
    `speakerSustainContextMs=0` + live chunk planner same). Wall **2.8 s**
    (fast enough) but 105622 duty **0.645→0.055** drop=**A**; 073304
    **0.045**; 041716 **0.085**; 060221 **0.09**. Same class as streaming
    DDC eye loss (DEC-0014) / independent-eye DEC-0017 — not a live win.
- **Decision:** Do **not** shrink block sustain eyes to 80/0. Keep DEC-0009
  80+280. Live drop D needs either (1) faster DSP on 360 ms eyes (DEC-0019
  path) measured on the **desktop** GUI, or (2) streaming DDC that keeps the
  CQPSK/dibit eye (DEC-0014/0018 still open). Do not invent TTL/grace/PLC.
- **Consequences:** Reverted. T-0010 remains open.

---

DEC-0204 (2026-10-10): Persist listen/deviceKey on explicit Device Manager Apply. Resolve the serial-based identity at tuning time and synchronize primary receiver. Never fall back to RTL when the chosen radio disappears. Evidence: user screenshots and logs show RSPdx dev2 real stream at 07:17:00, disabled RTL dev0 automatically enabled at 07:17:28 by Tune and Receive. Keep individual Enabled flags and workflow ownership checks.

2026-10-10 local repair completion: Device Manager Apply now persists the highlighted enabled Listen radio by stable identity; missing or ambiguous saved identity never falls back to another dongle. RSPdx 2237050048 hardware tested with RTL 00000001 and 00000003 attached: RSP live, both RTL stopped. Marker has a grab handle, prioritizes tuning at squelch intersection, freezes gesture axis and tunes once on release. Spectrum offers 8k/16k/64k bins (64k default), 1 kHz minimum view, adaptive labels and gradient fill. Remove DC spike defaults on and subtracts mean only from private FFT samples, preserving decoder IQ. QApplication scope guard joins diagnostics thread before child destruction. Release build and selection/marker/DC regressions PASS; installed radio smoke exits 0. Original desktop installation retained at sdrTown-backup-20261010-radio-selection. This is a local repair, no public release performed. Evidence: sdrtown-repair/hardware-qa.json and spectrum-final-smoke.stdout.txt, stderr.txt.

2026-10-10 follow-up: stopped stale Listen radios on primary tune/play/runtime start and Device Manager Apply, preserving active secondary receivers and other workflow owners/assignments. Reproduced RTL00000001 live before RSPdx selection with both Enabled boxes checked. Apply stopped RTL and started RSPdx, hardware report exclusive-hardware-qa.json PASS with 65536 bins. Tune-only reproduction separately qualified. Deliver complete local Windows ZIP and corresponding updated source, no standalone EXE.

2026-10-10 live marker update: mouse drag coalesces tuning every 40 ms and commits any final unsent position on release. In-capture Listen tuning changes the demodulator frequency while keeping RF center fixed; outside-capture tuning recenters. Green shaded passband width follows selected channel bandwidth. Qt regression confirms pre-release signal and proportional green shade. Physical RSPdx test live-marker-hardware-qa.json: tuned 100136989.96655518 Hz before release, RF center unchanged at 100000000 Hz, both RTL stopped, exit 0.

2026-10-10 mode/passband update: selected DemodMode now reaches SpectrumWidget. USB shade extends from carrier to carrier+BW; LSB from carrier-BW to carrier; AM/NFM/WFM/CW centered. Live mode and bandwidth values update together. Shared six-stop waterfall palette uses dark navy/blue/cyan/green/yellow/red consistently for full span and zoom. Qt rendering regression passes USB/LSB direction, AM/NFM/WFM centered widths, live drag and palette endpoints.

2026-10-10 frequency wheel: LiveFrequencySpinBox intercepts wheel over the field or its line editor without focus requirement, accumulates partial notches, clamps range, and invokes existing selected-radio/live Listen tuning handler. Ctrl uses 1/10 normal step, Shift 10x. Regression covers live callbacks, partial wheel steps, fine step and upper-bound no-op. Includes mode-aware passband and shared blue full-color waterfall.

2026-10-10 blue/sensitivity update: spectrum background dark navy, filled trace saturated blue. Default Auto display levels estimates floor from 20th percentile finite bins, sets floor-12 dB and upper max(floor+38, peak+6) with 40..100 dB bounded span, smooths after first frame. No radio gain or IQ modifications. Manual min/max available when auto is disabled. Render regression shows -130 dB tone above -140 dB floor visible above bottom; mode shade, wheel and live marker checks remain passing.

2026-10-10 HF static filter: switchable HF noise filter checkbox defaults on (saved listen/hfNoiseFilter). Analog playback below 30 MHz applies 200 Hz Butterworth high-pass and cascaded 2800 Hz low-pass stages, with 10 ms crossfade on toggle. Filter runs only at speaker output after decoder feeds, bypasses P25. No IQ or RF gain change. Test response amplitudes: 50 Hz 0.06237, 1 kHz 0.98382, 8 kHz 0.01051. Disabled output exactly unchanged; arbitrary chunk splitting matches continuous processing exactly. Cannot eliminate interference overlapping wanted audio.
