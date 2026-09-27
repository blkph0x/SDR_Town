# P25 missed grants / no-voice audit - 2026-09-27

T-0087 / DEC-0159. Diagnosis and next-pass plan only. No production source,
decoder gate, timer, slot selection or RF correction changes in this pass.

## Conclusion

A fresh live GUI capture reproduces a stale allocation: the GUI keeps following
TG 30302 on 420.100 MHz slot 0 after the recorded control channel announces
TG 10120 on that allocation. The speaker's talkgroup isolation rejects the
new talkgroup, correctly. Control-channel processing is disabled during this
one-SDR follow, and raw/other-call activity is allowed to prolong its lifetime.

The same eight seconds of IQ produce **zero audio for TG 30302**, versus
**304 accepted AMBE frames / 6.08 seconds of output for TG 10120**. Local STT
recognizes a coherent English sentence from the latter WAV. This is evidence
for a missed allocation transition, not evidence that mbelib needs replacing
or that talkgroup/slot/security rejection should be relaxed.

It does not establish the cause of every reported silent call. Historical
failed-call records have no matching recent IQ, and no independent receiver
was running alongside this live test. The first version introducing these
older orchestration defects is not established.

## Baseline and evidence

- Source HEAD at audit start: `a73b1c8`; published runtime v0.2.114 from
  `68a6586873b19d82b57c783247918c35f372268a`.
- `verify_no_p25_changes.py --base 1f7740e --head HEAD`: 73 changed paths,
  **zero protected paths** since v0.2.110. This rules out a direct recent edit
  to those sources, not every runtime/configuration/RF regression.
- User validation JSONL, 2026-09-27 05:52-06:05 UTC: 43 post-security records,
  36 partial-accept and seven attempts-rejected. These are throttled anomaly
  records; pre/post pairs and overlapping windows are not independent frames.
  They do not measure the number of missed grants or an overall success rate.
- New capture: `build/p25-audit-20260927/`
  `20260927_061308_403_regression-audit_420.35000MHz_startstop/`.
  RTL-SDR, 2.048 MS/s, gain 40, existing correction -2.316655763 ppm.
  Published GUI launched with `--gui-device 0 --gui-p25-control 420.350`
  `--gui-grant-test --gui-capture-seconds 60 --gui-exit-after-ms 75000`,
  explicit capture root/label and disabled remote diagnostics/control server.
  No experimental unlock/probe or clear-grant override used.
- 60.032 seconds / 122945536 complex samples / 983564288 bytes. IQ SHA256:
  `473b69c1dd5d0ed205bba1109c11e84046891a79112e76368d61340a728c9eaf`.
  Reported capture-ring overruns, epoch resets and write errors: zero.
  This proves captured sample continuity, not absence of tuner transitions.
- GUI speaker capture: 316800 samples at 48 kHz = 6.6 seconds of emitted PCM.
  This file concatenates emits; it is **not** 60 seconds of speaker chronology.
  Recording and local STT transcript stay local, not in Git or telemetry.

### Live timeline (local AEST)

| Time | Evidence |
| --- | --- |
| 16:13:12.360 | Accepted TG 30302, channel 0x7010, carrier 8, slot 0, 420.100 MHz. Hardware center moves to 420.08875 MHz. |
| 16:13:12.988 | First speaker push; target-slot clear evidence and 24 frames. |
| 16:13:14-19 | Several cadence windows near 1.0 duty; initial decode is functional. |
| 16:13:20.984 | Worker has 22 raw VCWs, zero target VCWs, 22 wrong-slot rejections. No PCM. |
| 16:13:22.756 | Zero target/fed frames; follow's lastActiveAgeMs is still zero. |
| 16:13:38-40 | Offline CC replay of this recorded interval resolves TG 10120, channel 0x7010, 420.100 MHz, slot 0. Live remains on TG 30302. |
| 16:13:45.750 | A small last emit is logged; it is not proof of renewed valid speech. |
| 16:13:57.687 | No-VCW watchdog finally returns to CC, 45.327 seconds after initial follow. |
| 16:13:59.134 | First subsequent CC stage log. Last pre-hold stage was 16:13:12.383. |
| 16:14:01.962 | Later TG 12068 follow is positively encrypted and returns muted. Correct behavior. |

Live worker log: median DSP 80.916 ms for median 160 ms fresh input;
observed queue drops, result drops and sequence drops all zero. Capture CSV
audio-producer dropped frames zero. The logscan heuristic says `slow_voice_worker`,
but these measurements do not establish sustained CPU overload as this failure's
cause. Zero-fill counters include legitimate silence and control monitoring.

## Confirmed defects and limitations

### P1: wrong-call evidence extends follow lifetime (ISS-0042)

`src/P25TrafficChannelProcessor.cpp:106` makes `p2vcw` the maximum of global
stats and **all-slot** burst counts. At line 165, any such count can clear
`m_callEnded`, even when every burst was rejected by target TG/slot matching.
At line 171, raw target VCWs refresh `lastVoiceMs` without validity/mask proof.
At line 181, sticky `sessionAudioRelease` alone refreshes `lastActiveMs`.

`src/MainWindow.cpp:6453` then treats that `p2vcw` as filtered meaningful voice
(the comment is incorrect). Lines 6553-6573 use it in acquisition/voice evidence,
and line 6574 refreshes the GUI follow timer. The state machine also treats
these raw counters as current evidence (`src/P25FollowStateMachine.cpp:79`,
`:107`, `:132`). Thus fixing only the traffic processor is insufficient.

An isolated MSVC/C++20 harness linked the **unchanged production cpp** and
reproduced these transitions:

| Input sequence | Actual result |
| --- | --- |
| Valid selected-slot END_PTT, then four VCW candidates belonging only to the opposite slot / another TG | `callEnded` changes true -> false; selected-slot lastVoice remains zero. |
| Invalid selected-slot burst, four raw VCWs, no XOR mask / MAC / security | lastVoice changes zero -> current time, audio remains closed. |
| Selected-slot burst with only sticky sessionAudioRelease, no VCWs or MAC CRC | lastActive advances and audioOpen becomes true; this alone does not prove speaker PCM escaped. |

Harness: `build/p25-audit-harness/`. Existing tests pass 68 assertions / 11
traffic cases and 190 assertions / 51 follow cases, but do not cover the
cross-slot-after-END sequence. Passing them is not a lifecycle correctness proof.

Local SDRTrunk reference `80360029efb008dca993938d1e34ad4a7a8c15bd`,
`P25P2DecoderState.java:225-243`, accepts valid messages for its own timeslot
before publishing voice continuation. Its ESS handler separately updates
security. These are distinct contracts; sticky security is not fresh RF activity.
Reference path: `_codex_refs/sdrtrunk/src/main/java/io/github/dsheirer/module/`
`decode/p25/phase2/P25P2DecoderState.java`.

### P1: in-band CC is disabled during follow (ISS-0043)

`src/MainWindow.cpp:5533-5542` disables control decoding for active independent
traffic whenever the primary was retuned **or CC offset exceeds 75 kHz**.
This is stronger than the preceding passband check. In this capture, CC offset
is about 261.25 kHz inside 2.048 MHz sampling, yet the GUI stops decoding it.
The startup message claiming that control remains on CC is misleading here.

Offline replay of 30-32 seconds of the **same saved IQ** decodes CRC-valid
Motorola OP 0x03 / MFID 0x90 grants for TG 10120, channel 0x7010, carrier 8,
slot 0, 420.100 MHz. No simultaneous hardware receiver or assumed bandplan was
needed. Evidence: `build/p25-audit-cc-30s.log`. The identifier is acquired within
the replay; its initial unresolved rows must not be read as FDMA grants.

This proves recoverable CC messages were present while live CC processing was
paused. The offline forensic search has a larger compute budget, so it does
not prove simply deleting the condition will make live simultaneous decode
reliable. A bounded, separately scheduled CC path needs measured qualification.

### P2: capture retune metadata is incomplete (ISS-0044)

The metadata emits one capture at sample 0 / center 420.350 MHz
(`src/MainWindow.cpp:12763-12771`), although this recording contains multiple
hardware retunes. Ring continuity alone is reported as `ok_gapless`.
Generic replay can tune the wrong RF location unless the operator supplies
the correct per-interval center. Event/ring logs do not provide sample-exact
center/epoch transitions here. Existing CLI `voicecenter` is a manual workaround,
not a self-describing multi-retune recording.

`vcw-soft-or-nonvoice-filtered` also hides important distinctions between an
opposite slot, changed TG and actual bad voice bits. These must be separate
follow decision reasons, not an instruction to admit rejected frames.

## Controlled replay results

All commands use the captured NAC/WACN/system and immutable slot, with no
`clear`, `probe`, `audioprobe` or environment-gate overrides. `stream noprobe`
uses normal realtime replay configuration. These are decoded PCM tests, not
audibility guarantees for every live talkgroup.

| IQ interval / selection | Result |
| --- | --- |
| 4.5-20.5 s, TG 30302, v0.2.113 | 272/272 AMBE, 5.44 s PCM, duty .34, PASS_PARTIAL_AUDIO. |
| Identical interval / selection, v0.2.114 | Identical counters **and identical WAV SHA256**. |
| 26-34 s, TG 30302, v0.2.114 | FAIL_NO_AUDIO, zero attempts/PCM, 1234 stale-TG mismatch VCWs (includes context). |
| Same 26-34 s, TG 10120, v0.2.114 | PASS_CONTINUOUS_AUDIO numeric gate; 304/304 AMBE, 6.08 s PCM, duty .76, 155 valid MAC CRC, clear ESS, seven concealment frames, two feed gaps. Local STT yields coherent English. |

v113/v114 identical WAV SHA256:
`d6388ccbe592431f3aeb5ed72949c0f8d4b1dacb550d8eb79fe14801a75130ee`.
Recovered TG10120 WAV:
`c8a1892899437ce95bb8c35f5ec484ca729f630b4e999aa8739d1fed44155c92`.
No full-release clear-audio acceptance is claimed from this one excerpt.

Reproduction, using the capture directory above as `CAPTURE`:

```text
p25 replay "CAPTURE" 420.350 2000 skip=30000 center=420.08875
p25 voicetest "CAPTURE" 420.100 8000 skip=26000 center=420.350 voicecenter=420.08875 tg=10120 slot=0 phase2 nac=0x2d2 wacn=0xbee00 system=0x2d1 stream noprobe wav=out.wav
```

Repeat the second command with only TG changed to 30302 for the silent control.
Replay modifies normal local P25 registry/cache data; it is not a stateless
registry inspector. No settings thresholds were manually changed in this audit.

## Next pass, in order

1. **T-0088: correct evidence ownership.** Add deterministic clocked lifecycle
   tests first. Separate raw diagnostics, valid selected-allocation activity,
   sticky security and emitted PCM. Wrong-slot, wrong-TG, invalid/maskless and
   replayed-context candidates must not extend the selected call or undo its
   END. Keep bounded pre-audio acquisition; do not require successful PCM to
   preserve a valid call. Process END/new-PTT in sample order. Update processor,
   GUI snapshot and follow SM together; no timer retuning or security relaxation.
2. **T-0089: keep in-passband CC allocation information current.** Independent
   bounded CC IQ cursor/worker, actual center/stream-epoch metadata and measured
   CPU/queue budgets. Observe grants without preempting valid selected speech.
   When a validated new allocation replaces an ended/retired one, apply TG,
   slot, frequency and generation atomically; clear prior security/vocoder/PCM
   state and discard old-generation worker results. Preserve out-of-band and
   two-SDR ownership rules. Do not switch merely because untrusted traffic
   mentions another TG. Retire/mute obsolete ownership rather than opening its
   speaker to the newly allocated talkgroup.
3. **T-0090: make evidence replayable and reasons explicit.** Record sample-indexed
   tune/center/rate/epoch changes in existing SigMF/events; reject ambiguous
   replay intervals. Log CC suspended/resumed reason, new/old allocation and
   selected-versus-companion/stale-TG counters with last proven activity sample.
   Keep logs bounded; no high-rate raw audio/IQ upload.
4. **Qualification before release.** Preserve initial TG30302 decoded output;
   the same-IQ live/GUI replay must discover the TG10120 allocation and match
   the trusted excerpt's voice/identity without forced-clear hints. Test
   opposite-slot encrypted traffic, clear->encrypted, same-RF slot reuse,
   END then new RID/PTT, no-signal timeout, retune/cancel and late worker results.
   Measure queues and sample continuity with CC+voice together. Run CLI/GUI
   replay and bounded live tests, STT plus listening, then standard full tests
   and CI/public-asset verification. Numeric duty alone is not release proof.

No P25 runtime fix or new binary release was produced by this diagnostic pass.
