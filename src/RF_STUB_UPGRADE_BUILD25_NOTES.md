# RF Stub Upgrade Build25 Notes

Purpose: fix the regression where the device could end up marked stopped after the synthetic stub carrier was removed.

Changes:
- Kept the no-fake-carrier stub behavior from build24a.
- Changed the stub-to-real upgrade path so a slow/stuck temporary stub thread no longer aborts a successful real Soapy open.
- If the stub thread does not report stopped within the handoff window, it is detached and the real hardware RX thread is started on a fresh session generation.
- The detached stub remains generation-guarded and should exit once it observes the mismatch/stop flag.
- The device now stays active as `live hardware` after real open succeeds instead of being marked `driver stuck, restart recommended` / stopped.

Expected result:
- No moving fake wave in the waterfall.
- Device should not flip to Stopped merely because the stub handoff was slow.
- If real Soapy open succeeds, strong WFM stations should be visible again.
- If hardware open fails, logs should show the Soapy open failure while the fallback remains flat/no-carrier.
