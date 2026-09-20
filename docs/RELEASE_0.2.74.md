# SDR Town 0.2.74 (experimental)

Paired with FUBAR for website control. Ship matching `SdrTownControl-0.2.74-win64.dll`.

## Ready for tester hardware

- **SDRplay (SoapySDRPlay3):** discovery splits API / module / RSP; Device Manager live IFGR/RFGR, AGC, notches, Bias-T (blocked on Hi-Z/C), HDR, clock OUT, Dual Tuner 2 MS/s clamp, host diversity (listen path uses the composite). Opening Device Manager no longer stops a live listen; **Rescan** still does.
- **WFM 0.2.73:** soft IQ catch-up no longer resets demod (98.1 buzz / waterfall freeze).
- **Satcom / ISS:** click the home map (or Shift-click aircraft map, or `observer set`) for lat/lon used by pass prediction, Doppler, and ISS SSTV arm. Doppler is TEME→ECEF with Earth rotation. TLE download is off the GUI thread (`tle refresh` / Refresh TLE).
- **Aircraft map:** local 1090 (opt-in) + OpenSky. `POST /v1/aircraft/refresh` fetches. Shift-click sets home.
- **CLI automation:** `observer`, `tle`, `satcom`, `inmarsat`, `aircraft`, `sdrplay`, `devices rescan`. Use `force` to take the tuner from a live listen session.

## Honest limits

- **Inmarsat** is a prototype (band plans + ACARS/ADS-C parse). No unique-word/FEC/Aero AMBE claim.
- Satcom/Inmarsat **cannot retune** while listen/P25 owns the device unless `force=true`.
- AX.25/APT/ADS-B are experimental. Meteor LRPT / commercial sat / dual-SDR Inmarsat voice are not in this build.
- P25 DSP is unchanged from 0.2.66.

## Assets

Installer, portable ZIP, `SdrTownControl-0.2.74-win64.dll`, `update.json` + `.sig`, `SHA256SUMS.txt`.
