# SDR Town

**SDR Town** is a Windows desktop SDR receiver and analysis application for multi-device monitoring, analog demodulation, P25 Phase 1/2 trunking experiments, spectrum/waterfall work, classifier training capture, CLI automation, and GitHub-based self-updates for tester builds.

| | |
|---|---|
| **Current version** | **0.2.83** (experimental channel) |
| **Platform** | Windows 10/11 x64 |
| **UI** | Qt 6 GUI + interactive CLI |
| **License** | See `LICENSE.txt` |
| **Releases** | https://github.com/Blkph0x/SDR_Town/releases |
| **Repo** | https://github.com/Blkph0x/SDR_Town |
| **Pairs with** | [FUBAR](https://github.com/blkph0x/FUBAR) (VOX capture, public live website, optional remote tune) |

Formerly *MaulAudio Pro*. Branding, binaries, installer, AppData paths, and release assets all use **SDR Town** / `SDR_Town`.

## SDR Town + FUBAR

[FUBAR](https://github.com/blkph0x/FUBAR) is the companion station app. Use SDR Town to receive and decode; use FUBAR to capture the audio and put a public website on the LAN (and optionally the [FUBAR Net](https://gearsqueens.online/fubar-net) directory).

| App | Role |
|-----|------|
| **SDR Town** | RF in, demod / P25 follow, speaker or virtual-cable out, local control API on `127.0.0.1:8765` |
| **FUBAR** | WASAPI capture, VOX WAVs, live website, typed **Now playing**, live P25 subtitle, optional website tune leases via `SdrTownControl.dll` |

### Why pair them

| Use case | SDR Town | FUBAR |
|----------|----------|-------|
| **Share what you hear** | Demod to VB-CABLE or a hardware line | Capture that feed; **Public website** → phones listen live |
| **Show live P25 activity** | Monitor CC + auto-follow; keep aliases filled in | Website keeps your typed title; smaller line shows `TG …` + alpha, or `Listening to NSWGRN Control` on the CC |
| **Let a trusted visitor retune** | Leave local control server running (default) | Enable SDR Town control in FUBAR settings; visitor **Take control** on the site (queued lease) |
| **Log interesting traffic** | Stay on channel / trunk follow | VOX clips land in `%AppData%\Roaming\FUBAR\Vox_captures` |

### Setup checklist

1. Install or unpack [SDR Town](https://github.com/Blkph0x/SDR_Town/releases) and [FUBAR](https://github.com/blkph0x/FUBAR/releases).
2. In SDR Town, start the receiver (and P25 Monitor CC / auto-follow if that is your station). Confirm the status bar shows local control on `127.0.0.1:8765` (loopback only).
3. Route SDR Town audio to **VB-CABLE** (or another capture endpoint FUBAR can open).
4. In FUBAR, select that cable as the input, enable **Public website**, and set **Now playing**.
5. Place a **matching** `SdrTownControl.dll` next to `FUBAR.exe`. This Town **0.2.80** release ships `SdrTownControl-0.2.80-win64.dll` (rename to `SdrTownControl.dll`). Pair with FUBAR **1.1.41**. See [pairing versions and gaps](docs/FUBAR_PAIRING.md).
6. **Tools → Settings** in FUBAR: enable SDR Town control and pick allowed actions. FUBAR **1.1.41** website has P25/RDS/tones/SSTV (Auto + HamDRM list, Receive/Finish/Cancel), Satcom, Inmarsat, Aircraft, and SDRplay. **Home lat/lon is Town-only** (not on the public site).

### Control API notes (for FUBAR and other local clients)

- HTTP JSON on **localhost only** (default port **8765**). Not exposed to the LAN.
- `GET /v1/status` includes monitor state and a `p25` object. From **0.2.63**, `p25.talkgroupStatusLabel` is the clean `TG <id> <alpha>` string (no voice diagnostic suffix). FUBAR uses that for the website subtitle, with an alias-file fallback on older SDR Town builds.
- From **0.2.64**, status also exposes read-only `rds`, `tones` (CTCSS/DCS), and `sstv` blocks for the FUBAR website panels. `POST /v1/direct-sampling` sets RTL-SDR `direct_samp` (0=off, 1=I-ADC, 2=Q-ADC) for HF down to ~500 kHz. `POST /v1/mode` changes demod at the current frequency.
- From **0.2.66**, website / local-control analog tune and mode changes fully leave P25 Monitor CC (pass `force=true` while a grant follow is live) so FUBAR mode/freq buttons stick instead of snapping back to P25.
- From **0.2.67**, `GET /v1/status` includes `activeDevice`, `rtlDirectSamplingAvailable`, and a `sdrplay` object (or null) with model-aware features. `POST /v1/sdrplay` applies antenna/AGC/IFGR/RFGR/notches/Bias-T/HDR/clock OUT/diversity. `SdrTownControl_Request` exposes arbitrary control paths for FUBAR.
- From **0.2.68**, Scan/Tools **Satcom Scanner** tab: band sweep/lock/record with async logging, AX.25/APRS AFSK1200 and NOAA APT grayscale decode. Local control: `GET /v1/satcom/status`, `POST /v1/satcom/control`, capability `satcomScanner`. Out of scope: commercial sat decrypt, Meteor LRPT/SatDump, full SGP4 Doppler.
- From **0.2.69**, Satcom pass planner: home lat/lon (N/S E/W), CelesTrak TLE cache, SGP4 AOS/LOS, Auto-track Doppler retune, ISS SSTV arm/handoff. API: `/v1/satcom/observer|passes|catalogue|arm|tle/refresh`, capability `satcomPasses`.
- From **0.2.70**, sat catalogue honesty badges + Aircraft Map (local 1090 ADS-B + OpenSky enrichment, OSM tiles, click popout). API `/v1/aircraft/status|track|refresh`, capability `aircraftMap`.
- From **0.2.74**, SDRplay discovery checks the installed API and SoapySDRPlay3 runtime in standard SDRplay, PothosSDR, radioconda, and application-local locations, and reports “module loaded” separately from “RSP detected”. From **0.2.73**, WFM listen path no longer resets demod on soft IQ catch-up (fixes 98.1 buzz + waterfall freeze); spectrum UI downsamples. From **0.2.72**, Satcom/Inmarsat/Aircraft dock is lazy and silent on the listening preset (no UI timer / pass-track retune fighting WFM). From **0.2.71**, experimental Inmarsat L-band prototype (public band plans, ACARS/ADS-C parse). **Not RF-qualified:** no unique-word/FEC/Aero AMBE proof. Tools **Inmarsat Aero**; API `/v1/inmarsat/status|control|messages|bandplans`, capability `inmarsatAero`. ADS-C positions feed Aircraft Map (`fromAdsc`). Dual-SDR voice radio not yet. Click the satcom home map (or Shift-click the aircraft map) to set observer lat/lon for ISS SSTV, pass prediction, and Doppler. CLI: `observer`, `tle`, `satcom`, `inmarsat`, `aircraft`.
- From **0.2.79**, analogue SSTV Auto covers Dayton + handbook leftovers (Martin M3/M4, Scottie S3/S4, SC-1, FAX480, MP/MR/ML). From **0.2.80**, **HamDRM digital** (HB9TLK Mode B 2.5 kHz) is a second engine; file Auto retries it if analogue finds nothing. Status `sstv.modes` is the same list FUBAR shows. `POST /v1/sstv/live|finish|cancel` starts/stops live NFM receive (Take control). See [docs/SSTV.md](docs/SSTV.md).
- From **0.2.65**, opt-in **Repeater Control Monitor** (receive-only): DTMF decode, CTCSS/DCS/carrier **heard list**, and dual-watch of output+input when the IQ passband covers the pair. Status adds `tones.dtmf` and `repeater`. See `docs/REPEATER_MONITOR.md`. NFM audio path improved; DCS uses the speech discriminator tap; DTMF no longer double-fires single keypresses.
- Analog `/v1/tune` is refused while a P25 voice follow or warm-standby hold is active so a website poll cannot yank RF back to the control channel mid-call.

FUBAR’s own README has the station/website side in full: https://github.com/blkph0x/FUBAR#fubar--sdr-town

P25 friendly names: **P25 Calls > Aliases...** manages named system-specific
talkgroup lists, JSON import/export, search, groups and protected manual edits.
Existing Alpha Tags win; labels never change decoding or encrypted-audio gates.
See [alias setup and import format](docs/P25_ALIASES.md).

Workspace development: the current source adds detachable/tabbed panels and
Listening, Trunking, HF/DX and Analysis layouts under **View > Workspace**.
Included in v0.2.56 installer and portable assets.
See [workspace controls, automation and decoder roadmap](docs/WORKSPACE_AND_DECODERS.md).

Receive band plans: **Scan > Band Plans** selects region/country/location and
labels the tuned service on the waterfall. Initial AU/GB/US coverage is partial;
validated local JSON imports extend it. AUTO uses band priors, not guaranteed
protocol identification. See [coverage, sources and CLI options](docs/BAND_PLANS.md).

RDS development: a redsea-backed bitstream/FEC decoder and raw WFM
multiplex tap are now available for integration tests. `rds bits "file.bits"`
decodes already-demodulated bits to station metadata. WFM now automatically
displays validated RDS station information above the spectrum; `rds mpx` handles
recorded multiplex files. See [RDS scope, commands and next gates](docs/RDS.md).

NFM tone development: experimental CTCSS/DCS identification appears above the
spectrum without changing squelch or speaker audio. DCS reports equivalent
code/polarity labels together. Offline CLI: `tones file`, `tones dcs` and
`tones dcs-bits`. Known-tone/code RF acceptance is pending; these features are
included in v0.2.56 as experimental identification, not tone squelch.
See [NFM tone tests and limits](docs/NFM_TONES.md).

Decoder development: `decoders` lists compiled adapters and their input/rate
requirements without opening an SDR. GUI RDS and CLI MPX replay share the new
receive-session contract. See [decoder contracts and validation](docs/RECEIVE_DECODERS.md).

Runtime hardening: the release now stages the configured RTL-SDR DLL instead of
retaining an old copy. The reproduced local shutdown access violation is fixed;
see [native debugger tests and hardware limits](docs/NATIVE_RUNTIME_QA.md).

SSTV (**Tools > SSTV Images**, CLI `sstv decode` / `sstv inspect`):

- **Analogue:** Dayton Martin/Scottie/Robot/PD/Pasokon/SC2-180 plus handbook/QSSTV
  leftovers (Robot B&W, SC2-30/60/120, AVT, M3/M4, S3/S4, SC-1, FAX480, MP/MR/ML).
  Auto = 7-bit VIS (Hamming-1), then 16-bit VIS, then closest 1200 Hz line-sync.
- **Digital:** **HamDRM** (HB9TLK Mode B 2.5 kHz). File Auto retries HamDRM if
  analogue finds no picture. Not EasyPal file-level RS.
- **Live NFM:** main receiver discriminator audio, Receive / Finish and save /
  Cancel. Does not retune the radio. Known-transmission RF acceptance is still open.
  HF SSB live input is not supported.

`sstv decode "file.wav" "new-output-directory" auto`  
`sstv decode "file.wav" "new-output-directory" hamdrm`  
See [SSTV.md](docs/SSTV.md). Satellite pass/Doppler/ISS arm is in Satcom (not the SSTV window).

---

## Current state (honest, code- and field-verified)

Development method (binding): [`SOURCE_OF_TRUTH.md`](SOURCE_OF_TRUTH.md),
[`CAUSE_EFFECT_MAP.md`](CAUSE_EFFECT_MAP.md), [`DEVELOPMENT_RULES.md`](DEVELOPMENT_RULES.md),
desk [`docs/README.md`](docs/README.md). **Continuous Phase 2 audio is not done.**
`PASS_PARTIAL_AUDIO` is a diagnosis. Do not start another gate/TTL tweak until
CADENCE/voicetest print `drop=A|B|C|D|E|ok` (REQ-P2.0 / ISS-0001).

This is **active experimental software**. It is useful for real RF testing and development. It is **not** a finished production trunking scanner.

### Working well today

| Area | Reality |
|------|---------|
| **Analog demod** | **WFM, AM, NFM** are solid everyday paths. **AUTO** picks mode/BW/LPF suggestions from band priors + live signal estimates. **USB / LSB / CW** exist and produce audio; they are basic receive chains, not polished DX receivers. |
| **GUI** | Spectrum + waterfall, device manager, multi-output audio, saved frequencies, P25 control/talkgroup panes, live SIG/NF/SNR/AFC readouts, IQ capture, training capture, Help â†’ Check for Updates / Report Issue. |
| **Devices** | RTL-SDR, **SDRplay RSP series** (SoapySDRPlay3: IFGR/RFGR, AGC, combined MW/FM + DAB notches, Bias-T, HDR, clock OUT, RSPduo Dual Tuner + host diversity/null-steer), and other SoapySDR devices. Safe stub path when hardware is absent. RF gain, sample rate, antenna, PPM (manual + cal/apply). See [docs/SDRPLAY.md](docs/SDRPLAY.md). |
| **Audio** | miniaudio multi-output (speakers + virtual cable), per-output enable/volume, ring-fill and underrun counters. Master volume in GUI. |
| **P25 Phase 1** | Control-channel C4FM path: frame sync, NID, TSDU/TSBK trust, grants, talkgroup list. Clear IMBE backend via mbelib when frames validate. |
| **P25 Phase 2** | Full experimental TDMA pipeline: superframe/ISCH, XOR mask (NAC/WACN/SysID), ACCH/MAC/ESS hypotheses, Voice2/Voice4 â†’ AMBE 3600Ã—2450 (mbelib), one-RTL traffic retune + return-to-control, security gate (encrypted mute, clear only with proof). |
| **Updater** | Fetches `update.json` + `update.json.sig` from GitHub **latest** release, verifies Ed25519 signature (when a release public key is configured) and SHA-256 of the installer, user consent onlyâ€”no silent install. |
| **CLI** | Full interactive shell + one-shot `--cli --cmd "..."`. Replay/voicetest/followtest/waitgrant for lab and field diagnostics. |

### P25 Phase 2 clear audio - experimental

As of **v0.2.51**, file voicetest of the 2026-09-08 `041716` call (TG 10330 slot 1) is `PASS_CONTINUOUS_AUDIO duty=0.87` after DEC-0012 stopped companion-louder mixed MAC-dead hops from reaching the speaker. Live CADENCE is still often drop **D** (worker-busy independent CQPSK). Treat Phase 2 as experimental.

What that means in practice:

- **When it works:** grant â†’ follow â†’ mask + slot â†’ clear proof (MAC/ESS/PTT or established clear carry) â†’ AMBE frames feed mbelib â†’ speaker audio. Continuous/joined enough to be useful.
- **When it fails or degrades:** low/zero MAC CRC despite high superframe/mask counts; opposite-slot thrash; late-entry wait; frame order/dedupe gaps (logs show `expVcw` / `fed` / `emitPcm` / `gaps`); irregular 20â€¯ms cadence â†’ blocky or â€œalmostâ€ speech; return-to-control before the call ends.
- **Security is intentional and strict:** unknown grants do **not** open the speaker by default. Encrypted grants/ESS stay muted. Lab-only late-entry/unknown probe is **default off** (`kP25Phase2AllowUnknownGrantFieldAudioProbe = false`); enable only via explicit CLI/GUI flags for diagnostics.

Do **not** treat Phase 2 as production-ready. Treat it as a working experimental decoder under active hardening toward SDRTrunk-class continuity.

### AI clear-audio automation

End-to-end live â†’ IQ save â†’ CLI voicetest â†’ STT â†’ GUI replay:

```powershell
python src/tools/run_p25_ai_clear_audio_pipeline.py --cc 420.350 --build
```

Shared STT defaults (also used by deep audit / live diag / GUI IQ replay): backend `auto` (`SDR_TOWN_STT_BACKEND` or faster-whisper then openai-whisper), `min_chars=12`, `min_words=3`. Setup: `scripts/setup_stt.ps1`.

### Incomplete or experimental (do not oversell)

- **ONNX classifier backend** is a placeholder; the **deterministic** classifier is what runs.
- **Smart Scan** button is present (PR6-era foundation)—not a full production scanner (no priority lists, lockout, hold, multi-TG routing product yet).
- **Satcom / aircraft / Inmarsat / SSTV / HamDRM** ship as **experimental** tools (recorded SSTV and HamDRM selftest are verified; live RF pictures, live HamDRM on-air, Inmarsat unique-word/FEC/AMBE, and Meteor LRPT are not).
- **DMR / NXDN / POCSAG / broadcast DRM** are still not implemented as receive decoders.
- **Updater** verifies Ed25519 manifest signatures (when configured) plus installer SHA-256; Authenticode not done yet.
- **SDR open/stream** is in-process (no separate helper process yet)—wedged USB/Soapy can still affect the app process.
- **SSB/CW** are functional basics, not contest-grade AGC/filtering chains.

---

## Downloads and updates

Tester builds: https://github.com/Blkph0x/SDR_Town/releases

| Asset | Purpose |
|-------|---------|
| `SDR_Town-X.Y.Z-win64-setup.exe` | NSIS installer (silent `/S` supported) |
| `SDR_Town-X.Y.Z-win64-portable.zip` | Portable folder |
| `update.json` / `update.json.sig` | In-app updater manifest + Ed25519 signature |
| `SHA256SUMS.txt` | Release hashes |
| `*.exe.sha256` | Per-installer hash file |

**How shipping works (code path):**

1. Bump `project(SDR_Town VERSION â€¦)` in `CMakeLists.txt`.
2. Commit reviewed source first. `scripts/release.ps1 -Version X.Y.Z -Channel experimental` checks the clean attached branch, builds/tests, deploys Qt, packages NSIS/ZIP/control DLL, signs and verifies the manifest/assets, commits release metadata, tags and pushes the current branch, then uploads assets. Native command failures stop the process. See [release gates and manual publication](docs/RELEASING.md).
3. App `UpdateManager` fetches  
   `https://github.com/Blkph0x/SDR_Town/releases/latest/download/update.json`  
   then downloads only HTTPS GitHub release installer URLs, verifies `update.json.sig` (Ed25519) when a release public key is configured, and verifies installer SHA-256 before launch.

---

## Quick start â€” GUI

1. Install drivers (RTL-SDR: Zadig → WinUSB as Administrator; SDRplay: API 3.x + SoapySDRPlay3 — see [docs/SDRPLAY.md](docs/SDRPLAY.md); other Soapy devices per vendor).
2. Run `SDR_Town.exe` (installer or portable).
3. **Devices â†’ Rescan / Discover Devices**, enable device, set sample rate / gain / antenna / PPM, apply.
4. **Audio â†’ Configure Output Devices** (speakers Â± virtual cable), test tone if needed.
5. Tune frequency; choose **AUTO** or **WFM / NFM / AM / USB / LSB / CW**.
6. Adjust BW, LPF, squelch, RF gain, master volume while watching **SIG / NF / SNR / AFC**.

### GUI feature map (from code)

| Panel / control | Function |
|-----------------|----------|
| Spectrum + waterfall | Live spectrum; FFT sizes include 4096â€“65536; zoom/color controls |
| Active receivers | Multi-receiver foundation: add/remove, mode, BW (incl. Auto BW), LPF on/off + cutoff, squelch + Auto, gain, set & tune device |
| SIG / NF / SNR / AFC | Live RF metrics from DSP worker |
| Master volume + Outputsâ€¦ | Global volume; multi-output routing |
| Saved frequencies | Add current / tune / delete / refresh |
| Capture Training Sample | SigMF + classifier training tile |
| Start/Stop IQ Capture | Rolling IQ capture for lab/debug (AppData under SDR Town) |
| P25 Control Channels | Scan CC, Monitor CC, **Grant Test**, Add known CC, Refresh, P25 Log |
| Auto Follow Grants | Follow clear voice grants from control channel |
| Traffic Source | Independent traffic-source path when available (one-RTL retune semantics) |
| Talkgroup table | Add / Verify / Follow TG / Delete / Refresh / Add to Scanner |
| IQ Replay | Tools -> IQ Replay opens a seekable SigMF replay window with P25 speaker-gate decode, WAV save, and STT tap |
| Help | Check for Updates; Report Issue; My Submitted Issues (with remote diagnostics config) |

**Grant Test:** tunes selected/known CC, mutes raw control audio, arms auto-follow, opens P25 logâ€”standard field grant/voice-gate workflow.

**P25 audio policy (GUI + CLI):** control-channel audio is muted by design. Voice audio opens only after security/session proof (or established clear carry). Encrypted calls are skipped/muted.

### GUI launch flags (selected)

```text
SDR_Town.exe
SDR_Town.exe --freq 476.4625 --start-device --default-audio
SDR_Town.exe --p25-cc 420.350 --gui-auto-follow --gui-default-audio
SDR_Town.exe --p25-cc 420.350 --grant-test --p25-log
SDR_Town.exe --gui-start-iq-capture --capture-label field1 --capture-seconds 30
SDR_Town.exe --gui-iq-replay "C:\captures\field_run" --gui-iq-replay-target 418.875 --gui-iq-replay-tg 30003 --gui-iq-replay-slot 1 --gui-iq-replay-autoplay
```

Also: `--gui-device`, `--gui-p25-monitor`, `--p25-late-entry-audio-probe` (lab), `--gui-startup-self-test`, `--gui-require-clear-audio`, `--gui-exit-after-ms`, `--allow-multiple` (lab only; default single-instance).

GUI IQ replay flags:

```text
--gui-iq-replay <sigmf-meta|sigmf-data|capture_dir>
--gui-iq-replay-target <mhz>     --gui-iq-replay-center <mhz>
--gui-iq-replay-start-ms <ms>    --gui-iq-replay-ms <ms>
--gui-iq-replay-window-ms <ms>   --gui-iq-replay-hop-ms <ms>
--gui-iq-replay-tg <id>          --gui-iq-replay-slot <0|1>
--gui-iq-replay-nac <hex|dec>    --gui-iq-replay-wacn <hex|dec>
--gui-iq-replay-system <hex|dec> --gui-iq-replay-clear|--gui-iq-replay-enc
--gui-iq-replay-wav <path>       --gui-iq-replay-result <json>
--gui-iq-replay-autoplay         --gui-iq-replay-no-stt
```

The replay path uses metadata-only SigMF inspection for the slider, then loads bounded windows from disk. Speaker/STT output is only tapped after the same P25 speaker gate used by live RX.

---

## Quick start â€” CLI

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
| `audio enable <out0> [out1 â€¦]` | Enable outputs |
| `audio disable` | Stop outputs |
| `rx add` | Add another receiver entry |
| `sstv inspect <wav>` | Classic VIS headers only |
| `sstv decode <wav> <new-dir> [auto\|hamdrm\|martin1\|…]` | Offline analogue or HamDRM image decode |
| `observer show` / `observer set <lat> <lon>` | Home position for ISS/passes/Doppler (Town only; not FUBAR) |
| `tle refresh` / `tle load` | CelesTrak or fixture TLE |
| `satcom status\|start [force]\|stop\|arm\|…` | Satcom scanner / ISS SSTV arm |
| `inmarsat status\|start [force]\|stop\|plan` | Inmarsat prototype (no voice follow) |
| `aircraft` | Aircraft map status |
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
| `p25 voicetest <capture> <voice_mhz> [ms] [skip=] [center=] [offsethz=] [tg=] [slot=0\|1] [nac=â€¦] [clear\|enc] [stream] [probe\|noprobe] [wav=]` | Phase 2 voice IQ through speaker gate |
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

**Cadence compare fields** (DSP / DEEP DIAG logs): `expVcw`, `fed`, `emitPcm`/`emit`, `gaps`, `lastAbs` â€” expected voice codewords vs frames fed to mbelib vs PCM emitted vs order gaps. Use these when diagnosing blocky/repeated/out-of-order audio.

---

## P25 workflow (GUI + CLI)

1. Tune or add a known **control channel**.
2. **Monitor CC** (audio muted) or `p25 monitor`.
3. Watch **P25 Log** / CLI for sync, NID, TSBK, grants, slot, mask, MAC, ESS.
4. Enable **Auto Follow Grants** or use `p25 waitgrant â€¦ follow`.
5. On grant: one-RTL path retunes (or independent traffic source), arms Phase 2 decode, gates speaker until clear proof.
6. On end/idle/timeout: return to control, clear voice state.

**Phase 2 gate summary (do not relax casually):**

- Emit clear audio only with defined proof: session release / ESS clear / MAC PTT-ACTIVE path / established clear carry after prior proof.
- Encrypted grant or encrypted ESS â†’ mute.
- Unknown security â†’ queue/wait, not free-play (except explicit lab probe flags).
- Opposite TDMA slot VCWs are ignored for release on the followed slot.

Field captures and logs typically live under:

```text
%APPDATA%\SDR_Town\SDR Town\
```

(IQ captures, logs, P25 logs, settings.)

---

## Remote diagnostics (alpha)

When `remote_diagnostics.json` is present (or `--diag-url` / env), the app can send **compact JSON events** (startup, stalls, P25 gate counters, audio metrics)â€”not full IQ/PCM dumps.

```powershell
# Collector
powershell -ExecutionPolicy Bypass -File scripts\start_remote_diag_server.ps1 -Port 8787 -Host 0.0.0.0

# App
.\SDR_Town.exe --diag-url https://host:8787/ingest --diag-token <token>
# or: SDR_TOWN_DIAG_URL / SDR_TOWN_DIAG_TOKEN
# disable: --diag-off
```

Help â†’ Report Issue / My Submitted Issues integrate with the collector when configured. See `scripts/install_remote_diag_task.ps1` for a logon task.

---

## Build from source

**Prerequisites:** Windows 10/11 x64, VS 2022 C++, CMake â‰¥ 3.25, Git, vcpkg, Qt 6 MSVC 64-bit Widgets, liquid-dsp / Soapy as per `vcpkg.json` and CMake, optional mbelib submodule for P25 voice.

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

Produces and uploads: NSIS setup, portable ZIP, `update.json`, `update.json.sig`, SHA files. Generate signing keys once with `scripts/sign_update_manifest.ps1 -GenerateKeyPair` (private key stays local). Optional: `-RemoteDiagnosticsUrl https://â€¦` injects packaged diag config without committing tokens.

---

## Project layout

```text
CMakeLists.txt          Version + deploy/CPack
include/                Headers (Demod, DeviceManager, P25*, AudioEngine, UpdateManager, â€¦)
src/                    Implementation (main GUI/CLI, P25LiveDecoder, Demod, â€¦)
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

- `docs/p25_phase2_release_gate.md` â€” Phase 2 security/release gates  
- `docs/P25_SDRTRUNK_FULL_COMPARISON.md` â€” parity notes vs SDRTrunk  
- `docs/signal_classifier.md` â€” deterministic classifier  
- `DESIGN.md` â€” long-form design log (not always current on every edge)

---

## Direction - current implementation order

1. **SSTV live RF and HamDRM on-air**
   Recorded analogue modes and HamDRM selftest ship. Next: known-transmission
   live NFM pictures, HF SSB live input, EasyPal file-RS only if a published table appears.

2. **Satellite / weather honesty**
   Satcom scanner, TLE/SGP4 Doppler, ISS SSTV arm, NOAA APT grayscale, and
   aircraft map ship experimentally. Meteor LRPT / SatDump-class decode do not.

3. **Inmarsat prototype gaps**
   Band plan, start/stop, ACARS/ADS-C log. No unique-word, FEC, or Aero AMBE.

4. **Analog and data polish**
   Known-tone CTCSS/DCS RF acceptance, SSB/CW filtering/AGC, then separately
   validated DMR/NXDN/pager chains.

5. **Operational hardening and trust**
   More hardware/debugger cycles, SDR process isolation, Authenticode and
   reproducible release gates.

6. **P25 follow-up (deferred by user)**
   Preserve security/slot isolation and capture-based regression tests. Remaining
   live continuity qualification is open.

7. **Scanner and classifier extensions**
   Recording history, multi-output per TG and a measured ONNX classifier backend;
   the current classifier remains deterministic.


---

## Architecture snapshot

```text
DeviceManager (RTL/Soapy IQ)
    â†’ DSP worker + optional P25 voice worker
        â†’ Demod (analog)  â†’ AudioEngine (miniaudio)
        â†’ P25LiveDecoder  â†’ grant/follow state machine
            â†’ Phase 2 mask / MAC / ESS / AMBE (mbelib)
            â†’ Security gate â†’ speaker / mute / queue
UpdateManager â† GitHub releases/latest/update.json
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

**Bottom line:** WFM/AM/NFM, RDS, workspaces, band plans, SDRplay Soapy, satcom/TLE/Doppler,
aircraft map, and analogue SSTV (plus HamDRM selftest) ship in **0.2.80**. P25, live SSTV RF,
Inmarsat, and HamDRM on-air remain experimental. No universal reception or clear-audio
percentage is claimed. Pair FUBAR **1.1.41** with this Town DLL.
