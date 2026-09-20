# SDR Town + FUBAR pairing

Evidence-only. Do not treat a GitHub Latest of one app as automatically matching the other.

## Roles

| App | Role | Control |
|-----|------|---------|
| **SDR Town** | RF in, demod / P25 / experimental satcom | Local HTTP JSON on `127.0.0.1:8765` |
| **FUBAR** | WASAPI capture, VOX WAVs, public website | Loads **`SdrTownControl.dll` beside `FUBAR.exe`** |

Visitors never connect to SDR Town. They use the FUBAR site; FUBAR uses the DLL.

## The control DLL (required)

1. SDR Town GitHub asset: `SdrTownControl-X.Y.Z-win64.dll` (must match that Town version).
2. Copy next to `FUBAR.exe` **as** `SdrTownControl.dll`.
3. Town portable ZIP also contains `SdrTownControl.dll` in the Town folder.

## Published GitHub Latest

| App | Latest tag | Ships / expects |
|-----|------------|-----------------|
| SDR Town | **v0.2.79** | `SdrTownControl-0.2.79-win64.dll` |
| FUBAR | **v1.1.40** | Matching 0.2.79 DLL. Website: P25/RDS/tones/SSTV Auto+mode list+Receive/Finish, Take control, SDRplay, satcom (no home lat/lon), aircraft, Inmarsat prototype (no voice follow). |

## Website vs Town GUI

| Feature | SDR Town | FUBAR website |
|---------|----------|---------------|
| Home lat/lon | Map + `observer set` | **Not shown, not set** (DEC-0104) |
| TLE refresh / ISS SSTV arm / satcom scan | Yes | Yes (Take control; Start uses `force=true`) |
| SDRplay IFGR/RFGR/notches | Device Manager + CLI | Radio tab when `status.sdrplay` present |
| Aircraft tracks | Local map | OSM of tracks only (no home center) |
| Inmarsat | Band plan, start/stop, log | Same; **no voice follow** (DEC-0105) |

## Remaining Inmarsat gaps (both apps)

Unique-word/FEC, verified C-assign, Aero AMBE, dual-SDR voice, live RF acceptance.
See `docs/INMARSAT.md`.

## Tester pairing for 0.2.79 / 1.1.40

1. Install Town **0.2.79** and FUBAR **1.1.40**.
2. Confirm `SdrTownControl.dll` beside `FUBAR.exe` is from 0.2.79.
3. Town listening; FUBAR on VB-CABLE; Tools → Settings → SDR Town control.
4. Close other SDRplay clients if using an RSP.
