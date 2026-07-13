# P25 independent traffic-channel source patch

This patch adds a first-pass sdrtrunk-style traffic-channel source path behind a P25 UI checkbox named **Traffic Source**.

## Why

The previous scanner-follow path retuned the primary control-channel receiver to the granted traffic channel. That means control decoding stops while the call is active, and short Phase 2 voice activity can be missed or decoded discontinuously. sdrtrunk instead asks its traffic-channel manager/source manager for a traffic channel source keyed by frequency/timeslot, while the control channel remains available for continuing grants and call state.

## Behavior

When **Traffic Source** is enabled and a resolved Phase 2 voice grant arrives:

1. The app first looks for an already-running SDR passband containing the traffic frequency.
2. If found, it creates an independent virtual `Receiver` on that device with its own IQ cursor, P25 decoder, AMBE state, and audio path.
3. If no running passband contains the traffic frequency, it tries to allocate/retune a second SDR device, never device 0.
4. If no independent RF source is available, it logs a clear reason and falls back to the older single-receiver retune-follow path.

The primary control-channel receiver remains muted and alive while the traffic receiver decodes voice. Control-channel worker results are no longer discarded while independent traffic is active, so grants/identifier updates can keep flowing.

## Safety

- The checkbox defaults from `QSettings` key `p25/independentTrafficSource` and currently defaults to enabled.
- Disabling the checkbox tears down the traffic receiver and returns to old retune-follow behavior.
- With one SDR and traffic outside the current passband, true independence is physically impossible; the code logs this and falls back.
- Explicit encrypted grant/ESS rules remain fail-closed in the existing audio gate.

## Files changed

- `include/Receiver.h`
- `src/main.cpp`
- `src/tools/verify_p25_independent_traffic_source.py`
