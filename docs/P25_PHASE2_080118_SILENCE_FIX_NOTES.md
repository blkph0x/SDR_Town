# P25 Phase 2 total silence — capture 20260720_080118

## Capture

- Dir: `iq_test_captures/20260720_080118_011_iq_NFM_419_12500MHz_419.12500MHz_startstop`
- ~750.1 s gapless IQ @ 2.048 Msps, CC 419.125, SNR ~25 dB
- User report: **heard NO audio at all** during live follow (continuity round-2 binary)

## Full-chain classification

| Stage | Evidence |
|---|---|
| Tuner / IQ | Gapless; health `ok_gapless`; not RF dead |
| Grants / follow / retune | Clear + unknown grants followed; 33 retunes; TGs 10330/30302/30003/10301 |
| CQPSK / mask / VCW / AMBE | Live `decoding clear voice` + `gate=emit`; fed≈emit |
| ESS / enc fail-closed | Clear emits with `ess=clear`; encrypted windows fail-closed (`trusted-encrypted-drop`) |
| PCM → pending → push | **654** `P25 audio output` emits; ~85 s wall PCM @ 48 kHz; `activeOutputs=1` |
| AudioEngine | Speakers (GSX 1200 Pro) @ 48 kHz; underruns 2366→46257 |
| Offline IQ | Voicetest **PASS_PARTIAL** with high-RMS WAV (peak≈0.98, rms≈0.25) |

**Class: live path bug (ring/pending wipe), not “no clear grant / enc-only / acquire fail / invent-PLC”.**

Offline recoverable clear audio + live push logs with zero audible speech ⇒ playback continuity destroyed after push.

## Root cause (why / how)

1. **Why:** Empty-audio worker windows (opposite-slot / waiting clear grant — hundreds per session) treated as “no active call” and called `AudioEngine::clearBuffers()`, wiping just-pushed clear PCM and the silence bridge before the RT callback played them. Pending was also cleared when sustain looked inactive.
2. **How:** `gP25AudioLastSpeakerOutputMs` / sustain emit refresh were gated on `p25Phase2SpeakerOutputCanRefreshFollowActivity`. Failed refresh ⇒ sustain/preserve went false ⇒ empty windows nuked the ring. Continuity bridge could not help if the ring was zeroed every opposite dwell.
3. **Upstream:** Unchanged — CQPSK/slot/ESS/AMBE/gate still produced clear PCM (offline WAV proves it).
4. **Downstream:** Ring preserve + always-refresh speaker/sustain on real push ⇒ empty windows no longer race-destroy playback; invent-PLC still denied; enc fail-closed unchanged.
5. **Outcome:** Live should play the same clear islands offline already recovers; underruns should climb far more slowly during clear emits.

## Fix

- `p25Phase2ShouldPreserveLivePlaybackBuffers` — preserve pending/ring across normal Phase-2 empty windows when sustain/bridge/recent emit/queued audio says continuity is live.
- Empty-window path: **do not** `clearBuffers()` / clear pending while preserve is true or ring/pending still hold audio.
- On every successful speaker push: always update `gP25AudioLastSpeakerOutputMs` + sustain emit; keep follow-hold tune activity gated separately.

## Proof artifacts

- `build/p25_vt_080118_tg10301_s1.wav` / `.txt` — 13.38 s, duty 0.535, PASS_PARTIAL
- `build/p25_vt_080118_tg30302_419375.wav` / `.txt` — 8.74 s, duty 0.397, PASS_PARTIAL
- Catch2 `[p25]` after rebuild

## User verify

1. Run rebuilt Release GUI, auto-follow on 419.125.
2. On clear calls expect audible speech (islands OK; no total mute).
3. Watch `underruns=` climb slowly during emits; `ringFill` should not slam to 0 immediately after every `gate=emit`.
4. Encrypted traffic must stay silent (fail-closed).
