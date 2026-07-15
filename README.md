# SDR Town

**SDR Town** is a Windows desktop SDR receiver and analysis application for multi-device monitoring, analog demodulation, P25 Phase 1/2 trunking experiments, spectrum/waterfall work, classifier training capture, CLI automation, and GitHub-based self-updates for tester builds.

| | |
|---|---|
| **Current version** | **0.2.34** (experimental channel) |
| **Platform** | Windows 10/11 x64 |
| **UI** | Qt 6 GUI + interactive CLI |
| **License** | See `LICENSE.txt` |
| **Releases** | https://github.com/Blkph0x/SDR_Town/releases |
| **Repo** | https://github.com/Blkph0x/SDR_Town |

Formerly *MaulAudio Pro*. Branding, binaries, installer, AppData paths, and release assets all use **SDR Town** / `SDR_Town`.

---

## Current state (honest, code- and field-verified)

This is **active experimental software**. It is useful for real RF testing and development. It is **not** a finished production trunking scanner.

### Working well today

| Area | Reality |
|------|---------|
| **Analog demod** | **WFM, AM, NFM** are solid everyday paths. **AUTO** picks mode/BW/LPF suggestions from band priors + live signal estimates. **USB / LSB / CW** exist and produce audio; they are basic receive chains, not polished DX receivers. |
| **GUI** | Spectrum + waterfall, device manager, multi-output audio, saved frequencies, P25 control/talkgroup panes, live SIG/NF/SNR/AFC readouts, IQ capture, training capture, Help → Check for Updates / Report Issue. |
| **Devices** | RTL-SDR (primary path) and SoapySDR discovery/open. Safe stub path when hardware is absent. RF gain, sample rate, antenna, PPM (manual + cal/apply). |
| **Audio** | miniaudio multi-output (speakers + virtual cable), per-output enable/volume, ring-fill and underrun counters. Master volume in GUI. |
| **P25 Phase 1** | Control-channel C4FM path: frame sync, NID, TSDU/TSBK trust, grants, talkgroup list. Clear IMBE backend via mbelib when frames validate. |
| **P25 Phase 2** | Full experimental TDMA pipeline: superframe/ISCH, XOR mask (NAC/WACN/SysID), ACCH/MAC/ESS hypotheses, Voice2/Voice4 → AMBE 3600×2450 (mbelib), one-RTL traffic retune + return-to-control, security gate (encrypted mute, clear only with proof). |
| **Updater** | Fetches `update.json` from GitHub **latest** release, SHA-256 verifies installer, user consent only—no silent install. |
| **CLI** | Full interactive shell + one-shot `--cli --cmd "..."`. Replay/voicetest/followtest/waitgrant for lab and field diagnostics. |

### P25 Phase 2 clear audio — about ~50% of the time

As of **v0.2.34**, field testing reports **clear Phase 2 voice roughly half the time** on live systems (good enough to understand speech when it works; still often blocky, intermittent, wrong-slot, or gated when it does not).

What that means in practice:

- **When it works:** grant → follow → mask + slot → clear proof (MAC/ESS/PTT or established clear carry) → AMBE frames feed mbelib → speaker audio. Continuous/joined enough to be useful.
- **When it fails or degrades:** low/zero MAC CRC despite high superframe/mask counts; opposite-slot thrash; late-entry wait; frame order/dedupe gaps (logs show `expVcw` / `fed` / `emitPcm` / `gaps`); irregular 20 ms cadence → blocky or “almost” speech; return-to-control before the call ends.
- **Security is intentional and strict:** unknown grants do **not** open the speaker by default. Encrypted grants/ESS stay muted. Lab-only late-entry/unknown probe is **default off** (`kP25Phase2AllowUnknownGrantFieldAudioProbe = false`); enable only via explicit CLI/GUI flags for diagnostics.

Do **not** treat Phase 2 as production-ready. Treat it as a working experimental decoder under active hardening toward SDRTrunk-class continuity.

### Incomplete or experimental (do not oversell)

- **ONNX classifier backend** is a placeholder; the **deterministic** classifier is what runs.
- **Smart Scan** button is present (PR6-era foundation)—not a full production scanner (no priority lists, lockout, hold, multi-TG routing product yet).
- **DMR / NXDN / DRM / pager / satellite** modules are roadmap only—not implemented as working decoders.
- **Updater** trusts GitHub release path + SHA-256 only; Authenticode / signed manifest not done.
- **SDR open/stream** is in-process (no separate helper process yet)—wedged USB/Soapy can still affect the app process.
- **SSB/CW** are functional basics, not contest-grade AGC/filtering chains.

---

## Downloads and updates

Tester builds: https://github.com/Blkph0x/SDR_Town/releases

| Asset | Purpose |
|-------|---------|
| `SDR_Town-X.Y.Z-win64-setup.exe` | NSIS installer (silent `/S` supported) |
| `SDR_Town-X.Y.Z-win64-portable.zip` | Portable folder |
| `update.json` | In-app updater manifest (version, URL, sha256, size) |
| `SHA256SUMS.txt` | Release hashes |
| `*.exe.sha256` | Per-installer hash file |

**How shipping works (code path):**

1. Bump `project(SDR_Town VERSION …)` in `CMakeLists.txt`.
2. `scripts/release.ps1 -Version X.Y.Z -Channel experimental` builds deploy + windeployqt + CPack NSIS + portable zip, rewrites `update.json` / `SHA256SUMS.txt`, commits, tags `vX.Y.Z`, pushes, uploads assets with `gh release create`.
3. App `UpdateManager` fetches  
   `https://github.com/Blkph0x/SDR_Town/releases/latest/download/update.json`  
   then downloads only HTTPS GitHub release installer URLs and verifies SHA-256 before launch.

---

## Quick start — GUI

1. Install drivers (RTL-SDR: Zadig → WinUSB as Administrator; other Soapy devices per vendor).
2. Run `SDR_Town.exe` (installer or portable).
3. **Devices → Rescan / Discover Devices**, enable device, set sample rate / gain / antenna / PPM, apply.
4. **Audio → Configure Output Devices** (speakers ± virtual cable), test tone if needed.
5. Tune frequency; choose **AUTO** or **WFM / NFM / AM / USB / LSB / CW**.
6. Adjust BW, LPF, squelch, RF gain, master volume while watching **SIG / NF / SNR / AFC**.

### GUI feature map (from code)

| Panel / control | Function |
|-----------------|----------|
| Spectrum + waterfall | Live spectrum; FFT sizes include 4096–65536; zoom/color controls |
| Active receivers | Multi-receiver foundation: add/remove, mode, BW (incl. Auto BW), LPF on/off + cutoff, squelch + Auto, gain, set & tune device |
| SIG / NF / SNR / AFC | Live RF metrics from DSP worker |
| Master volume + Outputs… | Global volume; multi-output routing |
| Saved frequencies | Add current / tune / delete / refresh |
| Capture Training Sample | SigMF + classifier training tile |
| Start/Stop IQ Capture | Rolling IQ capture for lab/debug (AppData under SDR Town) |
| P25 Control Channels | Scan CC, Monitor CC, **Grant Test**, Add known CC, Refresh, P25 Log |
| Auto Follow Grants | Follow clear voice grants from control channel |
| Traffic Source | Independent traffic-source path when available (one-RTL retune semantics) |
| Talkgroup table | Add / Verify / Follow TG / Delete / Refresh / Add to Scanner |
| Help | Check for Updates; Report Issue; My Submitted Issues (with remote diagnostics config) |

**Grant Test:** tunes selected/known CC, mutes raw control audio, arms auto-follow, opens P25 log—standard field grant/voice-gate workflow.

**P25 audio policy (GUI + CLI):** control-channel audio is muted by design. Voice audio opens only after security/session proof (or established clear carry). Encrypted calls are skipped/muted.

### GUI launch flags (selected)

```text
SDR_Town.exe
SDR_Town.exe --freq 476.4625 --start-device --default-audio
SDR_Town.exe --p25-cc 420.350 --gui-auto-follow --gui-default-audio
SDR_Town.exe --p25-cc 420.350 --grant-test --p25-log
SDR_Town.exe --gui-start-iq-capture --capture-label field1 --capture-seconds 30
```

Also: `--gui-device`, `--gui-p25-monitor`, `--p25-late-entry-audio-probe` (lab), `--gui-startup-self-test`, `--gui-require-clear-audio`, `--gui-exit-after-ms`, `--allow-multiple` (lab only; default single-instance).

---

## Quick start — CLI

```powershell
# Interactive shell
.\SDR_Town.exe --cli

# One command then exit
.\SDR_Town.exe --cli --cmd "p25 waitgrant 420.350 0 60 follow"
.\SDR_Town.exe --version
.\SDR_Town.exe --help
```

### Core CLI commands

| Command | Usage |
|---------|--------|
| `list` / `devices` | Enumerate devices |
| `enable <i>` | Enable + start streaming on device *i* |
| `disable <i>` | Stop streaming |
| `tune <mhz> [rx]` | Tune receiver (or `tune <rx> <mhz>` style where supported) |
| `mode <auto\|wfm\|nfm\|am\|usb\|lsb\|cw> [rx]` | Demod mode |
| `set bw <khz\|auto> [rx]` | Channel bandwidth |
| `set lpf <khz\|on\|off> [rx]` | Audio LPF cutoff / enable |
| `spectrum fft <4096\|8192\|16384\|65536> [dev]` | Waterfall FFT size |
| `gain <i> <db>` | RF gain |
| `ppm <i> <ppm>` | PPM correction |
| `ppm cal <i> <known_mhz> [search_khz]` | Calibrate PPM against known carrier |
| `ppm apply <i> <known_mhz> [search_khz]` | Apply PPM calibration |
| `squelch <db> [rx]` | Squelch threshold (receiver band, not whole spectrum) |
| `stats` / `status [rx]` | Live diagnostics (gain/mode/BW/IQ/DSP/ring/underruns) |
| `fav list` / `fav add` / `fav tune` / `fav del` | Saved frequencies |
| `plans` | Built-in band auto-mode plans |
| `classify [dev] [rx]` | Deterministic mode/BW/filter classifier |
| `capture <label> [rx]` | SigMF + training tile |
| `model status` / `model load <onnx>` / `model unload` | ONNX placeholder API (deterministic remains active) |
| `audio list` | Playback devices |
| `audio enable <out0> [out1 …]` | Enable outputs |
| `audio disable` | Stop outputs |
| `rx add` | Add another receiver entry |
| `help` | Command list |
| `quit` / `exit` | Leave CLI |

### P25 CLI commands

| Command | Usage |
|---------|--------|
| `p25 [dev]` | Likely P25 control-channel candidates |
| `p25 tgs` | Discovered/verified talkgroups |
| `p25 addtg <cc_mhz> <tgid> [tag]` | Manually verify a TG |
| `p25 deltg <index>` | Delete saved TG row |
| `p25 monitor <cc_mhz> [rx]` | Tune muted control channel |
| `p25 follow <index> [rx]` | Follow unencrypted active TG voice (when known) |
| `p25 tsbk <cc_mhz> <hex>` | Ingest decoded TSBK bytes |
| `p25 sync [dev] [target_mhz] [ms]` | Live C4FM/CQPSK sync/NID/TSBK check |
| `p25 waitgrant <cc_mhz> [dev] [seconds] [follow] [record[=s]] [wav] [tg=id]` | Wait for grant; optional follow + IQ/WAV capture |
| `p25 clearaudio <cc_mhz> [dev] [seconds] [record=s] [tg=id]` | Field clear-audio diagnostic (wait/follow/save) |
| `p25 replay <sigmf-meta\|data\|dir> [target_mhz] [ms] [phase2] [skip=ms] [center=mhz] [nac= wacn= system=]` | Offline IQ through P25 decoder |
| `p25 followtest <capture> <cc_mhz> [ms] [skip=] [center=] [voicecenter=] [followms=] [tg=]` | Replay CC grants + voice follow/gate test |
| `p25 voicetest <capture> <voice_mhz> [ms] [skip=] [center=] [offsethz=] [tg=] [slot=0\|1] [nac=…] [clear\|enc] [stream] [probe\|noprobe] [wav=]` | Phase 2 voice IQ through speaker gate |
| `p25 voice` | Voice backend status + Phase 2 validation log path |

### CLI examples

**WFM broadcast**

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

**UHF NFM**

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

**P25 grant follow + record**

```text
enable 0
p25 monitor 420.350
p25 waitgrant 420.350 0 120 follow record=8 wav
```

**Offline Phase 2 voice gate**

```text
p25 voicetest "C:\path\to\capture_dir" 421.850 8000 slot=1 tg=30302
```

**One-shot from shell**

```powershell
.\SDR_Town.exe --cli --cmd "p25 sync 0 420.350 5000"
```

### P25 validation logging (optional)

```powershell
$env:SDR_TOWN_P25_VALIDATION_LOG = "1"
$env:SDR_TOWN_P25_VALIDATION_REDACT = "1"
.\SDR_Town.exe --cli
```

Logs under AppData `logs` (rotated). With redact, sensitive symbols/AMBE/ESS identifiers are scrubbed.

**Cadence compare fields** (DSP / DEEP DIAG logs): `expVcw`, `fed`, `emitPcm`/`emit`, `gaps`, `lastAbs` — expected voice codewords vs frames fed to mbelib vs PCM emitted vs order gaps. Use these when diagnosing blocky/repeated/out-of-order audio.

---

## P25 workflow (GUI + CLI)

1. Tune or add a known **control channel**.
2. **Monitor CC** (audio muted) or `p25 monitor`.
3. Watch **P25 Log** / CLI for sync, NID, TSBK, grants, slot, mask, MAC, ESS.
4. Enable **Auto Follow Grants** or use `p25 waitgrant … follow`.
5. On grant: one-RTL path retunes (or independent traffic source), arms Phase 2 decode, gates speaker until clear proof.
6. On end/idle/timeout: return to control, clear voice state.

**Phase 2 gate summary (do not relax casually):**

- Emit clear audio only with defined proof: session release / ESS clear / MAC PTT-ACTIVE path / established clear carry after prior proof.
- Encrypted grant or encrypted ESS → mute.
- Unknown security → queue/wait, not free-play (except explicit lab probe flags).
- Opposite TDMA slot VCWs are ignored for release on the followed slot.

Field captures and logs typically live under:

```text
%APPDATA%\SDR_Town\SDR Town\
```

(IQ captures, logs, P25 logs, settings.)

---

## Remote diagnostics (alpha)

When `remote_diagnostics.json` is present (or `--diag-url` / env), the app can send **compact JSON events** (startup, stalls, P25 gate counters, audio metrics)—not full IQ/PCM dumps.

```powershell
# Collector
powershell -ExecutionPolicy Bypass -File scripts\start_remote_diag_server.ps1 -Port 8787 -Host 0.0.0.0

# App
.\SDR_Town.exe --diag-url http://host:8787/ingest --diag-token <token>
# or: SDR_TOWN_DIAG_URL / SDR_TOWN_DIAG_TOKEN
# disable: --diag-off
```

Help → Report Issue / My Submitted Issues integrate with the collector when configured. See `scripts/install_remote_diag_task.ps1` for a logon task.

---

## Build from source

**Prerequisites:** Windows 10/11 x64, VS 2022 C++, CMake ≥ 3.25, Git, vcpkg, Qt 6 MSVC 64-bit Widgets, liquid-dsp / Soapy as per `vcpkg.json` and CMake, optional mbelib submodule for P25 voice.

```powershell
git clone https://github.com/Blkph0x/SDR_Town.git
cd SDR_Town
git submodule update --init --recursive

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" `
  -G "Visual Studio 17 2022" -A x64

cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure
```

Deploy Qt:

```powershell
cd build\bin\Release
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler
.\SDR_Town.exe
```

---

## Release pipeline

```powershell
# After bumping CMakeLists.txt project VERSION to X.Y.Z and a clean build/test:
.\scripts\release.ps1 -Version X.Y.Z -Channel experimental
```

Produces and uploads: NSIS setup, portable ZIP, `update.json`, SHA files. Optional: `-RemoteDiagnosticsUrl http://…` injects packaged diag config without committing tokens.

---

## Project layout

```text
CMakeLists.txt          Version + deploy/CPack
include/                Headers (Demod, DeviceManager, P25*, AudioEngine, UpdateManager, …)
src/                    Implementation (main GUI/CLI, P25LiveDecoder, Demod, …)
src/tools/              Python audits, verify scripts, remote_diag_server, clear-audio diag
tests/                  C++ unit tests + PowerShell CLI/GUI harnesses
scripts/                release.ps1, remote diag helpers, classifier training helpers
docs/                   P25 gates, classifier plans, comparisons
resources/              App icon
external/               mbelib, liquid-dsp, miniaudio (as vendored/submodule)
update.json             Live updater manifest for current release series
DESIGN.md               Architecture / history (verbose; may lag code)
```

Useful docs (may be denser than this README):

- `docs/p25_phase2_release_gate.md` — Phase 2 security/release gates  
- `docs/P25_SDRTRUNK_FULL_COMPARISON.md` — parity notes vs SDRTrunk  
- `docs/signal_classifier.md` — deterministic classifier  
- `DESIGN.md` — long-form design log (not always current on every edge)

---

## Direction — what’s next (priority order)

1. **P25 Phase 2 continuity → production clarity**  
   Raise clear-audio reliability well past ~50%: continuous 20 ms frame feed (order + amount), MAC recovery when sf/mask are high, slot stability, joined PCM without repeats/gaps. Keep security gates strict. Use `expVcw/fed/emit/gaps` logs + capture audits.

2. **Operational hardening**  
   Split mega-`main.cpp` into GUI / CLI / P25 / settings modules; SDR helper process isolation; reduce freeze/retune edge cases on one-RTL follow.

3. **Trust & packaging**  
   Authenticode or signed update manifests beyond raw SHA-256.

4. **Classifier**  
   Real ONNX contract + training pipeline; keep deterministic path until then.

5. **Analog polish**  
   SSB/CW AGC/filtering; WFM/NFM/AM already primary “daily drivers.”

6. **Scanner product features**  
   Priority TGs, lockout, hold, recording history, multi-output per TG—after Phase 2 audio is reliable.

7. **Other digital modes**  
   DMR/NXDN/etc. only after trunking audio quality is stable.

---

## Architecture snapshot

```text
DeviceManager (RTL/Soapy IQ)
    → DSP worker + optional P25 voice worker
        → Demod (analog)  → AudioEngine (miniaudio)
        → P25LiveDecoder  → grant/follow state machine
            → Phase 2 mask / MAC / ESS / AMBE (mbelib)
            → Security gate → speaker / mute / queue
UpdateManager ← GitHub releases/latest/update.json
```

Single-instance lock by default (device contention). CLI and GUI share the same demod/P25 cores.

---

## Legal

Receive-only software. You must comply with laws in your jurisdiction for reception, recording, and use of radio traffic. Do not intercept private communications where prohibited. **Encrypted traffic is not decrypted**; encrypted calls are skipped or muted.

See `LICENSE.txt`.

---

## Contributing / tester feedback

- Prefer **reproducible captures** (SigMF + P25 log) under `%APPDATA%\SDR_Town\SDR Town\` plus version number.
- Include DEEP DIAG / DSP VOICE lines with `expVcw`, `fed`, `emit`, `gaps`, `p2sf`, `p2mask`, `p2mac`, gate reason.
- Issues and PRs: https://github.com/Blkph0x/SDR_Town  

**Bottom line:** Analog (especially **WFM / AM / NFM**) is in good shape for real use. **P25 Phase 2 clear audio is close—about half the time in field conditions—and still the primary focus until it is continuous, ordered, and trustworthy.**
