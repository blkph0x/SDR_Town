# Production audit fixes — buildfix12

Applied against `phase2_afc_freeze_encrypted_follow_buildfix11_clean.zip` after the full code review pasted by the user.

## Fixed in this pass

1. **AUTO no longer falls through to WFM in the DSP core**
   - `Demodulator::demodulateToAudio()` now treats any leaked `DemodMode::AUTO` as conservative NFM.
   - The GUI DSP worker resolves AUTO with `chooseSmartModeAndBandwidth()` before calling the demodulator when not in P25 voice mode.
   - This makes GUI behavior closer to CLI behavior and prevents AUTO from using broadcast-FM deviation/bandwidth by accident.

2. **Phase 2 release gate tightened**
   - `P25LiveDecoder` no longer promotes every burst in a 12-burst window just because one MAC CRC validated.
   - `phase2AudioLock` is now promoted only by the burst's own CRC-valid MAC/ACCH decode.
   - `decodeP25Phase2VoiceBlock()` rejects voice codewords unless that burst has `macCrcValid`.
   - Late-entry voice-only ESS hypotheses are retained for diagnostics but no longer promote to active release state without MAC context.
   - This is intentionally conservative and may keep audio muted until MAC/ESS extraction is truly correct.

3. **MAC 0xF3 TDMA identifier offset sign fixed**
   - `P25Control.cpp` now uses the same sign semantics as the TSBK TDMA identifier path.
   - Non-zero MAC-derived TX offsets are no longer inverted.

4. **Stub IQ now follows actual displayed stream state**
   - Stub/fallback IQ uses `StreamState::currentRate` and `currentCenter` instead of hardcoded 2.4 MS/s / 100 MHz.
   - Published stub spectrum also reports the current rate/center.

5. **IQ capture finalized on shutdown**
   - `stopAllStreaming()` now stops/finalizes live IQ capture before joining the DSP worker and stopping devices.
   - This prevents partial SigMF/metadata when the app exits during capture.

6. **Updater manifest hardening**
   - `notes_url` is now allowlisted to the project GitHub release/blob paths.
   - Installer size must be present and <= 512 MiB.

## Still not fully solved in this cpp-only pass

- Full removal of P25 control-channel decode from the GUI timer still needs a controller/worker refactor.
- Soapy API serialization and stable-key stream ownership are larger DeviceManager changes.
- Audio ring SPSC cleanup likely needs `AudioEngine.h` layout changes.
- Per-burst MAC/ESS hard gate may keep Phase 2 clear audio muted until MAC extraction is fixed; this is safer than emitting unauthenticated/possibly encrypted audio.
