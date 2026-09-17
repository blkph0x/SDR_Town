# Issues (canonical)

Never delete a row. Close with a commit hash and a sentence.

2026-09-18 OPEN T-0031: NFM raw tap can produce fractional sample rates
(Demod.cpp uses input rate / integer decimation), whereas the pinned SSTV
backend takes an integer rate. Rounding the metadata is not a verified
conversion. Qualify a continuous resampler against independent recordings
before attaching live input. Queue lifecycle requires producer detachment and
quiescence before restart; validate that in receiver integration. These are
unfinished live-feature gates, not defects in the existing recorded decoder.

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
