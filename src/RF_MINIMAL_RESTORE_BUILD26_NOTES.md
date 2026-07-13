# RF minimal restore build 26

Purpose: restore the DeviceManager RF start/open/read path to the original stable source while removing only the fake moving stub carrier.

Changes:
- DeviceManager.cpp is restored from the original uploaded DeviceManager(18).cpp baseline.
- Risky recent RF changes are removed: fail-closed mode, alternate Soapy enumerate open kwargs, session-generation handoff edits, and global Soapy serialization edits.
- The internal stub/no-hardware fallback no longer draws a synthetic sweeping RF carrier.
- Stub mode now publishes only flat low-level noise and logs explicit STUB/no-hardware warnings.

Expected behavior:
- If real Soapy hardware opens, real WFM/P25 signals should appear again.
- If hardware does not open, device may remain in safe stub mode but the waterfall is flat and logs say STUB/no-hardware.
- If Device Manager says stopped, inspect logs for Soapy make/setup/activate errors; the old risky patches are no longer involved.
