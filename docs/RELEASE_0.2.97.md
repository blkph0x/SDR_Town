# SDR Town 0.2.97 - Experimental RTL-SDR Bias-T

CI-built portable tester release. Extract the ZIP into its own directory and
run SDR_Town.exe. This is not a signed installer/in-app updater release; the
previous signed installer remains available. Existing settings are reused.

## RTL-SDR controls

- Device Manager now exposes RTL bias-T when the opened driver advertises it.
- Default OFF; enabling requires an explicit antenna DC-power confirmation.
- Saved per-device intent, checked live switching and best-effort OFF on stop,
  failed open and recoverable RX failure.
- CLI: `biastee <device> status|on|off`; status does not enable power.
- Failed writes/readback are reported. An unconfirmed ON attempts OFF.
- SDRplay continues to use its separate controls. No P25 DSP/audio changes.

Only use ON with a bias-T-equipped dongle and DC-safe antenna/LNA. Driver
support/readback is not a voltage measurement. Do not power a DC-shorted
antenna. Disconnect USB to guarantee power-off after a driver crash/hang.
See [RTL bias-T testing](https://github.com/blkph0x/SDR_Town/blob/master/docs/RTL_BIAS_T.md).

Local pre-release tests: 13/13 suites passed; adapter 32, GUI 20 and lifecycle
51 assertions passed. Attached RTL was probed read-only, reporting support
and OFF. Physical voltage/model acceptance remains with testers.

Project rules now require pushing completed changes, successful Actions,
public release publication, downloaded asset verification and smoke testing.
