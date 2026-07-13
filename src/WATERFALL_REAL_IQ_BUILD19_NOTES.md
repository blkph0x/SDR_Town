# Build 19 — Waterfall Real-IQ / Stub Spike Fix

Fixes a regression where failed Soapy open/fallback stub IQ could look like a moving carrier in the waterfall, hiding the fact that the display was not receiving real RF.

Changes:
- Stop passing display-only `hardware`/`label` fields as primary `SoapySDR::Device::make()` kwargs.
- Try conservative Soapy open attempts in order: driver+serial, driver+index, driver-only.
- Log every Soapy make attempt and the one that succeeds.
- Replace the fallback stub's moving sine carrier with very low-level flat noise only.
- The waterfall should no longer show a fake sweeping/waving spike if real hardware fails to open.

Expected logs:
- `Background: Attempting Soapy make...`
- `Background: Soapy make succeeded...` for real RF.
- If hardware fails, the waterfall should look flat/noisy, not like a moving signal.
