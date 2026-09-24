# SDRplay support (SoapySDRPlay3)

SDR Town talks to SDRplay RSP hardware through **SoapySDR + SoapySDRPlay3**
(API 3.x). There is no separate native `sdrplay_api` streaming path; this keeps
IQ rings, retunes, and P25 voice-follow on the same DeviceManager stack as
RTL-SDR and HackRF.

## Install (Windows)

1. Install **SDRplay API 3.x** from [sdrplay.com](https://www.sdrplay.com/downloads/).
2. Install a **64-bit SoapySDRPlay3** build compatible with the app's Soapy
   ABI and your model (module typically `sdrPlaySupport.dll` under
   `lib\SoapySDR\modules0.8`). An existing PothosSDR/radioconda install may
   provide it; not every distribution includes this module or supports newer RSPs.
3. Use **Rescan Devices**. Restart SDR Town after replacing a DLL that was
   already loaded; follow any reboot instruction from the vendor installer.

Device Manager shows the current discovery status. CLI: `sdrplay status`.

The standalone release does not redistribute the proprietary SDRplay API or
third-party SoapySDRPlay3 binaries. It discovers the normal SDRplay, PothosSDR
(both Program Files layouts), radioconda, and application-local locations at
runtime. The API DLL and Soapy module must be installed on the same machine as
the app.

### Troubleshooting “RSP not connected”

The September 25 local repair (DEC-0122, not yet a published release) reports:

- **Driver registered (not a hardware detection):** the module actually registered
  SDRplay find/open functions. A DLL loading without a valid registration no longer
  counts as success. The device table separately shows enumerated hardware;
  enumeration alone does not prove that a stream can be opened.
- **Service not installed / stopped:** install the official SDRplay API/service
  or start its Windows service. SDR Town only reads this state; it never installs,
  restarts, or changes services automatically.
- **Driver unavailable:** the status/log identifies missing API exports, Windows
  DLL loading errors, rejected Soapy ABI, or absent factory registration.
  Install a compatible API/module and retry Rescan. A working loaded module is
  retained until app exit, rather than replaced while a receiver uses it.

If the driver is registered and the service running but no RSP appears, check
USB connections/vendor drivers and close other applications using the RSP.
Do not replace an SDRplay USB driver with the RTL-SDR Zadig/WinUSB setup.

The repair adds application-local nested module layouts, both registry views
and per-user API registration, active/per-user radioconda, `SDRPLAY_API_DIR`,
`SDRPLAY_ROOT`, `SOAPY_SDR_ROOT`, `POTHOS_ROOT`, and `SOAPY_SDR_PLUGIN_PATH`.
It no longer prepends every x86/ARM/conda directory to the global process PATH.
Windows validates API DLL architecture. API paths use Unicode APIs; Soapy 0.8's
ANSI module loader uses a lossless native/short path, or reports that an ASCII
module installation path is required. Linux/macOS retain Soapy's platform search
paths; those OS builds have not been validated by the Windows loader tests.

From the PothosSDR installation directory, these commands are useful before
reporting a problem:

```text
SoapySDRUtil --info
SoapySDRUtil --find="driver=sdrplay"
SoapySDRUtil --probe="driver=sdrplay"
```

Also run the app's own runtime diagnostic, since SoapySDRUtil may load a different
Soapy installation:

```powershell
.\SDR_Town.exe --cli --no-remote-diagnostics --cmd "sdrplay status"
```

Report the exact RSP model, SDR Town version, status text and latest
`%APPDATA%\SDR_Town\SDR Town\logs\sdr_town.log`. The read-only
`scripts/sdrplay_preflight.ps1` source-tree helper can collect an independent
API/service/Soapy installation report. Never include unrelated credentials.

`sdrplay_api_Open() failed` points to the vendor runtime/service; an empty find
can also mean no module registered or no accessible hardware. It is not evidence
of a missing modulation setting. Only one client can usually own an RSP at a time.

### Repair verification

Windows Release app/core/GUI builds pass. Five process-isolated loader
tests cover false DLL success with rejected ABI, missing API exports, retry and
concurrent reuse, Unicode API paths, and real nested portable module discovery.
All five pass. They use fake DLLs outside deployment, never fake received samples. Packaging
checks reject those fixtures and unapproved vendor DLLs.

The first full CTest run passed; a repeat exposed an intermittent, unchanged
SSTV active-producer detach test stall (ISS-0019). All remaining core/GUI/backend
checks pass. This local repair is not a claim of fully qualified release status.

Local Pothos module 0.3.0-206b241 registers; its API open fails because this PC
has no SDRplay service installed. No physical RSP is attached here. Acceptance
on each affected model still requires a tester's successful Rescan, open/tune,
stream, stop and reopen. The RF/gain/sample-rate and P25 pipelines are unchanged.

## Full feature coverage matrix

| Feature | RSP1B | RSPdx / dx-R2 | RSPduo | In SDR Town |
|---|---|---|---|---|
| Single tuner / dual independent tuners | Single | Single | Dual (2× MSi001) | Yes — Dual Tuner expands to **two device rows** (ch0/ch1) |
| Frequency 1 kHz–2 GHz | Yes | Yes | Yes (each tuner) | Yes — from Soapy `getFrequencyRange` |
| 14-bit ADC / up to 10 MSPS | Yes | Yes | 10 MSPS single / **2 MSPS×2 dual** | Yes — Dual Tuner **clamped to 2 MS/s** |
| Instantaneous BW up to 10 MHz | Yes | Yes | 10 / 2×2 MHz | Yes — Soapy bandwidth list + `setBandwidth` |
| HDR mode (&lt; ~2 MHz) | — | Yes | — | Yes — `hdr_ctrl` |
| Antenna ports | 1× SMA (`RX`) | **A / B / C** | T1 50Ω / T1 Hi-Z / T2 50Ω | Yes — live `setAntenna` from Soapy names |
| Port A SMA 1 kHz–2 GHz | SMA | Yes | Tuner 1 50 Ω | Yes |
| Port B SMA 1 kHz–2 GHz | — | Yes | Tuner 2 50 Ω | Yes |
| Port C BNC 1 kHz–200 MHz | — | Yes | — | Yes — listed as `Antenna C` |
| Hi-Z long-wire input | — | — | Tuner 1 Hi-Z (not Dual) | Yes — Single/Master modes |
| Broadcast MW/FM notch | Yes | Yes | Yes | Yes — Soapy **`rfnotch_ctrl`** (combined; API has one `rfNotchEnable`) |
| DAB notch | Yes | Yes | Yes | Yes — `dabnotch_ctrl` |
| Bias-T ~4.7 V | Yes (SMA) | A/B SMA | 50 Ω SMA | Yes — blocked on **Hi-Z / Antenna C** |
| IFGR + RFGR (two-tier gain) | Yes | Yes | Yes | Yes — manual + main RF Gain → RFGR |
| Hardware IF AGC + setpoint | Yes | Yes | Yes | Yes — AGC toggle + `agc_setpoint` |
| IQ correction | Yes | Yes | Yes | Yes — `iqcorr_ctrl` |
| Reference clock OUT | — | (see note) | Yes (`extRefOutputEn`) | Yes — Soapy **`extref_ctrl`** labeled as clock OUT |
| Dual Tuner concurrent streams | — | — | Yes | Yes — shared Soapy device, two RX sessions |
| Spatial diversity / null-steer | — | — | Hardware-coherent Dual Tuner | Yes — **host DSP** equal-gain sum / null-steer |
| Multi-unit coherent arrays | — | — | Clock daisy-chain | Partial — clock OUT enable only (no array sync UI) |

### Clock I/O honesty

SoapySDRPlay3 exposes a single boolean **`extref_ctrl`**. On RSPduo / RSP2 this maps to
API **`extRefOutputEn`** (reference **clock output** for daisy-chain). There is **no**
separate Soapy key for external clock **input** / GPSDO lock. RSPdx MCX behaviour
depends on the installed SoapySDRPlay3 build — the UI labels the control from the
model and documents this limit in the tooltip.

### MW / FM notch honesty

The public API exposes one **`rfNotchEnable`** (broadcast notch). Soapy mirrors that as
**`rfnotch_ctrl`**. Separate MW vs FM toggles are **not available** through Soapy or the
published structs; the UI labels the control “Broadcast MW/FM notch (combined)”.

## Antenna selection (live)

SoapySDRPlay3 exposes the exact port names SDR Town uses:

| Model | `listAntennas` names |
|---|---|
| RSP1 / 1A / 1B | `RX` |
| RSP2 | `Antenna A`, `Antenna B`, `Hi-Z` |
| RSPdx / RSPdx-R2 | `Antenna A`, `Antenna B`, `Antenna C` |
| RSPduo Single/Master | `Tuner 1 50 ohm`, `Tuner 1 Hi-Z`, `Tuner 2 50 ohm` |
| RSPduo Dual Tuner | ch0: `Tuner 1 50 ohm`; ch1: `Tuner 2 50 ohm` (no Hi-Z) |

Device Manager antenna combo applies **live** via `setLiveAntenna`. Port descriptions and Bias-T eligibility update when the selection changes.

## RSPduo modes

Chosen at Soapy `Device::make` via kwargs (`ST` / `DT` / `MA` / `MA8` / `SL`). Changing mode requires Rescan + stream restart.

- **Single Tuner:** up to 10 MS/s, Hi-Z available.
- **Dual Tuner:** two Device Manager rows, **max 2 MS/s each**, shared device handle.
- **Master / Slave:** for multi-process / second-app sharing (Soapy enumeration).

## Diversity / null-steer (Dual Tuner)

With both Dual Tuner channels streaming, Device Manager / CLI can enable host-side
combining:

| Mode | Combine |
|---|---|
| `sum` | `A + B · amp · e^(jθ)` — spatial diversity / coherent sum |
| `null` | `A − B · amp · e^(jθ)` — null-steer / interference cancel |
| `off` | disabled |

Enabling creates a **Diversity composite** device row and retargets the listen
path to that composite so speaker/spectrum use A±B, not tuner A alone.
Center frequency on the composite retunes both source channels together.
Phase (degrees) and B amplitude (linear) are live-adjustable.

## CLI

```
sdrplay status
sdrplay show <device>
sdrplay gains <device>
sdrplay agc <device> on|off
sdrplay ifgr <device> <dB>
sdrplay rfgr <device> <dB>
sdrplay bw <device> <Hz|0>
sdrplay antenna <device> Antenna A
sdrplay set <device> <key> <value>
sdrplay diversity <device> off|sum|null [phase_deg] [amp_b]
```

## Limits (honest)

- **Separate MW vs FM notch toggles:** not exposed (one `rfnotch_ctrl` / `rfNotchEnable`).
- **External clock IN / GPSDO lock as a distinct control:** not exposed by Soapy (only clock OUT via `extref_ctrl`).
- **Multi-RSP coherent array orchestration:** beyond clock-OUT enable.
- **TX:** not supported (RSP is RX-only).
- Physical acceptance depends on API + Soapy module versions on the host.
