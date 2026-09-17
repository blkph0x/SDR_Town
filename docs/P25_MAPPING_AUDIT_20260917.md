# Phase 2 mapping and block-tail audit

## Reproducible defects

### Nonphysical symbol permutations

The Phase 2 traffic search evaluated all 24 permutations of four quadrant
labels. SDRTrunk's `DQPSKGardnerSymbolEvaluator` uses cyclic labels
3,2,0,1 at -135,-45,+45,+135 degrees. Its rotations/reflections have opposite
pairs differing by XOR 3. Sixteen arbitrary permutations violate that mapping.

Full validation of capture `20260917_060515_885` at 06:57 UTC reproduced this
at absolute burst dibit 57777. Two overlapping windows decoded the same RF:
one had zero I-ISCH errors/location 0; the other had six errors/location 2.
Comparing all 160 raw payload dibits found exactly:

- 47 occurrences of 1 unchanged, 39 occurrences of 3 unchanged;
- 43 occurrences of 0 mapped to 2, 31 occurrences of 2 mapped to 0.

This is a discrete mapping error, not random RF noise or audio jitter. The
wrong mapping produces plausible sync and voice DUIDs but incorrect payload.
DEC-0072 restricts Phase 2 candidates and remembered mappings to physical
rotations/reflections. Phase 1 candidate enumeration is unchanged.

The later tail after dibit 69000 becomes CRC-valid FACCH/SACCH signaling under
the corrected mapping, rather than the repeated high-error voice seen before.
Less WAV duration therefore cannot automatically be called lost speech. Some
real frames remain missing: do not equate all removed PCM with false voice.

### Complete bursts beyond one lock

`[block-tail]` supplies two valid superframes with maxPhase2SuperframeLocks=1
and block DDC. Original code loses physical burst 12. Its uncovered-sync path
sees C/D but misses intervening A/B bursts. DEC-0073 allows the existing bounded
walker to extend an anchor established in this same commit, never a previous
block's stale anchor. The first implementation duplicated burst 14 through
uncovered-sync recovery; skipping already-walked absolute positions fixes that.
The final test requires all 24 bursts exactly once, then no bursts from an
equally sized noise-only block using the same decoder.

## Measurements

Latest capture replay: 420.100 MHz, TG30003, slot 1, skip=8015, duration=8000,
recorded center=420.350 MHz, voicecenter=420.08875 MHz, NAC2D2/BEE00/2D1.

| Metric | Before this pass | Mapping only | Final mapping + block tail |
|---|---:|---:|---:|
| Decoded frames | 248 | 126 | 132 |
| Fed frames | 250 | 128 | 134 |
| Concealment frames | 66 | 3 | 3 |
| Feed-gap counter | 6 | 1 | 4 |
| Vocoder resets | 6 | 1 | 1 |
| Submitted PCM seconds | 4.96 | 2.52 | 2.64 |

The feed-gap counter uses dibit-distance bands, not an air-interface voice
schedule. It also counts signaling intervals; it is not a direct count of
audible dropouts. Do not change thresholds merely to make this metric pass.
Results remain PASS_PARTIAL_AUDIO. The complete trace needs explicit
SDR_TOWN_P25_VALIDATION_LOG=1 and SDR_TOWN_P25_VALIDATION_ALL=1; deep trace
can also be enabled. Ordinary logging remains throttled and rotated.

Reference 103841 GUI replay (TG10120 slot1, skip18047, 12000 ms):
296 decoded / 306 fed frames, 289920 submitted samples, one feed-gap counter,
zero pending or dropped tail samples; normal completion. Its duration is
6.04 s versus 8.08 s before mapping correction. STT yields recognizable
sentences with transcription errors, not proof of every syllable or no gaps.

## Live GUI test

90-second real RTL-SDR capture on CC420.350 MHz:
`build/live_physical_mapping/20260917_070612_514_physical_mapping_420.35000MHz_startstop`.
This used the mapping correction before the subsequent block-tail fix.

- 90.048 s recorded IQ, zero ring overruns or epoch resets.
- Three follows, 102 audio-output events, 13 real-PCM top-ups.
- One slow voice worker; 21 logged output-underrun increases remain.
- Local faster-whisper STT on the live speaker WAV produced a coherent
  request/report and acknowledgment, followed by less certain fragments.
- WAV taps capture submitted speaker PCM, not acoustic output or silence
  generated in the hardware callback. STT is supporting evidence only.

## Verification and remaining work

Release build successful. Full test suite: 81,149 assertions in 233 cases.
P25 subset: 73,045 assertions in 126 cases. Geometry tests exhaust all 24
permutations, including the observed 0/2-only swap. Existing slot/security
tests remain green. Final GUI replay and CLI bounded replay exit normally.

Artifacts: `build/p25_060515_{allframes,physical,final_geometry}.*`,
`build/p25_103841_physical.*`, `build/p25_103841_geometry_gui.*`,
`build/live_physical_mapping_audit.json`, `build/tests_geometry_full.log`.

Still open: live callback underruns; residual missing valid frames/deduplication
provenance; corrected mapping candidate selection under marginal RF; and
human confirmation of continuous, intelligible speech across both slots and
multiple talkgroups. No continuous-audio completion or new release is claimed.
