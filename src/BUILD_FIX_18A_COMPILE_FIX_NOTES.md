# Build Fix 18A — main.cpp compile fix

Fixes a compile break introduced in buildfix18 where a P25 stage diagnostic string referenced `iq.size()` outside the IQ buffer scope.

## Change

- Replaced the out-of-scope `iq.size()` diagnostic with `live.stats.inputSamples`, which is already populated by `P25LiveDecoder::processIq()`.
- No behavior changes to decoder, follow state, or audio gating.

## Expected result

MSVC error C2065 `iq`: undeclared identifier at `main.cpp(5048)` should be resolved.
