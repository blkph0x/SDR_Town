# P25 Phase 2 stability hotfix

This hotfix backs the realtime decoder away from the expensive recovery paths that can stall the GUI/DSP worker.

Changes:

- Disabled the multi-target Phase 2 offset probe in the live DSP loop. The probe ran several complete P25 decodes per window and could freeze on false Phase 2 telemetry from a selected control channel.
- Restored non-blocking receiver/state/DSP mutex behavior for voice arm. If the DSP worker owns the decoder, the existing retry timer defers the arm instead of blocking the Qt thread.
- Made non-deep ACCH recovery use direct CRC plus cheap erasure RS only. Full Berlekamp-Massey/unknown-symbol recovery remains in the code for deep/offline use, but is no longer used for every live ACCH hypothesis.
- Forced realtime Phase 2 burst decode to use non-deep ACCH hypotheses.

Goal: selecting a control channel and clicking UI controls must remain responsive. Once live logs are stable again, re-enable deeper recovery behind an explicit debug/offline capture path rather than in the hot loop.

## almost_stable reference - 2026-08-01 field pass

User field report: Phase-2 clear voice reached clear continuous audio with only a few double-ups and short garble bursts remaining. Treat this local source state as the `almost_stable` reference point for diagnostics only; it is not a release-channel change.

Follow-up hardening in this pass:

- Pending AMBE release now drains in chronological stream/absolute order before using key-quality rank as a tie-breaker.
- Pending AMBE release now refuses mixed dual-slot windows that lack current trusted MAC/session proof, preventing queued frames from being released through a window already classified as `phase2-dual-slot-untrusted-garble`.
- Follow-up latency/cutout hardening: strong fresh selected-slot AMBE can now feed when the companion TDMA slot is fully rejected/accounted and target clear evidence is present. The stricter mixed-slot rule still applies to pending AMBE drain, so stale queued frames do not re-enter through MAC-dead dual-slot windows.

## 20260808_034136 dual-slot blocky/shonky

User: more voice than prior capture but nothing intelligible — blocky/shonky.

Metrics: ~177 gate=emit, ~94s PCM pushed, 63 dual-slot emits (~38s) **all** with `p2mac=0/x`, sticky `ess=clear`, companion `rej>=opp`. Security action almost always `explicit-clear-grant-traffic-clear-release`. Prior dual-slot gate trusted companion-accounted + sticky ESS without this-window MAC → wrong-epoch AMBE to speaker.

Fix:
- `p25Phase2DualSlotUntrustedGarbleWindow` requires this-window MAC (`phase2MacCrcValid` or `phase2TargetMacCrcValid`); sticky ESS no longer passes.
- `trustedClear` fail-closes on dual-slot untrusted for every path (not only latch).
- Dual-slot MAC-dead drops window PCM only (`dual-slot-untrusted-garble-drop`) without collapsing Clear → unknown.
- Feed path `dualSlotUntrustedNow` ignores sticky ESS; dual-slot without MAC does not feed mbelib.
