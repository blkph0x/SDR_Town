# Satcom Receive Range Scan

## Quick Start

1. Open **Satcom Scanner** in the Satellite workspace. For another radio, use
   **Add receiver session** to open a separate Satcom session.
2. Select the **Radio** and open **Range Scan**.
3. Select a saved range or enter **From / To** in MHz.
4. Smart wideband scan and automatic squelch are enabled by default.
   Expand **Advanced scan settings** to set mode, channel bandwidth, channel
   search step, dwell, noise margin, quiet hold and the separate monitor volume.
5. Press **Start scan**. Selecting or saving a range does not start reception.
   **Listen** controls playback; **Record audio** records the held channel.
   **Next signal** skips the foreground channel until the next range sweep;
   **Stop** ends it and disarms
   automatic pass capture for that session.

The smart scan holds the captured band while an enabled channel is active and
resumes when all watched activity has been quiet for the configured hold.
Activity is RF power above local noise, not confirmed intelligible speech.
A continuous carrier can hold the scan; skip it or turn off its Watch checkbox.
Automatic squelch defaults to 12 dB above local noise, with 4 dB hysteresis.
These are relative levels, not calibrated dBm or speaker volume.
Range controls are disabled while receiving. Stop before changing them.

## Saved Channels

**Channels** lists discovered and manually added frequencies with an editable
label, **High / Medium / Low** priority, **Watch** checkbox and current status.
Edits apply while receiving and survive reopening. Newly found activity is saved
at Medium priority. Up to 128 channels are remembered; at most eight active
current-passband channels have independent demodulators.

The highest active priority is heard. Equal priorities keep the current
conversation; after it becomes quiet, another active channel can take over.
Other channels remain visible. Audio is one foreground mono stream, not a
mixture of conversations or left/right stereo. A background demodulator does
not queue old audio for later playback. Smart audio uses the default playback
device and its own volume/queue; P25 and Listen queues are untouched.

Remove forgets an entry, so discovery can add it again. Turn **Watch** off to
keep a persistent exclusion. Frequencies outside the search range stay saved
but are not monitored. Frequencies outside the current captured band are
explicitly marked: one SDR cannot receive them while holding another band.
Separate receiver sessions can use separate SDRs.

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

Smart scan covers one SDR's range in overlapping wideband captures, alternating
new coverage with saved-channel revisits. Search step is the channel raster
inside each capture, not the hardware tuning step. The actual sample rate
determines coverage. An 82 MHz range is not received simultaneously.
The centre spike and outer capture edges are excluded from activity detection;
overlap makes those frequencies observable off-centre on another tile.
The sample rate must be at least nine times the channel bandwidth.

Turning Smart wideband scan off retains the earlier sequential scanner.
Its displayed tune count is the nominal inclusive grid and dwell-only time
excludes retuning, processing and holds. That legacy mode holds until Skip/Stop.

The spectrum shows the current captured RF span, not the full selected search
range. The status identifies the scan/held frequency. **Satellite Passes**
retains the existing catalogue, observer, Doppler and SSTV automation; its
downlink is independent of an unfinished Range Scan draft. **Activity** retains
bounded session messages. Logs include applied range, mode, bandwidth,
step, dwell, device and ownership lease at scan start, plus smart tile
acknowledgments, activity changes, foreground switches and discontinuities.
Session status exposes sample counts, processing time and slow-block counts.
Three seconds without fresh current-tuning IQ stops the smart scan with an
error; old display spectra cannot keep a channel locked.

GUI and named-session web configuration use the same range validation.
The existing `configure` action additionally accepts integer `dwellMs`
(20-5000), `smartScan`, `autoSquelch`, `squelchMarginDb`, `quietHoldMs` and
`smartVolume`. `watch_set` accepts `frequencyHz`, `label`, integer priority
(0 Low, 1 Medium, 2 High) and `enabled`; `watch_remove` removes a frequency.
`smart_volume` changes only this session's monitor volume.
Frequencies remain Hz on the API. No new network permissions,
automatic transmission, external telemetry or background RF starts are added.

## Verification Scope

Regression fixtures exercise preset migration, draft persistence, explicit
start, invalid settings, named-session API validation, saved custom ranges,
compact/desktop layout and existing satellite-pass controls. They use isolated
profiles and do not require RF. Hardware reception and signal intelligibility
remain antenna/device/signal-dependent; no live MILSAT reception is claimed.
