# Satcom Receive Range Scan

## Quick Start

1. Open **Satcom Scanner** in the Satellite workspace. For another radio, use
   **Add receiver session** to open a separate Satcom session.
2. Select the **Radio** and open **Range Scan**.
3. Select a saved range or enter **From / To** in MHz.
4. Expand **Advanced scan settings** if needed to set mode, channel bandwidth,
   tune step and dwell. Set the signal detection threshold.
5. Press **Start scan**. Selecting or saving a range does not start reception.
   **Listen** controls playback; **Record audio** records the held channel.
   **Next signal** resumes the existing scan; **Stop** ends it and disarms
   automatic pass capture for that session.

The scan holds a detected signal until Next signal or Stop; this release does
not add voice classification or automatic silence-based resumption. Detection
uses the captured spectrum, so resuming near a strong carrier can reacquire it.
The threshold is relative signal power, not calibrated dBm or speaker volume.
Range controls are disabled while receiving. Stop before changing them.

**Save range...** stores the current draft with a name without applying it to
the radio. The trash button removes a selected custom range after confirmation.
Built-in ranges cannot be removed. Existing saved settings remain unchanged on
upgrade; new built-ins are added only if their names are absent.

## What the Ranges Mean

| Choice | MHz | Meaning |
| --- | --- | --- |
| Legacy UHF SATCOM downlinks | 243-270 | Legacy downlink search, analog NFM starting settings |
| UHF search | 240-270 | Broader search including the legacy region |
| UHF search | 240-322 | Mixed services and link directions, not downlink-only |
| UHF search | 335.4-399.9 | Mixed fixed, mobile and satellite services |

The US Coast Guard's published spectrum plan lists legacy MILSATCOM
downlinks at 243-270 MHz and uplinks at 291-318 MHz.
[Primary source, table 5](https://www.ntia.gov/sites/default/files/publications/coast_guard_stategic_spectrum_plan_nov2007_0.pdf).
The broader 335.4-399.9 MHz region is not exclusively satellite spectrum.
[NTIA band report](https://www.ntia.gov/files/ntia/publications/compendium/0335.40-0399.90_01MAY15.pdf).
Allocations and permitted reception vary by country. These presets are search
aids, not a channel database, current activity guarantee or legal authorization.

New UHF presets start at NFM, 25 kHz channel bandwidth/step and 80 ms dwell.
Those are editable analog starting settings, not universal signal parameters.
MUOS includes a separate WCDMA payload from legacy communications.
[US Navy description](https://www.navy.mil/Press-Office/News-Stories/Article/2249912/navy-satellite-system-receives-green-light-for-expanded-operational-use/).
This scanner does not add MUOS WCDMA, DAMA or encrypted-voice decoding.
It does not transmit.

## Timing and Display

The scanner sequentially retunes one selected SDR. It does not capture an
82 MHz range simultaneously. The displayed tune count is the inclusive nominal
grid; an off-grid upper endpoint is not an extra tune. The dwell-only time is a
lower bound excluding retuning, processing and signal holds. For example,
240-322 MHz at 25 kHz/80 ms is 3,281 nominal positions and 262.5 seconds of
dwell alone. Narrower ranges are more practical for known signals.

The spectrum shows the current captured RF span, not the full selected search
range. The status identifies the scan/held frequency. **Satellite Passes**
retains the existing catalogue, observer, Doppler and SSTV automation; its
downlink is independent of an unfinished Range Scan draft. **Activity** retains
bounded session messages. Logs now include applied range, mode, bandwidth,
step, dwell, nominal positions, device and ownership lease at scan start.

GUI and named-session web configuration use the same range validation.
The existing `configure` action additionally accepts integer `dwellMs`
(20-5000); frequencies remain Hz on the API. No new network permissions,
automatic transmission, external telemetry or background RF starts are added.

## Verification Scope

Regression fixtures exercise preset migration, draft persistence, explicit
start, invalid settings, named-session API validation, saved custom ranges,
compact/desktop layout and existing satellite-pass controls. They use isolated
profiles and do not require RF. Hardware reception and signal intelligibility
remain antenna/device/signal-dependent; no live MILSAT reception is claimed.
