# SDR Town 0.2.74 (experimental)

Companion: [FUBAR](https://github.com/blkph0x/FUBAR). Copy this release’s
`SdrTownControl-0.2.74-win64.dll` next to `FUBAR.exe` as `SdrTownControl.dll`.
FUBAR **GitHub Latest is 1.1.33** (ships the 0.2.66 DLL; P25/RDS/tones/SSTV +
Take control). Satcom / Inmarsat / Aircraft / SDRplay **website** tabs need
unreleased FUBAR 1.1.38 source. Pairing matrix: `docs/FUBAR_PAIRING.md`.

## Ready for tester hardware

- **SDRplay (SoapySDRPlay3):** discovery splits API / module / RSP; Device Manager live IFGR/RFGR, AGC, notches, Bias-T (blocked on Hi-Z/C), HDR, clock OUT, Dual Tuner 2 MS/s clamp, host diversity (listen path uses the composite). Opening Device Manager no longer stops a live listen; **Rescan** still does.
- **WFM 0.2.73:** soft IQ catch-up no longer resets demod (98.1 buzz / waterfall freeze).
- **Satcom / ISS:** click the home map (or Shift-click aircraft map, or `observer set`) for lat/lon used by pass prediction, Doppler, and ISS SSTV arm. Doppler is TEME→ECEF with Earth rotation. TLE download is off the GUI thread (`tle refresh` / Refresh TLE).
- **Aircraft map:** local 1090 (opt-in) + OpenSky. `POST /v1/aircraft/refresh` fetches. Shift-click sets home.
- **CLI automation:** `observer`, `tle`, `satcom`, `inmarsat`, `aircraft`, `sdrplay`, `devices rescan`. Use `force` to take the tuner from a live listen session.

## Honest limits

- **SDRplay vendor files are not in this ZIP/installer.** Testers need API 3.x + SoapySDRPlay3 already on the PC (`docs/SDRPLAY.md`).
- **Inmarsat** is a prototype (band plans + ACARS/ADS-C parse). No unique-word/FEC/Aero AMBE claim. This package does **not** include `data/inmarsat/*.json`; the UI uses the built-in 4f2 fallback (T-0041).
- Satcom/Inmarsat **cannot retune** while listen/P25 owns the device unless `force=true`.
- AX.25/APT/ADS-B are experimental. Meteor LRPT / commercial sat / dual-SDR Inmarsat voice are not in this build.
- P25 DSP is unchanged from 0.2.66. REQ-P2.2…P2.6 remain open.
- Portable ZIP also contains leftover `SdrTownControl-0.2.71-win64.dll` (T-0041 glob). **Do not** give that 0.2.71 file to FUBAR. Use `SdrTownControl.dll` in the ZIP or the GitHub **0.2.74** DLL asset.

## Assets

Installer, portable ZIP, `SdrTownControl-0.2.74-win64.dll`, `update.json` + `.sig`, `SHA256SUMS.txt`.
