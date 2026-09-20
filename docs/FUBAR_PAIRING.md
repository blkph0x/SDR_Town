# SDR Town + FUBAR pairing

Evidence-only. Do not treat a GitHub Latest of one app as automatically matching the other.

## Roles

| App | Role | Control |
|-----|------|---------|
| **SDR Town** | RF in, demod / P25 / experimental satcom | Local HTTP JSON on `127.0.0.1:8765` |
| **FUBAR** | WASAPI capture, VOX WAVs, public website | Loads **`SdrTownControl.dll` beside `FUBAR.exe`**, then talks to Town |

Visitors never connect to SDR Town. They use the FUBAR site; FUBAR uses the DLL.

## The control DLL (required)

This is the companion bridge, not an optional extra:

1. SDR Town GitHub release asset: `SdrTownControl-X.Y.Z-win64.dll` (must match that Town version).
2. Copy it next to `FUBAR.exe` **as** `SdrTownControl.dll` (that exact name; FUBAR `LoadLibrary`s it).
3. Town portable ZIP also contains `SdrTownControl.dll` in the Town folder (same binary for that Town build).

v0.2.74 ZIP additionally contains leftover `SdrTownControl-0.2.71-win64.dll` from a Release-folder glob (T-0041). **Do not use that 0.2.71 file with Town 0.2.74.** Use `SdrTownControl.dll` from the same ZIP, or the GitHub `SdrTownControl-0.2.74-win64.dll` asset.

## Published GitHub Latest (2026-09-20)

| App | Latest tag | Ships / expects |
|-----|------------|-----------------|
| SDR Town | **v0.2.74** | Town APIs through 0.2.74; GitHub asset `SdrTownControl-0.2.74-win64.dll` |
| FUBAR | **v1.1.33** (`037d290`) | Ships `SdrTownControl.dll` **from Town 0.2.66**. Website: P25 subtitle, RDS/tones/SSTV tabs, tune/mode/LPF/gain/P25 CC, leave-P25 on analog. **No** Satcom / Inmarsat / Aircraft / SDRplay website panels in this tag. |

A tester who installs **both GitHub Latest** gets Town 0.2.74 radio + FUBAR 1.1.33 website. Core listen/P25/tune still pairs if they **replace** FUBAR’s DLL with the 0.2.74 asset. Website satcom/Inmarsat/aircraft/SDRplay panels need FUBAR source **1.1.38** (in the FUBAR working tree, **not** GitHub Latest).

## FUBAR working tree vs Town 0.2.74

FUBAR `CMakeLists.txt` / CHANGELOG / website in the working tree are **1.1.38**, dirty, not tagged. That tree already mirrors:

| Town | FUBAR tree (unreleased) | Website |
|------|-------------------------|---------|
| 0.2.67 `POST /v1/sdrplay` | 1.1.34 | Radio tab RSP controls when `status.sdrplay` is non-null |
| 0.2.68 `/v1/satcom/*` | 1.1.35 | Satcom scanner tab |
| 0.2.69 observer/TLE/passes/arm | 1.1.36 | Lat/lon text, TLE refresh, ISS SSTV arm |
| 0.2.70 `/v1/aircraft/*` | 1.1.37 | Aircraft OSM map |
| 0.2.71 `/v1/inmarsat/*` | 1.1.38 | Inmarsat tab |

## Gaps to communicate (do not invent as done)

**SDR Town 0.2.74, not in FUBAR 1.1.33 (published):**

- Satcom / Inmarsat / Aircraft website tabs
- SDRplay IFGR/RFGR/notch/Bias-T/HDR/diversity website controls
- Observer clickable OSM map (Town GUI has it; FUBAR 1.1.38 tree has **text** lat/lon only)
- Tuner `force` on satcom start: FUBAR 1.1.38 `postSatcom('start')` does **not** send `force:true`. If Town is already listening, satcom start returns 409 until Take-control + force or Town-side `satcom start force`
- Inmarsat honesty: Town labels prototype / no unique-word/FEC. FUBAR 1.1.38 website copy still says “AMBE voice follow”
- Town CLI (`observer`, `tle`, `satcom`, …) is Town-only; FUBAR has its own `--cli`

**FUBAR 1.1.33 published, still valid with Town 0.2.74 + matching 0.2.74 DLL:**

- VOX / website live listen / Now playing / P25 subtitle
- RDS, UHF tones, SSTV picture status (read-only)
- Take-control lease: freq, mode, BW, LPF, volume, RF gain, P25 CC, leave-P25 analog

**Neither app (do not claim):**

- Meteor LRPT / SatDump
- Inmarsat unique-word/FEC/production voice
- SDRplay API/module inside the Town installer (host install)
- Authenticode
- FUBAR 1.1.38 GitHub release (not published)

## Tester pairing for 0.2.74

1. Install Town **0.2.74**.
2. Install FUBAR **1.1.33** (current GitHub) **or** a local 1.1.38 build if they want satcom/aircraft/Inmarsat/SDRplay **website** tabs.
3. Copy `SdrTownControl-0.2.74-win64.dll` → `SdrTownControl.dll` next to `FUBAR.exe`.
4. Town listening; FUBAR on VB-CABLE; Tools → Settings → SDR Town control.
5. Close other SDRplay clients if using an RSP.
