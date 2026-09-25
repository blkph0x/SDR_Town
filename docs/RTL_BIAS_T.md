# RTL-SDR Bias-T

Device Manager shows a Bias-T checkbox below the selected RTL-SDR row. It is
available only after the opened SoapyRTLSDR driver advertises the `biastee`
boolean setting. Start the receiver or use a full device rescan to discover
that capability. Discovery alone does not enable power.

## Electrical safety

Only enable on hardware with a switchable bias-T and a connected antenna/LNA
that accepts DC power. A generic RTL dongle or an R820T label is not evidence
of a bias-T circuit. Do not enable into a DC-shorted antenna or another RF
output. The GUI requires confirmation; the default is OFF. Vendor references:
[Blog V4 guide](https://www.rtl-sdr.com/V4/) and
[Blog V3 datasheet](https://www.rtl-sdr.com/wp-content/uploads/2018/02/RTL-SDR-Blog-V3-Datasheet.pdf).

The explicit setting is saved per device and restored at the next receiver
start. Changing it while receiving writes to the active device. A stopped,
previously probed receiver saves the choice for its next start. Changes are
refused while the device is opening or using simulated/no-hardware data.

The app attempts OFF on normal stop, failed open and recoverable RX fault,
without erasing the saved ON preference. A crashed or stuck driver cannot
guarantee power-off; unplug USB to guarantee it. Firmware/EEPROM force-on
configuration may also override software control.

## Status and CLI

`Driver reports ON/OFF` is a driver acknowledgment, **not a voltage measurement**.
The bundled driver caches its state and does not propagate the underlying
`rtlsdr_set_bias_tee` return code. Therefore no software-only test proves DC
voltage, load capacity or the presence of the physical circuit. The app reports
exceptions, invalid readback and mismatches instead of saving a failed change
as a success. An unconfirmed ON request triggers a best-effort OFF.

```text
devices probe
biastee 0 status
biastee 0 on
biastee 0 off
```

`on` is an explicit DC-power request, subject to the same safety requirements.
Use your actual device index. `status` is read-only and does not energize the
antenna. The setting is not exposed to the public web-control API.

## Physical acceptance

Use a supported model and safe test load/antenna setup. Check default OFF,
explicit ON/OFF, reception with the powered LNA, restart with saved intent,
and power removal on normal stop. A voltage measurement must use suitable RF
test equipment without shorting the connector. Record dongle model, driver
version, application status and the measured result. Automated contract and
GUI tests do not replace this check.

Implementation contract:
[SoapyRTLSDR Settings.cpp](https://github.com/pothosware/SoapyRTLSDR/blob/6ca357c15cbf676ff30eb8eb445d1e1eac17c136/Settings.cpp).
