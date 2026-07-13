# Buildfix20 - Real device open + waterfall source recovery

This build addresses the regression where the waterfall showed no real RF signal after the fake stub carrier was removed.

## Changes

- Keeps the buildfix19 removal of the fake moving stub carrier.
- Adds a safer real-device open path:
  - re-enumerates Soapy devices at open time,
  - matches the selected device by driver/serial first, then label/hardware when serial is absent,
  - passes the exact original Soapy enumerate result into `SoapySDR::Device::make()`.
- Keeps conservative fallbacks:
  - driver + serial,
  - driver only.

## Why

If the app shows a flat waterfall/no strong WFM/P25 signal after buildfix19, it is almost certainly running the internal stub/no-hardware path. The previous fallback stub used a strong fake moving sine, which made hardware failures look like RF. This build keeps that fake signal removed but improves hardware open so the waterfall uses real IQ again.

## Logs to check

Look for:

```text
Background: Attempting Soapy make for device ... using { ... }
Background: Soapy make succeeded for device ...
```

If every attempt fails, the waterfall will be flat/noise because it is not receiving real device IQ.
