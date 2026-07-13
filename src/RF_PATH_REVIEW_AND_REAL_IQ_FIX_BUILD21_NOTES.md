# RF path review + real-IQ recovery fix — buildfix21

User symptom: waterfall changed to weak white/noise-only floor and strong WFM/P25 signals disappeared.

Review finding:
- The displayed signal is consistent with the low-level internal stub/no-hardware path, not real RF.
- buildfix19 deliberately removed the old fake moving stub carrier, so hardware-open failure now appears as flat weak noise instead of a convincing moving spike.
- buildfix20 attempted to reopen the selected SDR using the full Soapy enumerate kwargs. That can pass driver-specific/display keys back into `Device::make()` and may open an unexpected RTL mode/path or fail into stub.
- `startStreaming()` also reset `currentCenter` to 100 MHz during the stub-first handoff, so a real device could momentarily open/tune at 100 MHz instead of the user's selected center until a later retune arrived.

Fixes:
- Restored minimal known-good Soapy open attempts: `driver+serial`, then `driver-only` fallback.
- Removed full enumerate-kwargs make path.
- Preserved the existing tuned center across start/real-upgrade instead of resetting to 100 MHz.
- Added RF health logs that explicitly say whether samples are REAL IQ or STUB/no-hardware IQ.
- Kept fake moving stub carrier removed so failed hardware cannot masquerade as RF.

Logs to check:
- `Background: Soapy make succeeded...`
- `Background upgrade: Started real Soapy streaming...`
- `RF health dev N REAL iq: samples=... rms=... max=...`

If instead you see:
- `hardware failed, using stub`
- `RF health dev N STUB/no-hardware iq...`

then the app is not receiving SDR hardware samples and no real stations will be visible.
