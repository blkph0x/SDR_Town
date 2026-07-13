# AFC Freeze and Encrypted Follow Fix Notes

This build hardens two state-machine policies exposed during Phase 2 acquisition testing.

## AFC / PPM policy

- Live NFM AFC is now rate-limited so it cannot chase bursty adjacent-channel energy by multiple kHz in one update.
- A global P25 AFC freeze flag was added for voice-follow acquisition.
- When P25 voice follow arms, the current AFC estimate is frozen and logged.
- While frozen, AFC reports the frozen value instead of adapting underneath the TDMA acquisition loop.
- When auto-follow returns to the control channel, AFC is unfrozen and adaptation resumes.

Expected log lines:

```text
AFC lock: frozen offset=...kHz ppmDelta=... reason=phase2_voice_acq
AFC unlock: returned to control channel; live AFC adaptation resumed.
```

## Encrypted grant policy

- Auto-follow no longer follows grants already known to be encrypted, including Phase 2 diagnostic follows.
- If an encrypted re-grant/update arrives for the currently followed TG, the app immediately releases voice follow and returns to the control channel.
- If the voice channel itself proves encrypted via ESS or `SkippedEncrypted`, the app immediately returns to the control channel instead of staying parked on the voice frequency.

Expected log lines:

```text
Auto-follow skipped encrypted P25 TG ... staying on/returning to control channel.
P25 auto-follow TG ... proved encrypted on voice channel; returning to control channel immediately.
```

Audio remains hard-gated unless clear MAC/ESS/audio decode is proven.
