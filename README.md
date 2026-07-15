# SDR Town

SDR Town is a Windows SDR receiver and analysis app for multi-device monitoring,
smart analog demodulation, P25 trunking experiments, high-resolution spectrum
work, classifier training capture, and safe self-updating tester builds.

Formerly MaulAudio Pro.

## Current Status

This project is in active experimental development. The app is useful for real
testing, but not every roadmap item is complete yet.

Working now:

- Qt 6 desktop GUI with spectrum, waterfall, device manager, audio controls,
  saved frequencies, P25 panes, and live receiver controls.
- RTL-SDR and SoapySDR device discovery, safe startup stubs, RF gain, sample
  rate, antenna selection, and persisted PPM correction.
- 64K spectrum/waterfall FFT option with zoom/color controls and presets.
- Analog demod paths for AUTO, WFM, NFM, AM, USB, LSB, and CW.
- Receiver-band squelch, local noise-floor tracking, auto squelch, RF signal
  level readouts, manual/auto bandwidth, audio LPF enable/cutoff, WFM
  de-emphasis, pilot notch, audio gain, and master volume.
- Multi-output audio through miniaudio, including speakers plus virtual cable
  workflows, independent output volume, and underrun/ring diagnostics.
- Deterministic signal classifier for mode/BW/filter recommendations.
- SigMF plus normalized waterfall ROI capture for future ML classifier
  training.
- P25 control-channel monitoring, known control-channel entry, talkgroup list,
  manual talkgroup verification, auto-follow grants, and muted control-channel
  monitoring.
- P25 Phase 1 frame/NID/TSDU/TSBK handling and clear IMBE voice backend hook.
- P25 Phase 2 TDMA grant metadata, channel/slot math, CQPSK/C4FM sync work,
  ISCH/MAC/ESS diagnostics, XOR mask application, late-entry logging, encrypted
  mute behavior, and gated clear AMBE validation.
- In-app updater manifest support with GitHub release assets and SHA-256
  installer verification.

Still experimental or incomplete:

- Phase 2 clear AMBE audio still needs more golden IQ capture validation across
  real networks before being called production-ready.
- The ONNX classifier backend is a placeholder. The deterministic classifier
  remains active while the trained model contract is finalized.
- SSB/CW are basic usable receive paths, not yet polished DX receiver chains.
- DMR, NXDN, DRM, pager, satellite, and deeper protocol decoders are roadmap
  modules.
- The updater validates GitHub release path and SHA-256, but a signed manifest
  or Authenticode verification is still planned for stronger trust.
- Native SDR driver isolation is still in-process. A future helper process is
  the best way to kill wedged Soapy/USB driver calls without risking the GUI.

## Downloads

Tester builds are published on GitHub Releases:

https://github.com/Blkph0x/SDR_Town/releases

Use either:

- `SDR_Town-X.Y.Z-win64-setup.exe` for the installer.
- `SDR_Town-X.Y.Z-win64-portable.zip` for a portable folder.

The in-app updater reads `update.json` from the latest GitHub release, verifies
the installer SHA-256, and then launches the installer only after user consent.

## Quick Start: GUI

1. Install the SDR driver stack.
   - RTL-SDR: use Zadig as Administrator, select the RTL device, and install
     WinUSB.
   - HackRF/other Soapy devices: install the correct USB driver and SoapySDR
     module.
2. Start `SDR_Town.exe`.
3. Open `Devices -> Rescan / Discover Devices`.
4. Enable a device, choose sample rate, gain, antenna, and PPM correction, then
   apply.
5. Open `Audio -> Configure Output Devices`, select your speakers and optional
   virtual cable, set volume, and test tones.
6. Tune a frequency from the main window or saved frequencies.
7. Choose AUTO or a specific mode, then adjust BW, LPF, squelch, RF gain, and
   volume while watching SIG/NF/SNR.

Useful GUI notes:

- Use AUTO mode first for mixed analog work. It uses band/frequency priors and
  live signal estimates to choose mode, bandwidth, and LPF suggestions.
- For UHF/VHF NFM voice, try 12.5 kHz channel BW and 3.0 kHz audio LPF.
- For broadcast WFM, try 120 to 200 kHz BW and keep RTL-SDR gain conservative
  at first, often around 15 to 25 dB for strong local stations.
- The squelch threshold is measured inside the receiver bandwidth, not across
  the whole visible spectrum.
- The green NF line is the receiver-band noise floor. Put SQ above NF and below
  the wanted SIG line.
- Disable audio LPF for data/decoder workflows where filtering would damage
  symbols.
- The P25 control-channel monitor mutes raw control-channel audio by design.

## P25 Workflow

GUI:

1. Tune or enter a known control channel.
2. Enable P25 CC monitoring.
3. Watch the P25 log window for sync, NID, TSBK, identifier, grant, slot, MAC,
   ESS, and encryption diagnostics.
4. Use the talkgroup table to verify talkgroups.
5. Enable auto-follow grants to follow clear calls when a valid voice grant is
   seen.
6. Encrypted calls are skipped/muted.

The `Grant Test` button performs the common tester workflow in one click: it
tunes the selected/known control channel, mutes raw control-channel audio,
enables auto-follow, and opens the P25 log window so grant and voice-gate
diagnostics are visible immediately.

CLI:

```powershell
.\SDR_Town.exe --cli
p25 monitor 420.250
p25 waitgrant 420.350 0 60 follow record=8 wav
p25 clearaudio 420.350 0 180
p25 followtest "C:\path\to\capture.sigmf-meta" 420.350 10000 followms=5000 tg=10609
p25 tgs
p25 follow 0
p25 voice
```

Phase 2 clear voice release is intentionally gated. Audio is released only when
the decoder has enough TDMA mask, MAC/ESS clear-state, and AMBE validation
evidence. Late entry into a call may show voice bursts before MAC/ESS state is
confirmed; the log will say that it is waiting rather than silently failing.
Grant updates without service options are still followed and queued even when
old talkgroup history says encrypted; the traffic-channel MAC/ESS path makes
the final clear/encrypted speaker decision. The unknown-grant AMBE probe is a
default-off lab diagnostic (`p25 voicetest ... probe`) and normal GUI/CLI audio
release stays muted until MAC/ESS/PTT proves clear. Encrypted ESS is treated as
final only when MAC CRC backs it or the voice decoder explicitly marks the call
encrypted.
For tester reports, add `record=8` to `p25 waitgrant`; the CLI saves a SigMF IQ
slice plus the final P25 voice-gate diagnostics under the app data
`iq_test_captures` folder before retuning back to the control channel.

One-command clear-audio field diagnostic:

```powershell
python src\tools\run_p25_live_clear_audio_diag.py --cc 420.350 --seconds 180 --record-seconds 8
```

That wrapper runs `p25 waitgrant` with follow, IQ capture, WAV capture,
validation logging, and then replays the saved IQ through the continuous Phase 2
voice harness. It writes `clear_audio_diag_summary.json`, the CLI transcript,
capture audit, replay transcripts, and decoded WAV files under
`build\p25_live_clear_audio_diag`.

Optional Phase 2 validation logging:

```powershell
$env:SDR_TOWN_P25_VALIDATION_LOG = "1"
$env:SDR_TOWN_P25_VALIDATION_REDACT = "1"
.\SDR_Town.exe --cli
```

Logs are written under the app data `logs` folder, rotated at 8 MB, and can be
redacted so raw symbols/AMBE bits and ESS key identifiers are not exported.

## CLI Help

Start CLI mode from the built release folder:

```powershell
cd C:\Users\Blkph0x\Desktop\maulaudio_pro\build\bin\Release
.\SDR_Town.exe --cli
```

Core commands:

```text
list | devices
enable <device>
disable <device>
tune <mhz> [rx]
mode <auto|wfm|nfm|am|usb|lsb|cw> [rx]
set bw <khz|auto> [rx]
set lpf <khz|on|off> [rx]
spectrum fft <4096|8192|16384|65536> [device]
gain <device> <db>
ppm <device> <ppm>
ppm cal <device> <known_mhz> [search_khz]
ppm apply <device> <known_mhz> [search_khz]
squelch <db> [rx]
stats | status [rx]
fav list
fav add <mhz> <mode> [tag]
fav tune <index> [rx]
fav del <index>
plans
classify [device] [rx]
capture <label> [rx]
model status
model load <path-to-model.onnx>
model unload
p25 [device]
p25 monitor <cc_mhz> [rx]
p25 tgs
p25 addtg <cc_mhz> <tgid> [tag]
p25 deltg <index>
p25 follow <talkgroup-index> [rx]
p25 tsbk <cc_mhz> <10-or-12-byte-hex-block>
p25 sync [device] [target_mhz] [ms]
p25 waitgrant <cc_mhz> [device] [seconds] [follow] [record[=seconds]] [wav] [tg=<id>]
p25 clearaudio <cc_mhz> [device] [seconds] [record=<seconds>] [tg=<id>]
p25 replay <sigmf-meta|sigmf-data|capture_dir> [target_mhz] [ms] [phase2] [skip=<ms>] [center=<mhz>] [nac=<id> wacn=<id> system=<id>]
p25 followtest <sigmf-meta|sigmf-data|capture_dir> <cc_mhz> [ms] [skip=<ms>] [center=<mhz>] [followms=<ms>] [tg=<id>]
p25 voicetest <capture_dir> <voice_mhz> [ms] [skip=<ms>] [slot=0|1] [tg=<id>] [clear|enc] [stream] [probe|noprobe] [wav=out.wav]
p25 voice
audio list
audio enable <output0> [output1 ...]
audio disable
rx add
help
quit
```

Example RTL-SDR WFM test:

```text
list
enable 0
tune 98.9
mode wfm
set bw 150
gain 0 20
audio list
audio enable 0
stats
```

Example UHF NFM voice test:

```text
enable 0
tune 476.4625
mode nfm
set bw 12.5
set lpf 3.0
squelch -95
gain 0 25
stats
```

## Build From Source

Prerequisites:

- Windows 10/11 x64.
- Visual Studio 2022 or Build Tools with Desktop development with C++.
- CMake 3.25 or newer.
- Git.
- vcpkg.
- Qt 6 MSVC 2022 64-bit with Widgets.

Configure and build:

```powershell
git submodule update --init --recursive

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" `
  -G "Visual Studio 17 2022" -A x64

cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure
```

Deploy Qt runtime files:

```powershell
cd build\bin\Release
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler
.\SDR_Town.exe
```

## Release Pipeline

Patch releases are used during active testing so the updater can be exercised
often.

1. Bump `project(SDR_Town VERSION X.Y.Z ...)` in `CMakeLists.txt`.
2. Build and run tests.
3. Commit source and documentation changes.
4. Run:

```powershell
.\scripts\release.ps1 -Version X.Y.Z -Channel experimental
```

The release script:

- Builds the Release deploy target.
- Runs `windeployqt` when available.
- Builds the NSIS installer with CPack.
- Creates the portable ZIP.
- Updates `update.json`.
- Updates `SHA256SUMS.txt`.
- Commits release manifests.
- Tags `vX.Y.Z`.
- Pushes `master` and the tag.
- Creates the GitHub release with installer, portable ZIP, manifest, and hashes.

## Project Layout

```text
CMakeLists.txt
include/
src/
tests/
docs/
scripts/
resources/
external/
update.json
README.md
DESIGN.md
```

Useful docs:

- `DESIGN.md` - broad architecture and implementation log.
- `docs/signal_classifier.md` - deterministic classifier and ML handoff.
- `docs/classifier_training_data_plan.md` - capture/training workflow.
- `docs/p25_phase2_release_gate.md` - Phase 2 validation gates.
- `docs/feature_alignment_next.md` - feature alignment checklist.

## Roadmap

Highest-value next steps:

- Split the large `src/main.cpp` into GUI, CLI, receiver, P25, settings, and
  audio-routing controllers.
- Add a separate SDR helper process for SoapySDR open/probe/stream isolation.
- Add signed update manifests or Authenticode verification.
- Validate Phase 2 clear AMBE audio on known good IQ captures.
- Finish a real ONNX classifier model contract and runtime binding.
- Improve SSB/CW with better sideband filtering, BFO behavior, AGC, and DX
  presets.
- Add production scanner workflows: scan lists, priorities, hold/lockout,
  recording, history, and per-talkgroup routing.
- Add DRM/DMR/NXDN/pager/satellite modules for unencrypted signals.

## Legal

SDR Town is receive-only software. You are responsible for following the laws in
your jurisdiction regarding radio reception, recording, and use of information.
Do not use this project to intercept private communications where prohibited.
Encrypted traffic is not decrypted; encrypted calls should be skipped or muted.

See `LICENSE.txt` for license terms.
