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

## Previously Recorded Pairing

This is historical evidence, not a live GitHub Latest query. Town 0.2.90 ships
its versioned control DLL; the DLL ABI is unchanged. SSTV live requests now
accept optional rfMode (default Auto), and status exposes RF route identity.
FUBAR integration was not re-tested and its website has no separate RF selector.
Use the 0.2.90 DLL when testing against Town 0.2.90 and report both app versions.

| App | Recorded tag | Ships / expects |
|-----|------------|-----------------|
| SDR Town | **v0.2.80** | `SdrTownControl-0.2.80-win64.dll` |
| FUBAR | **v1.1.41** | Matching 0.2.80 DLL. Website SSTV list includes HamDRM digital. |

## Website vs Town GUI

| Feature | SDR Town | FUBAR website |
|---------|----------|---------------|
| Home lat/lon | Map + `observer set` | **Not shown, not set** (DEC-0104) |
| TLE refresh / ISS SSTV arm / satcom scan | Yes | Yes (Take control; Start uses `force=true`) |
| SDRplay IFGR/RFGR/notches | Device Manager + CLI | Radio tab when `status.sdrplay` present |
| Aircraft tracks | Local map | OSM of tracks only (no home center) |
| Inmarsat | Native Aero, IQ replay, speaker/WAV, ADS-C map | Band plan, start/stop, log; **no voice follow** (DEC-0105) |

## Native Aero in Town 0.2.92

Town now has native continuous/burst framing/FEC/CRC, a separate Aero codec,
speaker/WAV output and an offline validated ADS-C map. GUI/CLI replay reference
tests pass. FUBAR was not changed or re-qualified with these additions; its
website does not expose the new replay, decoder/audio or offline-map controls.
The control DLL ABI is unchanged; this tester package includes the versioned
0.2.92 DLL. Do not interpret that as a new end-to-end FUBAR qualification.

Remaining: clear-conversation RF acceptance, call-end/security lifecycle and
automatic follow, dual-SDR voice/data, photos and website control integration.
See `docs/INMARSAT.md` and `docs/RELEASE_0.2.92.md`.

## Tester pairing for 0.2.80 / 1.1.41

1. Install Town **0.2.80** and FUBAR **1.1.41**.
2. Confirm `SdrTownControl.dll` beside `FUBAR.exe` is from 0.2.80.
3. Town listening; FUBAR on VB-CABLE; Tools → Settings → SDR Town control.
4. Close other SDRplay clients if using an RSP.
