# P25 Phase 2 target-slot security/audio patch

This patch hardens the P25 Phase 2 clear-audio path toward the current sdrtrunk model:

- Phase 2 audio release now uses followed-slot-only security evidence instead of aggregate RF-carrier ESS/MAC counters.
- Unknown/grant-clear Phase 2 PCM is queued under a strong call key `(NAC, WACN, System ID, TG, slot, frequency)` and is not flushed by talkgroup alone.
- Control-channel clear service options are treated as follow/probe permission, not as speaker-release proof.
- Speaker release requires target-slot PTT/ESS/session evidence (`sessionAudioRelease` or target-slot trusted clear ESS).
- Explicit target-slot encrypted ESS or encrypted grant fails closed and clears pending audio.
- Phase 2 decoder MAC state now distinguishes current-call PTT anchoring from generic CRC-valid MAC evidence.
- MAC_END_PTT, MAC_IDLE, and MAC_HANGTIME clear the call/session state.
- Mask-phase scoring now runs with per-timeslot session state and stronger I-ISCH/slot consistency checks.
- Pending Phase 2 audio is cleared on voice reset, RF/mode reset, and slot-probe reset.
- Static verifier scripts were updated for the stricter keyed pending-audio and target-slot security behavior.

Validation performed in this package:

```text
P25 Phase 2 sdrtrunk security/session gate: PASS
sdrtrunk-like Phase 2 encryption/security gate: PASS
P25 Phase 2 decode-through-settle/offset/session regression: PASS
P25 Phase 2 voice-arm blocking commit regression: PASS
P25 Phase 2 slot-probe/audio stability regression: PASS
P25 sdrtrunk parity math/flow regression: PASS
P25 Phase 2 sdrtrunk-like audio emit cursor regression: PASS
P25 Phase 2 unknown-grant vocoder gate regression: PASS
P25 Phase 2 current-call encrypted hold/manual-block diagnostic regression: PASS
```

Notes:

- The uploaded archive still does not include project headers such as `P25LiveDecoder.h`, `Receiver.h`, and `P25FollowStateMachine.h`, so full C++ compile validation could not be performed in this environment.
- The verifier scripts that previously assumed an `include/` directory were adjusted to use the current archive layout when headers are absent.
