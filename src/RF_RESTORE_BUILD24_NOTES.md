# RF restore build 24

Purpose: restore the RF/device path to the last known pre-waterfall-hardening behavior.

Changes:
- Restored DeviceManager.cpp from the stage-13 RF path, before the risky Soapy open identity and fail-closed/stub changes.
- Real open is back to the minimal known-good Soapy make kwargs: driver + serial when available.
- Restored the stub-first upgrade behavior so the UI does not appear dead while hardware opens.
- Preserved the tuned center frequency across start/restart instead of forcing 100 MHz.

This intentionally prioritizes getting real WFM/P25 signals visible again over the later no-stub/fail-closed diagnostics.
