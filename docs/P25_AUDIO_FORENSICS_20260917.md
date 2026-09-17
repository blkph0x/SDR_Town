# P25 Phase 2 receive-chain investigation, 2026-09-17

Status: confirmed software defects repaired; continuous intelligible live audio is NOT yet proven.

## Reproduction

Capture: `20260916_103841_245_iq_NFM_420_35000MHz_420.35000MHz_startstop` under the user's AppData IQ capture directory.

Primary interval: 18,047 through 30,047 ms, TG 10120, slot 1, voice 420.100 MHz, physical voice center 420.08875 MHz, NAC 0x2D2, WACN 0xBEE00, system 0x2D1. The SigMF capture has 415.168 seconds of IQ with zero reported capture overruns or sample gaps. Capture integrity does not establish RF or decoder correctness.

CLI command (replace CAPTURE with the capture directory):

```text
p25 voicetest "CAPTURE" 420.10000 12000 skip=18047 center=420.35000 voicecenter=420.08875 tg=10120 slot=1 phase2 nac=0x2d2 wacn=0xbee00 system=0x2d1 wav=build/repro.wav trace
```

## Confirmed failure chain

The primary symptom appears in `p25Phase2SpeakerAudioForQueue`: newly generated PCM is discarded as duplicate audio. Its input has complete PCM and an ordinal for each frame, but the ordinal belongs to a restarted counter. The earliest confirmed divergence is in `p25Phase2ResetVocoderForNewTalkspurt`, which reset the frame sequencer to ordinal zero while keeping the same call session. The speaker queue retains its per-call ordinal.

The instrumented baseline reaches expected speaker ordinal 294. At window 76, `VOCODER_RESET why=post-end-voice` occurs. The next block contains ordinals 0 through 19; all 20 PCM frames are rejected. Repeated resets cause a total of 106 output frames to be lost. The speaker's duplicate check is doing its job; the producer restarted its numbering in the same call.

`P25Phase2FrameSequencer::resetForTalkspurt` now resets burst-local state while preserving call identity and the next output ordinal. Full session reset still clears the ordinal. Absolute-input duplicate suppression remains enabled.

Further confirmed code-path defects:

* MAC/PTT handling ran as a prepass over the whole window, before earlier voice in that window. MAC boundaries now run in the ordered burst loop. `P25Phase2TalkspurtOrder` rejects repeated or older boundary positions. A post-end restart requires a later, otherwise eligible voice codeword. Unknown slot labels do not authorize resetting the selected slot. Wall-clock debounce no longer decides whether an identical captured boundary is new.
* `pushP25LiveStreamingAudio` withheld ready PCM whenever queued plus pending samples fell below the startup threshold, even during active playback. Startup priming now applies only to an empty ring. Tests cover 20 ms frames extending an active queue below the previous 240 ms threshold.
* GUI replay only drained approved pending PCM when another decoder block could emit. The GUI result showed 7,680 approved pending samples surviving several empty decoder windows. Those windows now also service approved pending PCM without admitting the current rejected block.
* The block resampler recalculated its fixed Blackman window coefficients in the per-output-sample loop. These identical coefficients are now computed once. No tap formula, cutoff, sample position, or filtering order changed.

## Measurements

| Run | PCM frames generated | Frames retained | Speaker filter drops | WAV seconds | Reported feed gaps | Vocoder resets |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original baseline | 392 | 286 | 106 | 5.72 | 8 | 8 |
| Ordinal fix only | 392 | 392 | 0 | 7.84 | 8 | 8 |
| Ordered MAC transitions | 392 | 392 | 0 | 7.84 | 8 | 6 |
| Final CLI replay | 400 | 400 | 0 | 8.00 | 7 | 6 |
| GUI replay with pending drain | 400 | 400 | 0 sample loss at push boundary | 8.00 | 7 | Not exported by GUI report |

The exact ordinal-only comparison recovered 106 x 20 ms = 2.12 seconds. The additional eight frames in some runs also occurred in the instrumented baseline; search uses wall-clock budgets, so that difference must not be attributed to the ordinal fix.

Final CLI details: 1,260 reported bursts, 932 selected codeword observations including overlap, 424 reported expected codewords, 400 vocoder submissions, 394 accepted decode results, 400 PCM/ordinal frames, 49 concealment frames, 532 duplicate suppressions (450 absolute/context suppressions), zero sequencer late drops, zero slot changes, zero unnumbered PCM windows, and 204 MAC CRC successes. Observation/expected counters are not independently deduplicated ground truth and cannot establish the exact missing RF-frame count.

The final CLI processed this 12-second interval in 19.688 wall-clock seconds. This remains too slow for reliable real-time operation on this test machine. The coefficient optimization did not make the whole decoder real-time.

The GUI used the default GSX 1200 Pro speaker output at 48 kHz. Its report records 384,000 pushed samples, zero pending samples, and zero discarded tail samples. These are producer-side counts, not an independent recording of the callback or loudspeaker. Callback underruns, overruns, and wall-clock output gaps were not measured for this replay and must not be reported as zero.

A second interval, TG 10326 slot 0, starting at 236 ms for six seconds, generated and retained 56 frames (1.12 seconds), with zero speaker-filter losses. It still reported three feed gaps and 24 concealment frames. This is a slot-0 regression check, not proof of a six-second continuous transmission.

## Reference and experiments

Inspected local SDRTrunk `P25P2AudioModule.receive`: it processes selected-timeslot voice and valid MAC messages as they arrive and queues voice pending encryption determination. Its flow supports handling MAC state in temporal order. No independent SDRTrunk/OP25 decode of this same IQ was completed.

Two alternatives were tested without changing defaults:

* `SDR_TOWN_P25_STREAMING_DDC=1`: 5.42 seconds runtime, zero selected audio frames. Rejected as a replacement for the default path.
* `hopms=240`: 10.03 seconds runtime, 6.36 seconds of speaker PCM, 12 feed gaps. It lost coverage relative to the normal hop and was not made the default.

## Verification and artifacts

Release app and test executable build successfully. P25 tests pass: 3,165 assertions in 121 cases. New behavioral coverage includes talkspurt ordinal continuity, overlapping MAC boundary order, and active playback below its startup threshold. The existing playback queue verification script also passes.

Artifacts are in `build/`:

* `p25_103841_tg10120_before.log` and `.wav`
* `p25_103841_tg10120_trace.log` and `.wav` (contains the exact ordinal collision)
* `p25_103841_tg10120_ordinal_fix.log` and `.wav`
* `p25_103841_tg10120_order_fix.log` and `.wav`
* `p25_103841_tg10120_final.log` and `.wav`
* `p25_103841_tg10120_gui_drain.json`, `.log`, and `.wav`
* `p25_103841_tg10326_slot0.log` and `.wav`
* `p25_103841_tg10120_stream_experiment.log`
* `p25_103841_tg10120_hop240.log` and `.wav`

Local STT ran on baseline, repaired CLI, and GUI WAVs. It returned similar partial English with uncertain wording. There is no verified transcript to score against, and STT output is not proof of intelligibility. The program's `PASS_CONTINUOUS_AUDIO` label measures coverage thresholds; it is not an independent speech-quality verdict.

Remaining work: identify the unrecovered/poor-quality selected frames against the same-frame RF/FEC evidence; explain remaining talkspurt resets; make the selected-slot decoder consistently faster than incoming IQ; and measure callback consumption and actual playback gaps during live reception. Do not declare the Phase 2 repair complete from this report.

## New capture 055312: playback silence insertion

Capture `20260917_055312_036` contains 199.984 seconds of IQ with no reported ring overruns. The audit finds 28 increases in playback underrun counts and several approximately 600 ms voice jobs. These do not establish that all gaps originate in playback: the opening replay also rejects voice codewords.

The logged top-up operations sum to 724,800 synthetic bridge samples (15.1 seconds at 48 kHz). This is a producer-side sum, not a measurement of silence actually heard: some bridge samples can be preempted. The prior capture logged 167,040 samples (3.48 seconds); the captures have different durations and traffic, so these totals are not a rate comparison.

Confirmed code issue: the top-up producer inserts zeros to maintain a 320 ms queue even with real speech still queued. Subsequent speech is appended behind those zeros. Bridge removal only handles leading bridge samples when the combined queue exceeds the push ceiling, so it does not prevent silence being interleaved between real blocks below that ceiling. The AudioEngine callback already zero-fills a genuine underrun.

Removed the bridge insertion call from `p25TopUpSpeakerPlaybackRing`, retaining real pending-PCM draining, startup priming, vocoder concealment, and encryption gates. Local SDRTrunk `AudioChannel.getAudio()` returns no audio when insufficient samples are available rather than appending speculative silence to its speech queue. This is a narrowly scoped scheduling correction, not a claim of full pipeline equivalence or faster decoding.

Volume measurements of the recorded pre-device speaker PCM: previous capture RMS -20.36 dBFS, new capture -21.04 dBFS. Both reach almost full scale. These mixed-call aggregates do not measure perceived loudness of a particular radio; they do not justify a global gain increase. No gain or filtering change was made. Background buzz remains unlocalized.

Verification:

* Release app and tests rebuilt; 3,165 assertions in 121 P25 cases pass.
* Playback source regression verifies no producer calls the legacy bridge helper and genuine callback underruns remain zero-filled.
* Opening six seconds, TG 30003 slot 0 at 416.550 MHz: CLI before/after PCM WAV SHA256 is identical, `FCC03FE27C74EF57A2E83CF814D86DAC710902FE8FECFD0231FC433411BCCFE4`.
* Both CLI runs produce 126,720 speaker samples (2.64 seconds), four feed gaps, 52 concealment frames, and three speaker-filtered frames. No decoded-audio improvement is claimed for this playback-only fix.
* GUI replay exits successfully, pushes the same 126,720 samples, and ends with zero pending/tail-discarded samples. This file replay does not reproduce all live scheduler conditions or independently record callback audio.
* The opening replay changes to encrypted status around 3,880 ms. Its correctness requires same-burst signalling validation; the gate was not weakened. An encrypted result label is not proof of clear speech.

Artifacts: `build/p25_055312_before.log/.wav`, `build/p25_055312_after.log/.wav`, and `build/p25_055312_gui.log/.json/.wav`. Live improvement and the remaining buzz require further measurement; this pass does not certify continuous clear Phase 2 audio.

## Capture 060515: exact RS recovery optimization (0.2.54)

The next user capture lasts 92.064 seconds with no recorded IQ gaps/overruns.
It logs 24 playback-underrun increases, 24 slow worker results, and no synthetic
bridge pushes. Removing bridge injection did not solve late speech delivery.
These observations cannot prove the subjective regression was caused by the
bridge change: RF traffic differs, and underrun counters were previously masked
by queued silence. No additional queue/timeout/security tuning was made.

Located redundant work in `rs63DecodeErasures`: every recovery hypothesis
recomputed unit-symbol syndromes, GF64 polynomial products, and inverse powers.
The 0x43 field polynomial and all decoder decisions remain unchanged. Immutable
product/inverse tables and 63 cached unit-symbol syndrome columns remove that
work. All 65,536 byte-input products and 256 inverse inputs match the independent
existing polynomial implementation in tests. Existing ACCH RS repair tests pass.

Same-IQ comparison (TG 30003, slot 1, 420.100 MHz, skip 8015 ms, duration 8000 ms):

| Metric | Before | After |
| --- | ---: | ---: |
| Process wall time | 11.265 s | 7.562 s |
| Profiled commit time, 92 windows | 4423 ms | 421 ms |
| Profiled total decode time | 10511 ms | 6770 ms |
| Worst profiled window | 484 ms | 203 ms |
| Pushed PCM | 4.92 s | 4.96 s |
| Feed gaps | 6 | 6 |
| Concealment frames | 66 | 66 |

Time-budgeted search can complete additional work after a speedup. Thus this
capture's output is not byte-identical: it recovers two additional frames.
The earlier 103841 TG10120 reference is byte-identical (SHA256
`3B1D9D029F16D48DC120946689EE19BFF600BDE183871A264553CF8BC577C95F`), with
wall time reduced from 19.688 to 11.625 seconds for twelve seconds of IQ.
New-capture GUI replay pushes the same 238,080 samples as CLI, zero pending and
zero tail discarded. Six feed gaps remain. Counts do not prove intelligibility.

An automated 60-second live GUI capture on 420.350 MHz opened real RTL-SDR
streaming, followed two grants, and recorded 49 direct audio-output events.
It logs one slow worker result and 17 underrun increases. Different RF traffic
means these counts are not an A/B speed benchmark. Capture health is gapless.
Shutdown exits 0 but logs a recoverable native Soapy teardown warning, still
unresolved. Artifact directory: `build/live_gf64_validation/`.

Release/test scope: 68,957 assertions in 122 P25 cases pass. The stale scheduler
source check was updated to recognize the tested startup-prime helper rather
than the removed variable name. Staging now copies runtime DLLs/Qt plugins,
not replay WAVs, logs, nested build files, or old baseline executables.
Version 0.2.54 remains experimental; live continuity, concealment losses and
background buzz are open. No clear-audio completion claim is justified.
