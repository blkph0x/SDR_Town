# SDR Town 0.2.75 (experimental)

Patch on **0.2.74**. Same pairing rules: copy `SdrTownControl-0.2.75-win64.dll` next to `FUBAR.exe` as `SdrTownControl.dll`. FUBAR GitHub Latest is still **1.1.33** (ships the 0.2.66 DLL).

## Fixes

- **TLE Refresh** actually finishes: WinHTTP with 15 s timeouts (CelesTrak stations/weather/amateur). Live fetch parses **ISS 25544**. The satcom UI no longer overwrites “TLE: downloading…” every 500 ms, so the button no longer looks dead.
- **Home lat/lon stays in SDR Town.** FUBAR/website HTTP status no longer includes observer coordinates (`publicStatusJson`, aircraft status omits `centerLat`/`centerLon`). Set home on the Town map or `observer set` only (DEC-0104).
- **Package:** `data/inmarsat/*.json` is copied into deploy staging. Leftover `SdrTownControl-0.2.N-win64.dll` files are not globbed into the ZIP (T-0041). Use this release’s `SdrTownControl.dll` / `SdrTownControl-0.2.75-win64.dll` for FUBAR.

## Still in from 0.2.74

SDRplay Soapy profile (host API + SoapySDRPlay3, not in this ZIP), satcom/observer map/Doppler, aircraft map, Inmarsat prototype, tuner lease, CLI. P25 DSP unchanged. REQ-P2.2…P2.6 still open.

## Honest limits (unchanged)

- Inmarsat is a prototype (no unique-word/FEC).
- SDRplay vendor API/module are installed on the PC, not shipped here.
- FUBAR **1.1.33** website does not have Satcom/Inmarsat/Aircraft/SDRplay tabs (those are FUBAR 1.1.38 source, not GitHub Latest).

## Assets

Installer, portable ZIP, `SdrTownControl-0.2.75-win64.dll`, `update.json` + `.sig`, `SHA256SUMS.txt`.
