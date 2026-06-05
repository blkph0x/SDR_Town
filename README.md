# MaulAudio Pro

**Professional multi-SDR monitoring, smart scanning, unencrypted voice/data playback, and advanced signal analysis for Windows.**

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
2. Run MaulAudioPro.exe.
3. Devices → Rescan. Enable your SDR(s), set gains/sample rate.
4. Audio → Configure Output Devices... — check your speakers and a virtual cable (e.g. VB-Audio CABLE Input). Set volumes. Test tones.
5. Load or edit band plans, hit "Start Smart Scan".
6. Watch hits, promote interesting ones to persistent receivers, monitor audio on your chosen outputs.
7. For satellite: tune a NOAA freq (~137.1 MHz etc.) or let scanner find it; open APT decoder pane.
8. For advanced analysis: right-click spectrum or receiver → "Analyze in Signal Analyzer".

See DESIGN.md for detailed feature descriptions and roadmap.

## Building (Windows 10/11, x64)

### Prerequisites
- Visual Studio 2022 (or Build Tools) with C++ desktop workload.
- Qt 6.5+ installed via the official **Qt Online Installer** (https://www.qt.io/download-qt-installer). Install Qt 6.x for MSVC 2022 64-bit (Widgets, etc.). Note the installation path (commonly `C:\Qt\6.7.0\msvc2022_64`).
- CMake 3.25 or newer.
- Git (for submodules).
- vcpkg (recommended): `git clone https://github.com/microsoft/vcpkg.git C:\vcpkg && C:\vcpkg\bootstrap-vcpkg.bat`.

### Steps

```powershell
# 1. Clone / open this folder
cd "$env:USERPROFILE\Desktop\maulaudio_pro"

# 2. Submodules (liquid-dsp for DSP, miniaudio for audio routing)
git submodule update --init --recursive

# 3. (If not already) run vcpkg manifest (already done in dev, but):
C:\vcpkg\vcpkg.exe install --triplet x64-windows --x-manifest-root=.

# 4. Create build dir
mkdir build; cd build

# 5. Configure - IMPORTANT: point to your Qt install from the Online Installer
# Example (change version/path to what the installer created, usually C:\Qt\6.7.x\msvc2022_64)
cmake .. `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2022_64;C:/Qt/6.6.3/msvc2022_64" `
  -G "Visual Studio 17 2022" -A x64

# 6. Build
cmake --build . --config Release -j

# 7. Run the awesome app
.\bin\MaulAudioPro.exe
```

**Qt Installer (already downloaded for you):**
The Qt Online Installer was downloaded to:
`C:\Users\Blkph0x\Downloads\qt-online-installer-windows-x64-online.exe`

Run it (it was launched), accept license, select a recent Qt 6.5+ (e.g. 6.7), **MSVC 2022 64-bit** component + at minimum Qt Base (Core/Gui/Widgets). It will take time and disk space.

After install, re-run the cmake configure with the correct CMAKE_PREFIX_PATH.

### vcpkg.json (deps)

The project includes (or will include) a `vcpkg.json` manifest. Common deps for early PRs: `spdlog`, `nlohmann-json`.

For SoapySDR: `soapy` port or manual (many users use the PothosSDR bundle which includes Soapy + modules + HackRF support).

See DESIGN.md "Technology Stack" and build notes for full details and troubleshooting.

## Project Layout (Evolving)

```
maulaudio_pro/
├── CMakeLists.txt
├── vcpkg.json
├── .gitignore
├── README.md
├── DESIGN.md                 # Living design document — update as we go
├── src/
│   └── main.cpp
├── resources/
├── external/                 # git submodules (liquid-dsp, mbelib)
├── docs/
└── build/                    # (ignored)
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

**MaulAudio Pro** — Professional tools for serious monitoring and analysis.

For the detailed architecture, algorithms, UI mock considerations, and exact incremental implementation plan, read [DESIGN.md](./DESIGN.md).

Issues and feedback welcome once the repo is public.
