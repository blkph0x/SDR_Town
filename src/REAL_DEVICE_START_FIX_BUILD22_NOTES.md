# Build 22 — Real device start fix

This fixes the flat-white-noise/no-signal regression where GUI paths could start the internal stub instead of the real SDR.

## Changes

- Device Manager Apply now calls `startStreaming(i, true)` when a device is enabled.
- Add Receiver now calls `startStreaming(0, true)` instead of the default stub/safe mode.
- Keeps the previous fake-stub-carrier removal so fallback/no-hardware is obvious instead of pretending RF exists.

## Expected result

Strong local WFM stations should be visible again when hardware opens successfully. If hardware fails, logs should show STUB/no-hardware rather than a fake signal.
