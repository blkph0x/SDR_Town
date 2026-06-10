# SDR Town

**Professional multi-SDR monitoring, smart scanning, unencrypted voice/data playback, and advanced signal analysis for Windows.** (formerly MaulAudio Pro, rebranded as SDR Town)

Connect multiple HackRF (and other SoapySDR) devices in one clean GUI. Smart scanning with voice activity detection. High-quality analog + digital voice (NFM/AM/WFM/SSB + P25/DMR/NXDN clear). Weather satellite downlinks (NOAA APT). Dedicated multi-device audio output (listen on speakers while simultaneously piping to VB-Audio Cable or other virtual devices). Powerful Signal Analyzer with state-of-the-art (hybrid classical + modern) automatic modulation recognition for ASK, PSK, FSK, QAM and more — with decoding tools for unencrypted signals only.

**Status:** Early development (see [DESIGN.md](./DESIGN.md) for the full approved architecture and incremental PR plan).

## Key Features (Target)

- Multiple SDRs (HackRF focus, any SoapySDR device) with per-device configuration.
- Professional Qt6 GUI: spectrum/waterfall, active receivers table, hits, decoder panes.
- **Audio menu**: Configure and route to 2+ output devices simultaneously with independent volumes and test tones (perfect for speakers + VAC).
- Smart scanner: band plans, energy detection, classification hints, VAD gating, priority/history-aware scanning.
- Excellent analog voice with CTCSS/DCS, squelch, AGC, recording.
- Data: POCSAG pager decoding, basic AX.25.
- Weather sats: live NOAA APT image decoding and save.
- Digital voice: P25 Phase 1 (clear), DMR clear, NXDN (phased delivery).
- **Advanced Signal Analyzer**: pick freq + bandwidth → automatic modulation classification (ASK/OOK, PSK variants, QAM, FSK etc.), constellation, eye diagram, parameter estimation, generic + protocol-aware decoding of *unencrypted* signals. No decryption ever.
- Robust error handling (device removal, buffer health, etc.).
- JSON config, session restore, dark modern professional theme, keyboard-driven.

**Legal / Important:** This software is for **receiving only**. You are solely responsible for complying with all laws in your jurisdiction regarding radio reception, recording, and use of information. Do not use this tool to intercept private or encrypted communications where prohibited. The signal analyzer and decoders are designed exclusively for unencrypted / clear signals. Encrypted traffic will produce unintelligible output; no crypto attacks or decryption are implemented or attempted.

## Quick Start (Once Built)

1. Install HackRF drivers (Zadig → WinUSB for the HackRF device) and SoapySDR modules as needed.
2. Run SDR_Town.exe.
3. Devices → Rescan. Enable your SDR(s), set gains/sample rate.
4. Audio → Configure Output Devices... — check your speakers and a virtual cable (e.g. VB-Audio CABLE Input). Set volumes. Test tones.
5. Load or edit band plans, hit "Start Smart Scan".
6. Watch hits, promote interesting ones to persistent receivers, monitor audio on your chosen outputs.
7. For satellite: tune a NOAA freq (~137.1 MHz etc.) or let scanner find it; open APT decoder pane.
8. For advanced analysis: right-click spectrum or receiver → "Analyze in Signal Analyzer".

See DESIGN.md for detailed feature descriptions and roadmap.

## Building (Windows 10/11, x64) — For Developers Who Want to Build

**Important:** We do **not** commit any DLLs, Qt plugins, or built binaries to the repository.  
This is by design. All runtime dependencies are obtained automatically via:

- **vcpkg** (manifest mode in `vcpkg.json`) — provides spdlog, nlohmann-json, SoapySDR, rtlsdr, Catch2 + their DLLs.
- **Official Qt installer** + `windeployqt` (the standard Qt tool) — copies the exact Qt6 DLLs and plugin folders you need next to the .exe.

This is the clean, reproducible, and recommended way. You will not have to hunt for DLLs manually.

### Build Steps

```powershell
# 1. Clone the repo
cd "$env:USERPROFILE\Desktop\SDR_Town"

# 2. Submodules
git submodule update --init --recursive

# 3. Configure with vcpkg toolchain + your Qt path
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" `
  -G "Visual Studio 17 2022" -A x64

# 4. Build
cmake --build build --config Release -j

# 5. Deploy Qt runtime (critical step)
cd build/bin/Release
C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler

# 6. Run
.\SDR_Town.exe
```

See DESIGN.md for the full current feature list and phased implementation roadmap (per-receiver architecture, recording, decoders, weather sats, number stations, sat tracking, etc.). The roadmap is designed so you can say "let's get started" on the next item tomorrow.

**Important:** We do **not** commit any DLLs, Qt plugins, or built binaries to the repository.  
This is by design. All runtime dependencies are obtained automatically via:

- **vcpkg** (manifest mode in `vcpkg.json`) — provides spdlog, nlohmann-json, SoapySDR, rtlsdr, Catch2 + their DLLs.
- **Official Qt installer** + `windeployqt` (the standard Qt tool) — copies the exact Qt6 DLLs and plugin folders you need next to the .exe.

This is the clean, reproducible, and recommended way. You will not have to hunt for DLLs manually.

### Prerequisites
- Visual Studio 2022 (or Build Tools) with "Desktop development with C++".
- Qt 6.5+ installed via the official Qt Online Installer (https://www.qt.io/download-qt-installer). Choose **MSVC 2022 64-bit** + Widgets. Remember the path (e.g. `C:\Qt\6.11.1\msvc2022_64`).
- CMake 3.25+.
- Git.
- vcpkg (strongly recommended): clone and bootstrap it once.

### Build Steps

```powershell
# 1. Clone the repo (or open the folder)
cd "$env:USERPROFILE\Desktop\SDR_Town"     # or wherever you cloned it

# 2. Initialize submodules (liquid-dsp for advanced DSP, miniaudio header)
git submodule update --init --recursive

# 3. Configure with vcpkg toolchain + your Qt path
# (vcpkg will automatically install everything listed in vcpkg.json on first configure)
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" `
  -G "Visual Studio 17 2022" -A x64

# 4. Build (Release recommended for size/performance)
cmake --build build --config Release -j

# 5. Deploy Qt runtime files (this is the step that gives you all the Qt DLLs + plugins)
cd build/bin/Release
C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler

# 6. The folder now contains a runnable SDR_Town.exe + all required DLLs and folders
# (platforms/, imageformats/, tls/, SoapyRTLSDR.dll, rtlsdr.dll, libusb-1.0.dll, etc. come from vcpkg)
.\SDR_Town.exe
```

### What provides the DLLs?

- vcpkg (via the manifest in `vcpkg.json`) builds and provides: SoapySDR, rtlsdr, libusb, spdlog, etc. Their DLLs appear in the build output or `vcpkg_installed` (you don't commit them).
- `windeployqt` (official Qt tool) scans `SDR_Town.exe` and copies the precise Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll + all the plugin subfolders it actually needs.
- No manual DLL hunting required if you follow the steps above.

### For End Users (not developers)

Use the pre-built **installer** or **Portable.zip** from the GitHub Releases page. Those already contain every DLL and plugin bundled correctly.

See the Releases tab for the latest `SDR_Town-*.exe` installer and the portable zip.

**Qt Installer (already downloaded for you):**
The Qt Online Installer was downloaded to:
`C:\Users\Blkph0x\Downloads\qt-online-installer-windows-x64-online.exe`

Run it (it was launched), accept license, select a recent Qt 6.5+ (e.g. 6.7), **MSVC 2022 64-bit** component + at minimum Qt Base (Core/Gui/Widgets). It will take time and disk space.

After install, re-run the cmake configure with the correct CMAKE_PREFIX_PATH.

### Release Process + In-App Self-Update (state-of-the-art, safe)

**Producing a v0.2.0 release (exact asset names for the updater):**

1. `cmake --build build --config Release --target deploy`
2. `cd build/bin/Release`
3. `windeployqt SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler`
4. `cd ../../.. ; cmake --build build --config Release --target deploy` (re-purges stales)
5. `cpack -G NSIS -C Release` → produces `SDR_Town-0.2.0-win64-setup.exe`
   (optionally `cpack -G ZIP` for the portable zip)
6. Compute SHA256 of the setup.exe → `SDR_Town-0.2.0-win64-setup.exe.sha256`
7. Create `update.json` (see `update.json.example` + the exact structure in the user query / DESIGN.md) with real sha256 + size.
8. Create `SHA256SUMS.txt` (contains the .exe and .zip hashes).
9. Tag `v0.2.0`, attach the four assets (setup.exe, .sha256, update.json, SHA256SUMS.txt).

**In-app updater behavior (Help → Check for Updates or startup):**

- Fetches `https://github.com/Blkph0x/SDR_Town/releases/latest/download/update.json` (or the asset).
- Compares `QApplication::applicationVersion()` ("0.2.0").
- Never auto-downloads or installs.
- Consent dialog: version + size + notes link, buttons "Download and Install", "Later", "Skip this version", "View Notes".
- Downloads to `%LOCALAPPDATA%\SDR_Town\updates\...`, verifies SHA256, launches the NSIS (interactive or /S), then quits.
- 24 h cooldown on background checks; skipped version persisted; "You are up to date." only on manual checks.
- No GitHub tokens ever embedded. Public releases only.
- NSIS installer handles overwrite of exe/DLLs, recreates shortcuts, preserves your `%APPDATA%\SDR_Town` settings and receivers.json etc.

See DESIGN.md "Professional Updater" section for the full rationale, safety rules, and the recommended update.json shape. Packaging hygiene (clean staging, real icon, no stales) is a hard prerequisite and is enforced by the deploy target + CMake logic.

### vcpkg.json (deps)

The project includes (or will include) a `vcpkg.json` manifest. Common deps for early PRs: `spdlog`, `nlohmann-json`.

For SoapySDR: `soapy` port or manual (many users use the PothosSDR bundle which includes Soapy + modules + HackRF support).

See DESIGN.md "Technology Stack" and build notes for full details and troubleshooting.

## Project Layout (Evolving)

```
SDR_Town/
├── CMakeLists.txt
├── vcpkg.json
├── .gitignore
├── README.md
├── DESIGN.md                 # Living design document — update as we go
├── src/
│   └── main.cpp
├── resources/
├── external/                 # git submodules (liquid-dsp, miniaudio)
├── docs/
└── build/                    # (ignored by git)
```

## Contributing / Development Process

- Follow the approved plan in DESIGN.md.
- Work in small, reviewable increments matching the PR list.
- Keep DESIGN.md updated when architecture decisions change or new trade-offs are discovered.
- All audio paths and analyzers must enforce "unencrypted only" with clear UI messaging.
- Real hardware testing (HackRF or equivalent) is required for DSP/audio PRs.

## License & Disclaimer

To be determined (likely permissive open source with strong liability disclaimer). Until then, all rights reserved by the project authors. Use at your own risk for lawful purposes only.

---

**SDR Town** — Professional multi-SDR monitoring and signal analysis.

For the detailed architecture, algorithms, UI considerations, and exact implementation plan, read [DESIGN.md](./DESIGN.md).

The GitHub repo contains only source code. All runtime DLLs and Qt plugins are obtained at build time via vcpkg + the official Qt installer + windeployqt (see Building section above). Pre-built installers and portable zips (with everything bundled) are provided in the Releases.

## RTL-SDR Specific Setup (to fix "not picking up")
1. Plug in RTL-SDR dongle.
2. Run **Zadig** (https://zadig.akeo.ie/) **as Administrator**.
3. Select the RTL device (Interface 0 / RTL2832U), set driver to **WinUSB**, Install.
4. Unplug/replug the RTL-SDR.
5. In SDR Town, open Devices → Device Manager. It should now list the device with driver "rtlsdr".
6. Enable it, set sample rate (e.g. 2.048 MS/s), gain (try 15-25 for strong local FM stations), Apply.
7. The app can start real streaming when you enable a device.
8. If still not listed: make sure the Soapy "rtlsdr" module is present (the PothosSDR bundle at https://github.com/pothosware/PothosSDR is the easiest all-in-one for Windows users).

Common safe rates for RTL-SDR: 0.25, 1.024, 2.048, 2.4 MS/s.
If using multiple, or HackRF, similar driver setup (Zadig for HackRF too).

After setup, "Rescan" in dialog, enable, and spectrum/audio should pick up real signals when tuned (e.g. FM radio, ADS-B, etc.).

## CLI Mode for Testing (new)
Run the built exe with --cli (or -c) for a full command-line interface that exposes **all core features** without the Qt GUI. Perfect for on-the-fly testing, scripting, or when you just want to poke devices/streaming/demod/audio from the terminal.

`powershell
cd "C:\Users\Blkph0x\Desktop\maulaudio_pro\build\bin\Release"
.\MaulAudioPro.exe --cli
`

Commands (see help inside):
- list - devices (with driver/label/serial/enabled/rate/gain)
- enable <idx> / disable <idx> - start/stop real Soapy streaming (RTL-SDR, HackRF, etc.)
- tune <mhz> - set monitor freq + retune the active device (affects spectrum + demod)
- gain <idx> <db>, rate <idx> <msps>
- spectrum - live-ish text power summary + ASCII bar from real IQ
- audio list / audio enable <i> [i2 ...] / audio test [idx] / audio vol <idx> <0-100>
  - Full multi-device audio support (speakers + VB-Audio Cable etc.) even in CLI.
- mode nfm|wfm|auto|am|usb|lsb, set bw|lpf|squelch|gain|wfmde|pilotnotch <val>
- stats - live diagnostics: RF gain, mode/BW, IQ queue depth, last DSP us, audio ring fill %, underruns, rates/bitrate math, device CF/offset, RMS
- status, scan (enables all), help, quit

A background thread keeps demod + audio routing + spectrum updates running while you type commands.

Example RTL-SDR WFM test session (per audit: start with low gain + 120-150 kHz BW for strong locals):
`
list
enable 0
tune 98.9
mode wfm
set bw 150
gain 0 20
audio list
audio enable 0 1
stats
`
(While listening: vary `gain 0 15` / `gain 0 25` / `set bw 120` and watch stats counters for queue/DSP/underruns vs audio quality. Expect clean audio with gain 15-25.)

The same DeviceManager + AudioEngine + streaming/demod logic powers both GUI and CLI. All major phases (device management, real streaming, spectrum, high-quality WFM/NFM/AM/SSB with channelizer, multi-output audio, simple scan/tune) are now directly testable from the command line.

See DESIGN.md for the full feature/phase list + audit response log.
