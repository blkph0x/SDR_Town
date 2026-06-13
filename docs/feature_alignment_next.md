# Feature Alignment And Next Build Step

Updated: 2026-06-11

This maps the requested target feature set against the current codebase and turns the gaps into a concrete next implementation step.

## Alignment Summary

| Area | Requested | Current Status | Notes |
| --- | --- | --- | --- |
| Hardware | RTL-SDR all versions | Partial | Real path uses SoapySDR plus RTL-SDR defaults/fallback. This should work for common RTL variants when the Soapy RTL module and WinUSB driver are installed, but there is no explicit variant database or device profile layer yet. |
| Hardware | SDRplay RSP series | Partial | Generic SoapySDR devices can enumerate, but there is no SDRplay-specific profile, gain element mapping, IF/RF gain handling, or documented SDRplay module flow. |
| Hardware | 2.4 MHz sample rate | Yes | RTL defaults include 2.4 MS/s and Device Manager allows sample-rate changes. Default remains conservative at 2.048 MS/s for RTL stability. |
| Hardware | RF gain 0-49 dB | Yes | RTL fallback/profile now clamps to 49.6 dB and probed devices still use advertised ranges. |
| Hardware | Frequency correction | Yes | Per-device PPM is persisted, shown in Device Manager, exposed via CLI, applied live through native Soapy correction or corrected-tune fallback. |
| Receiver | Auto band-mode | Mostly | Auto mode now checks built-in band plans before using the signal classifier. Next improvement is editable/importable band plans plus hysteresis controls. |
| Receiver | Squelch control | Yes | Main GUI and CLI control calibrated RF squelch against receiver bandwidth with live SIG/NF markers. |
| Receiver | Saved frequencies | Yes | `saved_frequencies.json` stores name, frequency, mode, bandwidth, LPF, squelch, and tags. GUI and CLI support add/list/tune/delete flows. |
| Display | 64K FFT waterfall | Gap | Current live FFT is fixed at 8192 bins. History-based zoom is good, but not 64K. |
| Display | Zoom 1-10x | Partial | Mouse wheel/keyboard zoom exists. No explicit 1-10x control or saved zoom presets. |
| Display | Contrast and decay | Partial | Color min/max exists and spectrum peak decay exists internally. No named contrast/decay controls. |
| Display | 3 display presets | Gap | No display preset persistence yet. |
| Display | Palettes: GQRX, Matrix Rain, Dark Forest | Gap | Current palette is one hard-coded heat map. No palette engine. |
| Display | Wave height and speed | Gap | No waterfall speed or spectrum/wave-height controls. |
| Audio DSP | Noise reduction | Gap | No NR stage, threshold, or smoothing controls. |
| Audio DSP | Notch filter | Partial | WFM pilot notch exists internally. No user notch with frequency/depth. |
| Audio DSP | 5-band EQ 80 Hz-12 kHz | Gap | No EQ stage or controls. |
| Modes | WFM, NFM, AM, USB, LSB | Yes | Core modes exist in GUI, CLI, and demod pipeline. |
| Modes | CW | Gap | SSB can be used manually, but there is no dedicated CW mode, BFO pitch, narrow filters, or tone display. |
| Modes | DRM digital radio | Gap | No DRM demod/decode path. |
| Modes | Auto BW selection | Yes | Manual and Auto BW are implemented around the tuned frequency. |
| Modes | Band auto-mode | Mostly | Built-in plans cover FM broadcast, airband AM, NOAA/weather sat starter, 2m/70cm amateur, marine VHF, and AU UHF CB. Needs user-editable plan database and hysteresis. |
| Solar | SFI, A/K, X-ray, aurora, MUF, conditions | Gap | No solar data module, cache, UI panel, or auto refresh. |
| Solar | HamQSL banner and DXMaps integration | Gap | No network integration yet. Needs source/API validation before implementation. |

## Current Pass Complete

**Feature Pack A: Hardware Profiles, Frequency Correction, Saved Frequencies, And Band-Plan Auto Mode** is now implemented in the main app/CLI path.

Completed:

1. Added frequency correction in PPM:
   - Persist per device.
   - GUI spin in Device Manager.
   - CLI command: `ppm <dev> <ppm>`.
   - Prefer Soapy `setFrequencyCorrection` when available.
   - Fallback to corrected LO/tune math when the device driver does not expose native correction.
2. Added saved frequencies:
   - `saved_frequencies.json` in the app data directory.
   - Fields: name, frequency Hz, mode, bandwidth Hz, LPF, squelch, tags.
   - GUI table with Tune/Add/Delete/Refresh.
   - CLI commands: `fav list`, `fav add`, `fav tune`, `fav del`.
3. Added built-in band plans:
   - FM broadcast, airband AM, UHF CB AU 12.5 kHz NFM, marine, ham 2m/70cm, weather sat starters.
   - Auto mode first checks band plan, then signal classifier.
4. Tightened RTL gain profile to driver-realistic 0-49.6 dB while keeping generic devices range-based.

Still to do from Pack A:

1. Move built-in band plans to editable `band_plans.json` with import/export.
2. Add confidence/hysteresis so AUTO does not bounce modes every FFT frame.
3. Add SDRplay-specific notes/profile hooks:
   - Recognize common Soapy driver keys/names.
   - Do not fake support beyond Soapy availability.
   - Document required SDRplay API/Soapy module setup.

## Best Next Build Step

The next build should be **Feature Pack B: Display Engine**. The radio workflow is now corrected, recallable, and band-aware enough to support the next visible leap: higher FFT resolution, professional palettes, display presets, and explicit waterfall controls.

## Following Packs

**Feature Pack B: Display Engine**

- User-selectable FFT size: 8192, 16384, 32768, 65536.
- Palette engine: current, GQRX-style, Matrix Rain, Dark Forest.
- Explicit zoom 1-10x, contrast, decay, waterfall speed, spectrum height.
- Three display presets persisted to JSON.

**Feature Pack C: Audio Cleanup DSP**

- User notch filter with frequency/depth/Q.
- 5-band EQ: 80 Hz, 250 Hz, 1 kHz, 3 kHz, 12 kHz.
- NR stage with threshold/smoothing and per-mode sensible defaults.

**Feature Pack D: Modes**

- CW mode with BFO pitch, narrow filters, and tone readout.
- DRM investigation and integration plan. DRM is much larger than adding another analog demod mode.

**Feature Pack E: Solar/Propagation Panel**

- QtNetwork auto-refresh every 10 minutes.
- Cached solar data model.
- SFI, A-index, K-index, X-ray flux, aurora, MUF, band conditions.
- HamQSL/DXMaps integration after verifying current feed/API behavior.

## Recommendation

Do Feature Pack B next and keep Feature Pack A hardening in the background. The highest-impact user-visible upgrade is now the 64K display engine with palette/preset controls, while later scanner/P25 work benefits from the saved-frequency and band-plan foundation added here.
