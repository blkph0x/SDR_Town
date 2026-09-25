# SDR Town 0.2.96 - RSPdx device controls

Experimental tester release. Fixes a confirmed discovery/control path defect:
light enumeration left the RSPdx with a generic RX antenna and the live-open
path never replaced it with actual A/B/C capabilities. The panel now refreshes
from the opened receiver and shows driver acknowledgement or failure.

## Changes

- Dedicated antenna selector, correct B-only Bias-T policy and safe port-change order.
- Actual driver capabilities, not guessed setting support; active/first RSP selected in Device Manager.
- IF AGC survives startup, RF state edits and sample-rate restart.
- RFGR and optional RF selection alias cannot overwrite one another on startup.
- Auto hardware bandwidth actually returns the driver to automatic selection.
- GUI, CLI and local API reject invalid/unsupported settings and report failed readback.
- Main RF gain and remote Tune/gain routes target the selected SDRplay receiver.
- Scrollable controls, visible status, corrected RF state units and supported clock controls.
- No P25 DSP, vocoder, audio-buffer or sample/tune-loop changes.

## Checks

Native Qt control interaction, simulated RSPdx driver contract and asynchronous
open/control/stop/reopen tests cover all exposed RSPdx controls, invalid and
ignored writes, saved profile restoration, PPM and sample-rate restart.
Full Windows build/test and packaging results are recorded in BUILD_NOTES.md.
These tests do not claim physical RSP reception or electrical measurements.

## RSPdx Hardware Acceptance

1. Close other applications using the RSP. Install the SDRplay API/service and
   a matching 64-bit SoapySDRPlay3 module, then Rescan Devices.
2. Select RSPdx, open reception and Device Manager. Confirm A/B/C appears and
   status reports driver readback confirmation, not a generic RX input.
3. With a suitable antenna on B, select B. Bias-T must now be available.
   Only enable it for an antenna/LNA designed for DC power. Verify its actual
   effect; switching to A/C must turn it off. C requires tuning below 200 MHz.
4. Check RF state, IF AGC/manual IF, setpoint, each notch, IQ correction,
   hardware bandwidth/Auto, and HDR within the vendor's supported conditions.
   Reference clock OUT being disabled on RSPdx is expected.
5. Change sample rate, stop/restart, close/reopen. Confirm the saved port and
   AGC remain correct and reception continues. Test any local API/FUBAR controls
   used in your setup; FUBAR itself is unchanged.
6. Report model, API/module version, failing action, panel status and SDR Town
   log. Do not include credentials. No new IQ capture is required for a control error.

Driver readback is software confirmation, not an RF or bias voltage measurement.
RSPduo dual-channel settings/physical reception remain a separate acceptance gate.
