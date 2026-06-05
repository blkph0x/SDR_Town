# MaulAudio Pro — Design Document

**Project codename / folder:** `maulaudio_pro`  
**Target (v1):** Professional Windows desktop application (C++)  
**Status:** Planning phase — pre-implementation  
**Last updated:** 2026-04 (planning)  
**Owner:** User + Grok engineering  

---

## Executive Summary

MaulAudio Pro is a professional-grade, easy-to-use Windows C++ GUI application for connecting to **multiple SDRs** (HackRF and other SoapySDR-supported devices) simultaneously. It provides **smart scanning** for voice and data signals, high-quality playback of **unencrypted audio**, and dedicated support for data modes including **weather satellite downlinks** (NOAA APT and similar).

Core differentiators:
- Clean, modern professional UI with powerful yet approachable configuration.
- Native support for routing the selected audio to **2+ output devices at once** (real speakers + VB-Audio Cable / virtual cable software, with independent per-device volume).
- Practical "state-of-the-art" but implementable DSP techniques for detection, classification, demodulation, and decoding.
- **Advanced Signal Analyzer**: user-selectable frequency + bandwidth with hybrid classical + modern algorithms for automatic modulation recognition (ASK/OOK, PSK variants, FSK, QAM, etc.), constellation analysis, parameter estimation, and decoding of unencrypted digital signals (no decryption support or attempts on encrypted traffic).
- Robust multi-device handling and error recovery.

The application prioritizes **real-world usability** for monitoring, hobbyist SIGINT-lite, weather satellite reception, and public safety / ham listening (lawful unencrypted only).

---

## Goals

- Allow users to discover, configure, and use **1–N SDR devices** (focus HackRF, but any SoapySDR device) through a single GUI.
- Perform **smart, efficient scanning** across bands with minimal missed signals, using energy detection, basic classification, voice activity detection (VAD), and history.
- Deliver excellent **analog voice** (NFM primary, AM, WFM, SSB/CW) with CTCSS/DCS, squelch, and clean audio.
- Support key **digital voice** (P25 Phase 1 clear, DMR clear, NXDN) and common **data modes** (POCSAG, basic AX.25).
- Provide first-class **weather satellite** support (NOAA APT imagery live + recording; stretch LRPT).
- Offer a dedicated, reliable **multi-device audio output** system (simple duplication model for v1) so one can listen on desktop speakers while simultaneously piping to virtual audio cables for external tools, recording, or further processing.
- Provide a powerful **Advanced Signal Analyzer** tool: select any frequency and bandwidth; the app applies state-of-the-art (hybrid classical DSP + modern techniques) automatic modulation classification for common digital schemes (ASK/OOK, various PSK, FSK/MSK, QAM orders, etc.), estimates parameters (symbol rate, order, offsets), displays constellation/eye/spectrogram, and offers decoding paths for recognized unencrypted signals (generic bitstream, text, or mapped to supported protocols). Explicitly no decryption or crypto attacks on encrypted traffic.
- Feel **professional and polished**: stable, responsive, good defaults, discoverable advanced features, excellent error handling, and low cognitive load.
- Use modern C++, good architecture, and maintainable code so features can be added without major rewrites.

## Non-Goals (v1)

- Any transmit functionality (RX-only for safety and legal clarity).
- Decoding of encrypted or proprietary controlled signals. The Advanced Signal Analyzer will **never attempt to break or decrypt encryption**; it will clearly indicate when a signal appears encrypted or fails to produce intelligible output and will only provide tools for lawful unencrypted traffic.
- Full blind demodulation / perfect classification of arbitrary high-order, exotic, or heavily spread-spectrum signals (we provide excellent tools and known-family support, not magic).
- Full trunked radio system following (control channel + voice channel coordination) — this is a significant later phase.
- Cross-platform (Linux/macOS) in v1. Design for portability but ship Windows-first.
- Plugin architecture (internal modularity yes; user plugins no in v1).
- Antenna rotator control, external hardware switching, or TLE-based automated satellite tracking/scheduling (nice-to-have later).
- Heavy custom ML model training in v1 (we may integrate a small pre-trained ONNX model for classification boost using classical features + spectrogram/constellation inputs; pure heuristic + liquid-dsp classical methods are the reliable base).

## MVP Definition (First Usable Releases)

A user can:
1. Plug in one or more HackRFs (or other Soapy devices).
2. Discover and configure them in a GUI (gains, sample rates, antennas, bias-tee).
3. Define or use built-in band plans.
4. Start a "Smart Scan".
5. See live spectrum/waterfall.
6. Have the app lock on active signals, demodulate cleanly, play audio.
7. Configure 2+ audio output devices and hear the same audio on both (speakers + VAC) with independent volumes.
8. See hits logged, manually tune a receiver, record audio.
9. For weather sats: tune a known freq or have scanner hit it, see live decoded image building.
10. Basic digital: POCSAG text decoding; P25/DMR voice when signal is strong and clear.

Everything else (more modes, DDC multi-channel per wideband device, advanced classification, better sat support, full recorder manager, presets system) comes in follow-on increments.

---

## Architecture Overview

### High-Level Layers

```
GUI (Qt6 Widgets + custom viz)
          |
    Application Core / Orchestrator
          |
    +-----+-----+-----+
    |           |     |
Devices    DSP /     Audio
(Soapy)   Decoders   Engine
            |       (miniaudio)
       Scanner / Scheduler
            |
       Config & Persistence (JSON)
```

**Key abstractions:**
- `Device` — physical SDR (SoapySDR wrapper). Owns the RX stream thread.
- `Receiver` (or Channel) — logical tuned narrowband path (freq, mode, filters, squelch, demod instance). Can be user-created or scanner-spawned. Bound to one Device.
- `AudioSink` — abstraction for an output device. Multiple can be active.
- `MonitorBus` or simple fan-out — the "selected" receiver's post-demod audio is fanned (with gain) to all active AudioSinks.
- `Decoder` — pluggable per-mode (analog voice, POCSAG, APT image, P25 voice frame handler, etc.). Some produce audio, some produce text/images/events.

### Sample & Threading Model (Critical for Real-Time)

- Device RX thread (per physical SDR): blocking `SoapySDR::Device::readStream`, push IQ blocks into a lock-free SPSC or MPSC ring buffer (samples as `std::complex<float>` or `cf32`).
- DSP / Demod threads or task pool: pull from ring, run channel DDC/filter/demod chain (liquid-dsp), produce mono 48 kHz float PCM.
- For wideband devices: optional efficient channelizer (polyphase or multiple parallel DDCs) so one 2–10 MS/s device can feed several narrow Receivers without multiple USB reads.
- Audio: miniaudio playback devices run their own real-time threads/callbacks. The app pushes or the callback pulls the latest PCM block (duplicated + scaled per device).
- Scanner: background thread or timer that performs FFT on wideband data or steps receivers, updates detection state, creates/destroys temporary Receivers.
- UI thread: Qt main loop. All cross-thread communication via Qt signals/slots (queued) or thread-safe queues + `QMetaObject::invokeMethod`. Never block UI on hardware/DSP.
- Recording: separate writer thread per active recording to avoid glitches.

**Latency targets (typical):**
- RF to speaker: < 150–250 ms end-to-end for voice (acceptable for monitoring).
- Buffer sizes tunable in Audio config for "low latency" vs "rock solid" trade-off.

### Data Flow (Voice Path)

```
SDR (IQ @ Fs) 
  → Device RX thread 
  → Ring buffer 
  → Per-Receiver DDC / filter / rate change (to ~16–48 kHz) 
  → Demod (NFM/AM/SSB/...) + squelch + CTCSS 
  → Post-filter / AGC / de-emphasis 
  → Voice Activity Gate (optional) 
  → Resample to 48 kHz mono float 
  → "Selected Monitor" bus 
  → Fan-out to N active AudioSinks (with per-sink gain) 
  → miniaudio device(s) → WASAPI → speakers / VAC
```

Data paths (pager, sat, packet) branch earlier (after appropriate demod or directly from IF) and feed their own UI panes / file writers. They do **not** consume the primary voice audio bus.

---

## Technology Stack & Rationale

**Language & Standard:** Modern C++20 (or C++23 where supported), MSVC 2022 primary compiler for Windows. Use RAII, smart pointers, `std::expected`/`std::optional`, concepts, etc. Avoid raw new/delete.

**GUI:** Qt 6.5+ (Widgets module primary, with Gui, Core, OpenGL or Widgets for custom painting). 
- Rationale: Extremely mature for professional desktop tools with dense configuration UIs, menus, docks, tables, and dialogs. Excellent Windows integration and high-DPI support.
- "Modern" feel: Fusion style + dark palette + stylesheets + high-quality icons (e.g. via Qt Resource or SVG). Spectrum/waterfall implemented with custom `QOpenGLWidget` or `QWidget` + double-buffered pixmap + efficient drawing (or QCustomPlot / Qwt if it reduces effort without perf loss). This gives fluid updates without going full QML for the entire app.
- Alternative considered: Full Qt Quick + QML. Rejected for v1 because complex traditional forms (device trees, receiver tables, multi-slider audio config) are more work and error-prone in QML than Widgets. We can embed QQuickWidget for specific modern views later if desired.

**SDR Hardware Abstraction:** SoapySDR (latest stable).
- Excellent support for HackRF (via SoapyHackRF), RTL-SDR, Airspy, Lime, USRP, etc.
- Unified API for discovery, streaming, gains, antennas, settings.
- Users install the Soapy modules they need (or we document a "PothosSDR-like" bundle or individual downloads).
- Native libhackrf is a fallback only for features not yet in Soapy.

**DSP & Demodulation:** liquid-dsp (primary library).
- Purpose-built for SDR: excellent FIR/IIR filters, arbitrary resamplers, FM/AM/SSB demod, AGC, symbol sync, Costas, framing helpers, etc.
- Well maintained, used in many serious projects.
- Supplemented with hand-written or minimal custom code only where needed (e.g., specific CTCSS Goertzel, APT line sync state machine).
- FFT: liquid-dsp built-ins or pffft / kissfft for spectrum (avoid heavy FFTW licensing/dependency in v1).

**Audio Output (Multi-Device):** miniaudio.
- Header-friendly, modern, excellent WASAPI backend on Windows.
- Trivial and safe to open **multiple independent playback devices** simultaneously.
- Low overhead, good latency control, easy volume/gain per device.
- Chosen over PortAudio for simpler deployment (fewer DLLs to manage on user machines) and modern API. Over RtAudio for similar reasons.
- Duplication model: one PCM source block is written (scaled) to each active `ma_device`. Handles device hot-unplug by removing the sink and surfacing a non-fatal notification.

**Logging:** spdlog (file + console + optional Qt sink). Rotating logs, levels.

**Configuration & Persistence:** nlohmann/json for human-readable/editable config files (devices.json, bands.json, audio.json, receivers.json, settings.json). Simple versioning + migration. User can back up the whole `%APPDATA%\MaulAudioPro` or equivalent folder.

**Digital Voice (P25/DMR/NXDN):** mbelib (submodule) for the voice codec part after proper symbol/frame sync and error correction. 
- Only unencrypted traffic. Add very visible disclaimers.
- Implementation order: solid analog first → POCSAG → one digital voice mode (P25 Phase 1 recommended first) → others.

**Weather Satellites (APT):** Custom implementation based on well-documented public algorithms.
- FM demod → 2.4 kHz bandpass → AM / envelope on subcarrier → sync detection (7x 1040 Hz pulses or equivalent) → line assembly → de-interlace → contrast/brightness + optional histogram.
- Display in a scrollable or full-frame image widget; auto-save PNG + metadata.
- References: public NOAA APT specs, existing open-source decoders (no wholesale copy; re-implement cleanly).
- LRPT (Meteor) is a stretch goal (QPSK + convolutional + etc.); SatDump is an excellent architectural reference but we will not depend on it.

**Build System:** CMake 3.25+.
- vcpkg manifest (`vcpkg.json`) for most third-party (spdlog, nlohmann-json, SoapySDR, miniaudio via port or fetch, etc.).
- Qt 6 via official Qt Online Installer (or vcpkg Qt6 port if it matures sufficiently for full Widgets + tools). Document both.
- liquid-dsp and mbelib as git submodules (pinned) with `add_subdirectory`.
- Windows: target x64. Provide clear build instructions + a "bootstrap" script or vcpkg + CMake preset.

**Other:**
- No GNU Radio (too heavy, Python interop, deployment pain for a focused desktop tool).
- Testing: Catch2 or doctest for unit tests. Manual + scripted integration tests on real hardware for DSP/audio.
- Packaging (later): CMake CPack + NSIS or Qt IFW, or simple zip + README for v1. Include driver notes (Zadig for WinUSB on HackRF).

**References for implementation (do not copy large sections):**
- Gqrx / CubicSDR / SDRangel (UI and receiver patterns)
- SatDump (satellite live processing architecture and APT/LRPT pipelines)
- liquid-dsp examples
- Public APT decoding tutorials and NOAA docs
- mbelib + OP25 / DSD projects for digital voice framing (study, reimplement cleanly)

---

## Detailed Feature Areas

### 1. Multi-SDR Configuration (GUI + Backend)

- "Devices" panel or dialog (dockable or modal).
- "Rescan / Discover" button → calls `SoapySDR::Device::enumerate()`.
- Per-device card:
  - Friendly name + driver + serial (HackRFs show serial to distinguish multiples).
  - Status (Connected / Error / Sample rate / USB throughput).
  - Enable/Disable toggle.
  - Antenna selector (from device query).
  - Gain controls (master + per-stage if available; sliders + numeric with dB).
  - Sample rate selector (common safe values + custom).
  - Bias-T / other flags.
  - Advanced: DC offset / IQ imbalance correction toggles (if Soapy supports).
- Persist last-known good settings per serial.
- Warn on unsupported sample rates or conflicting devices (USB bandwidth).

**Future (post v1):** Per-device "roles" (dedicated wideband scanner vs. targeted narrowband).

### 2. Sound Output Configuration (Dedicated Menu / Dialog)

This is a first-class, always-accessible feature.

**Access:** Menu bar → Audio → "Configure Output Devices..." (or toolbar button).

**Dialog contents:**
- Available playback devices list (populated via miniaudio enumeration, showing friendly names, e.g. "Speakers (Realtek Audio)", "CABLE Input (VB-Audio Virtual Cable)").
- Multi-select checkboxes (0 to all; user can choose exactly two or more).
- Global "Master Volume" slider (0–100%) + Mute.
- Per-device section (when checked):
  - Volume slider (0–100%, relative to master).
  - Mute per device.
  - "Test" button — plays a short 1 kHz tone burst **only on that device** (useful for identifying which physical/VAC is which).
  - Latency / buffer hint (Low / Normal / Safe) — maps to period size / buffer frames.
- Current status strip at bottom or in main window: "Audio Outputs: 2/5 active — Speakers (vol 85%) ✓   VAC (vol 70%) ✓   [levels meters]".
- "Apply" applies live (restart only affected devices; try to be seamless).
- "Reset to Defaults" (system default device only).

**Implementation notes:**
- Maintain a vector of active `ma_device` (playback, 48 kHz, mono or stereo as appropriate).
- When the monitor audio bus produces a new block of PCM, for each active sink: apply gain, convert if needed, `ma_device_write` or feed callback ring.
- On device failure or removal: remove from active list, log, show non-modal warning "Output device 'VAC' was disconnected. Audio continues on remaining devices."
- Support "No outputs" (headless / record-only mode).

**Menu integration:** Also quick items like "Mute All", "Cycle Test Tone", "Audio Devices..." .

### 3. Smart Scanning Techniques

**Band Plans:**
- JSON list of entries: `{ "name": "VHF Public Safety", "start_hz": 150e6, "end_hz": 174e6, "step_hz": 12500, "default_mode": "NFM", "priority": 8, "dwell_ms": 200 }`.
- Built-in library + user editable + import/export.
- Include useful starters: Airband (AM 8.33/25), Marine, NOAA sats (exact freqs), Weather radio, GMRS/FRS, Ham segments, Rail, etc.

**Scanning Strategies (layered, user selectable):**
1. **Spectrum sweep** (on a wideband device): continuous or triggered FFT, CFAR-style or noise-floor-tracking threshold, peak finding, center-of-mass refinement for freq estimate, rough bandwidth classification (helps pick mode).
2. **List / stepped scan**: step through band plan, dwell, measure power + quality.
3. **Priority + history aware**: channels with recent hits get higher revisit rate. Quiet channels get longer skip. User can "lock" favorites.
4. **Activity gated**: once a signal is detected above squelch, dwell longer, engage VAD (post-demod energy in speech band + spectral features or simple zero-crossing + RMS). Only pass audio / start recording when voice or data burst is present. Hang time (1–3 s configurable).
5. **Classification hints**: use occupied bandwidth, AM vs FM symmetry, subcarrier presence (for APT), sync word search (for digital/pagers) to choose or suggest mode.

**UI for scanning:**
- Big "Start Smart Scan" / Pause / Stop.
- Current activity: "Scanning 162.400 MHz (NOAA WX) — 3 hits this session".
- Hits table (sortable, filterable): Freq, Mode (detected or forced), First seen, Last seen, Hit count, Peak level, Notes. Buttons: "Tune", "Add to Receivers", "Ignore for 1h".
- Per-band-plan enable/disable and priority sliders.

**Performance:** Scanner should use a low-duty wideband device or share a device when possible. Do not starve the main monitor receiver.

### 4. Receivers, Demodulation & Modes (Prioritized)

**Core analog (MVP must-have):**
- NFM (12.5 kHz and 25 kHz variants) — primary for most voice.
- AM (air, marine, CB).
- WFM (broadcast, with stereo decode later?).
- SSB (USB/LSB) + CW (with narrow filters and BFO pitch control).

**Digital voice (high priority per user):**
- P25 Phase 1 (C4FM, IMBE via mbelib) — clear voice only.
- DMR (clear, Tier II basic).
- NXDN.
Order: get one solid (P25) before adding others. Provide "Force analog" fallback.

**Data / Pager:**
- POCSAG (512/1200/2400 baud) — text messages, very useful.
- Basic AX.25 / APRS (kiss frames → text + map plot later).

**Satellite (weather):**
- NOAA APT (primary): 137 MHz band, specific freqs. Live image assembly + save.
- Stretch: Meteor-M LRPT, others if hardware supports (L-band needs good antenna/LNA).

**Per-Receiver controls (in table or inspector):**
- Frequency (direct entry, spinner with mode-appropriate step, "fine" / "coarse").
- Mode selector (with auto-suggest).
- Filter bandwidth slider.
- Squelch (dBFS or relative).
- CTCSS/DCS selector + "Search" (for NFM).
- Volume (pre-fanout).
- "Monitor" (this receiver's audio goes to the multi-outputs) — radio button, only one at a time.
- Record (manual + "auto on voice").
- "Hold" / "Skip" for scanner.

**Quality features:**
- AGC (fast/slow/off).
- Noise reduction (simple first: comb or spectral gate; advanced later).
- De-emphasis on/off for FM.
- Audio high-pass / low-pass.

### 5. GUI Layout & Professional Polish

**Main window (dockable / savable layout):**
- Menu bar (File, Devices, Receivers, Scan, Audio ← important, View, Tools, Settings, Help).
- Toolbar with common actions (Discover Devices, Start/Stop Scan, Add Receiver, Record, Mute).
- Left / top: Devices status strip (compact cards or list, click to open full config).
- Center: 
  - Spectrum + Waterfall (main tunable view, click-to-tune, drag to pan, scroll wheel zoom, peak hold, etc.).
  - Below or tabbed: Active Receivers table (very important — sortable, context menu for each row).
- Right / bottom: 
  - Decoder / Data output panes (tabbed: "Voice Log", "Pagers", "Satellite Images").
  - Hits / Scan log.
- Bottom: Status bar (current monitor freq/mode, CPU, USB health, output device summary, log level indicator).
- Separate or dockable: Audio levels (multi vertical or horizontal meters for the active outputs).

**Sound output quick access:** Persistent panel or always-visible in status that shows active outputs with mini volume sliders or click to open full dialog.

**Other polish:**
- Keyboard shortcuts (documented in a cheat sheet).
- Freq entry anywhere accepts "162.4", "162400000", "162.4M", etc.
- Presets / "Quick Start" for common scenarios (e.g. "NOAA Satellites", "VHF Voice Scan").
- Full settings dialog with tabs (General, Devices, Audio, Scan, DSP, Recording, Appearance).
- Dark mode default + light option.
- Tooltips + "What's This?" help.
- Session restore (re-open last devices + receivers + scan state?).
- Export / import full configuration.

**Error & edge handling (professional requirement):**
- USB device yanked → try graceful reopen or mark offline; other devices continue.
- Buffer underrun / overrun → increment health counters, auto-adjust buffer sizes with user notification.
- Over-temperature or ADC overload on HackRF → surface warning.
- Invalid sample rate for device → disable or suggest safe value.

---

## 6. Advanced Signal Analysis & Automatic Modulation Recognition (State-of-the-Art Tools)

This is a major power-user / professional differentiator. The app will include a dedicated **Signal Analyzer** view (dockable window, perspective, or tab) that lets the user point at any frequency and bandwidth and receive deep insight plus decoding assistance for digital signals.

### User Workflow
1. From spectrum/waterfall: right-click a peak or drag a bandwidth region → "Analyze in Signal Analyzer" (pre-fills center freq + BW).
2. Or from an existing Receiver: "Open Analyzer on this channel".
3. Or manual entry: type/ spin center frequency + bandwidth (e.g., 2.4 kHz, 12.5 kHz, 25 kHz, 100 kHz for wider digital).
4. Analyzer captures a high-quality IQ buffer (or runs continuously in "live" mode, subject to CPU).
5. Results appear quickly:
   - Ranked list of likely modulation families with confidence scores: ASK/OOK, 2/4/8-PSK, π/4-DQPSK, 8/16/32/64-QAM, FSK (various deviations), GMSK, MSK, OFDM hint, "Analog voice-like", "Noise/Unknown", etc.
   - Estimated parameters: symbol/baud rate, carrier offset, excess bandwidth, approximate order.
   - Visuals: live-updating constellation (scatter plot, density), eye diagram (for PAM/PSK), zoomed spectrogram, instantaneous amplitude/phase/frequency traces.
6. User can:
   - "Try Demod" on any candidate → spawns a generic or family-specific demodulator (using liquid-dsp primitives + custom recovery loops for carrier/symbol timing).
   - View raw or framed bitstream (hex, ASCII if text-like, or protocol guesser).
   - If it matches a supported protocol (P25, DMR, POCSAG, AX.25, etc.), offer "Decode with full protocol handler" (re-uses the main decoders).
   - Route resulting audio (if voice-like) to the main multi-output bus or a dedicated analyzer preview.
   - Save IQ snapshot + analysis report (JSON + images) for later or external tools.
7. "Apply to Receiver": button that creates or updates a main Receiver with the detected/selected mode and parameters.

### Algorithms & "State of the Art" Approach (Practical & Maintainable)
We will use a **hybrid classical + modern** pipeline (the realistic way to deliver usable SOTA in a desktop tool without massive ongoing ML infrastructure):

- **Classical / Feature-based (core, always-on, fast, explainable)**:
  - Higher-order statistics / cumulants (excellent for distinguishing PSK vs QAM orders and against noise).
  - Cyclostationary analysis (spectral correlation for baud rate, carrier, modulation family).
  - Instantaneous features (amplitude variance, phase std dev, zero-crossing rates, etc.) — many computable efficiently with liquid-dsp helpers.
  - Maximum-likelihood or decision-tree / random-forest style classifier on the feature vector.
  - Specific known-signal detectors (preamble / sync word search for P25, DMR, POCSAG, APT subcarrier, etc.).
  - Symbol rate estimation via delay-and-multiply, cyclostationary, or Gardner/ Mueller loops.

- **Modern / ML-assisted (optional accuracy booster)**:
  - Small pre-trained model (e.g. ONNX Runtime) taking constellation images, spectrogram patches, or normalized IQ snapshots as input.
  - Model can be a lightweight CNN or even a tiny transformer distilled for modulation classification. Public datasets (RadioML, etc.) or synthetic + captured data can be used to train/fine-tune a model we ship.
  - The UI will always show "Classical features: X, ML model: Y, Combined: Z" so the user understands the basis and can trust or override.
  - Fallback: if no model or low confidence, pure classical + user expertise wins.

- **Demodulation & Decoding**:
  - Generic toolkits: ASK/OOK slicer, coherent & differential PSK (BPSK/QPSK/8PSK with Costas or decision-directed carrier recovery), CPM/FSK/MSK with appropriate discriminators.
  - Timing recovery, matched filtering, equalization (simple CMA or decision-directed).
  - For voice-bearing digital: hand off to the mbelib-backed P25/DMR paths when framing matches.
  - Generic output: bitstream viewer, packet parser heuristics (length fields, CRC checks, ASCII/HDLC-like), audio if it demodulates to something listenable.
  - Clear, persistent UI label on every output: **"Unencrypted signal path only. No decryption or cryptoanalysis is performed or attempted. If the signal is encrypted or heavily coded, output will be unintelligible or random."**

- **Performance / UX**:
  - Analysis can run on a short capture (0.1–2 seconds typical) or continuously with decimated processing.
  - "Advanced" settings exposed: analysis duration, feature set enable/disable, ML model on/off, confidence threshold.
  - Results are cached per (freq, BW) for quick re-inspection.
  - The analyzer re-uses the same Device IQ stream where possible (no extra USB load when analyzing a receiver already being monitored).

This feature turns the app from "a scanner + player" into a serious **signal intelligence / reverse-engineering workbench** while staying lawful and scoped.

**Placement in UI:** Prominent "Analyzer" button in toolbar and context menus. A dedicated dock or floating professional window with its own toolbar (Capture, Live, Export Report, Apply to Main Receivers).

**Implementation complexity note:** This is one of the more advanced pieces. It will be introduced after solid analog + basic digital voice (so we have good demod primitives), and delivered incrementally (classical features + visuals first, then ML assist, then deeper generic decoders).

---

## Implementation Phases & PR Plan (Incremental, Independently Valuable)

Each PR should be reviewable, buildable, and deliver incremental user value. Order is important for dependencies.

**PR 1: Foundation**
- Project layout, CMake, .gitignore, README with legal disclaimer and build instructions.
- vcpkg.json + submodules (liquid-dsp initial).
- Basic Qt6 main window with menu bar, empty central widget, "About" dialog.
- spdlog integration.
- Initial DESIGN.md (copy or link of this plan) placed in repo root/docs.

**PR 2: Device Layer**
- SoapySDR integration + vcpkg/fetch.
- Device enumeration, basic info display.
- Activate/deactivate device, gain/sample rate/antenna controls in a Devices dialog/panel.
- Simple status and error surfacing.
- Persistence of device settings.

**PR 3: Basic Visualization + Sample Pipeline**
- IQ receive loop + ring buffer.
- FFT spectrum + basic waterfall widget (QOpenGL or efficient QWidget).
- Click-to-tune on spectrum.
- One "manual" receiver stub (no demod yet).

**PR 4: Audio Output Engine + Multi-Device Config (Critical)**
- miniaudio integration.
- Single PCM source → multiple playback devices (duplication + per-device gain).
- Full "Configure Output Devices" dialog with enumeration, checkboxes, per-device volume, test tones, latency choices.
- Live apply, status in main UI.
- "No audio" / record-only path.
- Test: user can verify speakers + VAC simultaneously.

**PR 5: Analog Demodulators + Basic Receiver Management**
- liquid-dsp based NFM, AM, WFM, SSB/CW.
- Squelch, CTCSS tone detection (basic set), de-emphasis, AGC.
- Receivers table: add/edit/remove/tune, mode change, monitor selection.
- Audio path from a receiver to the multi-output bus.
- Manual recording to WAV.

**PR 6: Smart Scanner + Band Plans**
- Band plan JSON + editor (simple table or tree).
- Energy detection + basic classification (BW + AM/FM).
- Stepped list scan + simple spectrum-assisted.
- Hits logging + "promote to permanent receiver".
- VAD gating (basic energy post-demod).
- Scanner controls and status.

**PR 7: Core Polish & Recording Management**
- Full settings dialog.
- Session save/restore (at least devices + last receivers + audio config).
- Better error handling & USB resilience.
- Recording list / browser (with metadata), auto-naming, "open folder".
- Keyboard shortcuts, tooltips, initial user docs.
- Performance baselines.

**PR 8: Data Modes — POCSAG + Basic Pager Pane**
- POCSAG decoder (after FSK or appropriate path).
- Text output pane with timestamps, addresses, messages.
- Logging to file.

**PR 9: Weather Satellite — NOAA APT**
- Dedicated or mode-based APT pipeline (FM → subcarrier → sync → image lines).
- Live image widget (scrolling or accumulating frame).
- Auto save PNG + JSON sidecar (freq, time, gains).
- Manual "force APT decode on current receiver" for testing.
- Known freq presets.

**PR 10: Digital Voice (P25 first)**
- mbelib submodule + framing/sync for P25 Phase 1.
- Clear voice playback through the same audio path.
- Mode selector entry + quality indicators.
- Strong disclaimers in UI and docs.
- (DMR / NXDN can follow in 10a/10b or later PRs.)

**PR 11: Advanced Signal Analyzer Foundation (Classical + Visuals)**
- Dedicated Signal Analyzer dock/window/perspective.
- Capture from freq + BW (manual, spectrum selection, or receiver context menu).
- Classical feature extraction (cumulants, cyclostationary hints, instantaneous stats) + decision logic for common families (ASK/OOK, PSK orders, FSK/GMSK, QAM hints, analog, unknown).
- Core visuals: constellation scatter (live), eye diagram, parameter readouts (baud estimate, offsets, confidence).
- Generic demod attempts (liquid-dsp based ASK/PSK/FSK slicers + timing/carrier recovery) producing bits / preview audio.
- "Apply parameters to Receiver" and "Export IQ + report".
- Clear persistent "unencrypted only — no decryption attempted" messaging and behavior everywhere in the analyzer.
- This PR delivers immediate high value for digital signal reverse-engineering and classification even before full ML or every protocol.

**PR 12: Major UX & Professional Features**
- Presets / Quick Start wizard.
- Favorites / blacklist management.
- Improved spectrum controls (markers, measurements, averaging).
- Full theme / appearance options.
- Log viewer + export.
- About box with versions, legal notice, links.
- Help / documentation (at minimum good README + in-app tips).

**PR 13: Signal Analyzer — ML Assist + Deeper Decoding**
- Optional ONNX Runtime integration + a small, well-documented pre-trained or fine-tunable classification model (constellation/spectrogram features).
- UI showing classical vs. ML vs. combined confidence; easy toggle/disable of ML for determinism or air-gapped use.
- Stronger generic digital decoding: better sync, differential modes, simple protocol heuristics, hand-off to existing P25/DMR/POCSAG/AX.25 engines when signatures match.
- Continuous "live analysis" mode with throttling.
- Polish: report templates, comparison of multiple captures, integration with scanner "analyze hits" workflow.

**PR 14: Packaging, Distribution & Hardening**
- Windows build scripts / presets.
- Bundle notes or simple installer (Soapy modules, Zadig guidance for HackRF).
- Crash reporting stub (or just good minidump + log collection instructions).
- Final performance tuning, memory leak audits.
- User acceptance test checklist.

**Post-v1 / Future PRs (documented in plan but not blocking):**
- Efficient multi-channel DDC on wideband devices.
- Even better / user-trainable classification models.
- LRPT + other sats + automated pass tools.
- More digital modes + trunking-lite.
- Database-backed history + search.
- Plugin or script hooks for custom decoders / custom classifiers.
- Linux port (PipeWire/JACK multi-output, same abstractions).
- TLE / pass prediction + scheduled sat reception.
- Full blind protocol reverse engineering workbench extensions.

---

## Key Decisions (with Rationale)

1. **Windows 10/11 only for v1, portable abstractions underneath**  
   User requirement (VB-Audio Cable). Soapy + miniaudio + Qt + liquid-dsp are all portable, so a high-quality Linux port is realistic later without rewriting core DSP or device code.

2. **SoapySDR as the SDR abstraction**  
   One API for many devices today and tomorrow. HackRF works excellently. Avoids per-hardware forks.

3. **miniaudio for audio output**  
   Simplest reliable way to open and feed multiple independent WASAPI devices from one source. Matches the "simple duplication to speakers + VAC" requirement perfectly. Easier deployment than PortAudio.

4. **Qt Widgets (with modern styling + custom OpenGL/paint widgets for viz)**  
   Best for professional, dense, discoverable configuration UIs (device settings, audio matrix, receiver tables, band plans). "Modern Qt" look achieved via style + custom high-performance views. Full QML would slow down complex traditional forms.

5. **Simple duplication of the primary/selected receiver audio to all chosen outputs (per-device volume)**  
   Directly solves the stated use case (listen live while feeding external software via virtual cable). More advanced independent per-receiver routing is valuable but adds significant UI and scheduling complexity — deferred to v2+ without compromising the core requirement.

6. **liquid-dsp as the DSP foundation**  
   Reduces the amount of custom filter/demod/resample code that must be written and debugged. Battle-tested in SDR contexts.

7. **Phased mode support starting with strong analog + POCSAG + APT, then digital voice**  
   Delivers immediate "voice and data" and "weather satellite" value quickly. P25/DMR/NXDN are important (per user) but have higher implementation risk and interop surface area; they benefit from a solid analog + audio + scanner base first.

8. **No GNU Radio dependency**  
   Keeps the app lightweight, self-contained, and easier to package/distribute as a native Windows tool. Full control over threading and buffering.

9. **JSON file-based configuration (user-visible folder)**  
   Easy to back up, inspect, edit, or version-control. No opaque registry or database lock-in for v1.

10. **Git submodules for liquid-dsp + mbelib**  
    Guarantees reproducible builds and exact versions across developer and CI machines.

11. **Hybrid classical + lightweight modern (ONNX) approach for the Advanced Signal Analyzer**  
    "State of the art" for practical desktop use in 2026 means excellent classical DSP features (cumulants, cyclostationary, liquid-dsp primitives) as the reliable, explainable, zero-dependency base, augmented by an optional small pre-trained model for accuracy on common modulations. This delivers real capability for ASK/PSK/FSK/QAM classification and generic decoding without promising impossible perfect blind demod of everything, and without requiring the project to maintain large training pipelines. The UI makes the basis of every classification transparent and the "no decryption" rule is enforced at the architecture level.

---

## Implementation Log (Living Updates as We Code)

**2026-06 (PR1 + start of PR2)**

- Project skeleton committed (Qt6 stub with professional dark theme, full menus including prominent Audio menu, spdlog, nlohmann, legal notices in README + About).
- DESIGN.md + README placed and kept as living docs.
- vcpkg.json + CMake + presets set up for manifest mode.
- Qt Online Installer downloaded and launched for the dev environment (`~\Downloads\qt-online-installer-windows-x64-online.exe`).
- vcpkg bootstrap + `soapysdr` (correct port name), spdlog, nlohmann-json installed successfully (note: port is `soapysdr`, provides `find_package(SoapySDR CONFIG)` and target `SoapySDR`).
- git submodules added early: `external/liquid-dsp` (for PR5+ DSP) and `external/miniaudio` (header-only, for PR4 critical multi-device audio).
- .gitignore adjusted to allow submodule gitlinks.
- PR2 Device Layer implemented:
  - `DeviceManager` (singleton) with SoapySDR enumeration (real or nice stubs when !HAVE_SOAPYSDR), antenna/sample/gain probing where possible.
  - JSON persistence to `%APPDATA%\.../devices.json` (per serial/driver matching).
  - Full "Device Manager" dialog from menu (table with enable checkboxes, antenna combos, rate/gain spinboxes, apply live, status bar updates).
  - CMake updated with Soapy find + conditional link + HAVE_SOAPYSDR define.
  - Main window status and central label updated to reflect progress.
- Environment: submodules + vcpkg ready. Full build waits on completing Qt installer (select MSVC2022 64-bit + Widgets).
- Next immediate: once buildable, move to PR3 (IQ pipeline + basic custom spectrum/waterfall widget + click-to-tune), then PR4 (the star feature: miniaudio duplication to 2+ outputs with full config dialog + test tones + per-device volumes).

All changes committed incrementally. We build/test at each step and update this log + DESIGN.md.

Build command reminder (see updated README.md for full details + Qt path):
```
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64" -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

---

## Risks, Mitigations & Open Items

**Risks:**
- Digital voice (P25/DMR) quality in real-world noisy conditions can be disappointing without significant tuning. Mitigation: deliver excellent analog first; treat digital voice as "bonus when conditions allow"; document limitations.
- APT sync robustness (noise, Doppler, weak signals). Mitigation: start with strong-signal test recordings + known good pipelines; provide manual sync assist or "capture buffer" for offline decode.
- USB throughput / multiple HackRFs on same controller. Mitigation: document recommended USB topology; support lower sample rates; provide health meters.
- Soapy module / driver installation friction on user machines. Mitigation: excellent README + in-app "Device Setup Helper" wizard with links to Zadig, pre-built Soapy packages, etc.
- Scope creep on "smart" algorithms. Mitigation: define clear MVP detection/classification behaviors; measure "good enough" with user feedback.
- Automatic modulation recognition (even SOTA) is never 100% accurate, especially on short bursts, low SNR, or non-standard signals. The analyzer can produce wrong or low-confidence results. Mitigation: strong classical + ML ensemble with visible confidence scores, easy user override, "apply as starting point for manual Receiver" rather than fully automatic, and clear disclaimers that the tool assists but does not replace operator skill. Generic decoding of unknown protocols will often yield only raw bits; full protocol reverse-engineering is a research activity, not a button.

**Open Items (to be resolved during or after planning, before or during early PRs):**
- Exact default buffer sizes and latency targets (measure on target hardware).
- Whether to ship a minimal set of Soapy modules or require separate install.
- mbelib licensing / patent considerations text (consult user/legal if needed; emphasize "unencrypted only").
- Whether to include basic TLE download + crude pass prediction in the satellite pane for v1 (low effort, high convenience for weather sats).
- Final decision on spectrum drawing widget (pure custom vs. small 3rd-party lib).
- CI strategy (GitHub Actions Windows runners with Qt + vcpkg can be slow; cache aggressively).

---

## Living Document & Process

- This plan (or a cleaned copy) will live in the repository as `DESIGN.md` (root or `docs/`) once the project is bootstrapped.
- **Update the design document** as we implement: new decisions, changed trade-offs, discovered constraints, and user feedback must be recorded. Do not let the doc go stale.
- Use the PR plan above as the backbone. Before starting a new PR, re-read the relevant section and update if reality has diverged.
- When in doubt on a feature or algorithm, prefer the simplest thing that is still professional and meets the stated goal; document the "why" and the "when we might improve it".

---

## Summary for Implementation Start (Post-Approval)

Once this plan is reviewed and approved (via exit from plan mode):
1. Create the project skeleton in `C:\Users\Blkph0x\Desktop\maulaudio_pro` following PR 1.
2. Add the initial `DESIGN.md` (this document or a polished export).
3. Set up submodules + first successful CMake + Qt "empty professional window" build.
4. Proceed PR-by-PR, updating the living design doc and git history.
5. Use the plan as the source of truth for scope and architecture decisions.

This approach minimizes the chance of getting stuck in bad architecture or feature bloat while still allowing smart, professional evolution.

---

**End of initial design document.**  
Ready for user review, questions, and adjustments before any code is written in the project.