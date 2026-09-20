# SDR Town 0.2.76 (experimental)

Paired with **FUBAR 1.1.39**. Copy `SdrTownControl-0.2.76-win64.dll` next to
`FUBAR.exe` as `SdrTownControl.dll`.

## Inmarsat (honest)

**Does:** band plans, tune listed L-band channels, slicer + ASCII ACARS/ADS-C
parse on fixtures, ADS-C marks on aircraft map, start/stop from GUI/CLI/FUBAR.

**Does not (still open):** unique-word/frame sync, FEC, verified C-assign,
Aero AMBE 8400, dual-SDR voice, live RF acceptance. Voice follow is **disabled**
and does not retune. Status `locked` is always false. See `docs/INMARSAT.md`.

## Also from 0.2.75

TLE WinHTTP + UI busy flag. Home lat/lon not in FUBAR HTTP. Inmarsat JSON in
the ZIP. No leftover 0.2.71 control DLL.

## Assets

Installer, portable ZIP, `SdrTownControl-0.2.76-win64.dll`, `update.json` + `.sig`,
`SHA256SUMS.txt`.
