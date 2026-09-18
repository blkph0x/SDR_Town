# P25 downstream audit - 2026-09-17

## Confirmed defect

The callback formerly loaded readPos, copied samples, then unconditionally
stored its advanced position. clearBuffers could concurrently advance readPos
to writePos; the callback could overwrite that with its old position, making
discarded audio readable again. dropQueuedBridgeAudio was another writer to
the same consumer cursor. This violates the single-consumer ownership rule.

DEC-0074 guards exceptional cursor changes against the callback. The callback
tries an atomic lease once, never waits, and produces silence if control owns
the cursor. Control operations hold the existing producer mutex and wait for
the callback lease. There is no callback logging, allocation, or mutex wait.
Tests cover clear versus an in-flight cursor and failed lease ownership.
This demonstrates and repairs a possible interleaving; it does not attribute
every historic repeat or the current captured gaps to this race.

## Live evidence

Capture: `build/live_audio_cursor/20260917_072154_033_audio_cursor_420.35000MHz_startstop`.
Audit: `build/live_audio_cursor_audit.json`. Application exited successfully.
90.016 seconds, zero IQ overruns/epoch resets, three follows:
TG10703 slot 0 at 417.550 MHz, TG10120 slot 1 at 421.850 MHz,
TG30302 slot 1 at 421.350 MHz. There were 158 audio-output events and 54
real-PCM top-ups. TG10703 has 38 output events spanning 7.117 seconds with
the underrun counter unchanged.

Callback deltas across 643 polls:

| Counter | Delta |
|---|---:|
| consumed frames | 1741440 |
| zero-fill frames | 2580960 |
| empty callbacks | 5377 |
| partial callbacks | 0 |
| control-silence frames | 0 |
| producer-dropped frames | 0 |

Counters aggregate outputs. Empty callbacks include idle and CC silence.
Zero partial callbacks does not exclude gaps aligned to callback boundaries.
The unchanged-underrun span proves no reported ring shortage between these
events, not continuous intelligible acoustic speech. The existing audit
underpush heuristic can include deferred PCM and is not proof of loss.

## Replay and non-regression

`build/p25_live_cursor_10703_replay.log`: same live IQ, skip 3500 ms, duration
10000 ms, TG10703 slot 0, target 417.550 MHz, voice center 417.53875 MHz,
capture center 420.350 MHz, NAC 0x2d2, WACN 0xbee00, system 0x2d1.
No clear-encryption override. 378 decoded/fed frames, six concealment frames,
two feed-gap events, 7.56 seconds PCM; zero speaker drops or TG mismatches.
The tool's PASS_CONTINUOUS_AUDIO label is a counter-based heuristic only.

The live speaker WAV and replay STT have recognizable phrases but erroneous
words. STT is supporting evidence, not ground truth. The speaker WAV is a
submitted-PCM tap, not microphone/loopback proof of the physical output.

The 060515 reference replay after this downstream change is byte-identical
to `build/p25_060515_final_geometry.wav`. Full Release tests pass: 81,154
assertions across 235 cases. Capture audit self-tests pass.

## Remaining acceptance gap

ISS-0001/T-0010 remain open. Establish missing speech versus real call pauses
using selected-slot, per-call decoded-frame timelines and independent known
speech, then align those to callback output. A continuous WAV or a plausible
transcript alone cannot validate intelligibility or correct slot identity.
Do not enlarge buffers, relax validation, or synthesize missing speech merely
to improve duty metrics. This pass changes no decoder/timer thresholds and
does not publish a release.
