# Repeater Control Monitor

Opt-in, receive-only tooling for observing how an old UHF repeater is remotely
enabled or disabled (DTMF on the input, CTCSS/DCS changes, carrier open/close).
Nothing is transmitted.

## Enable

In **Receiver Controls → Repeater Control Monitor**:

1. Leave **Enable** unchecked until you need it (DSP/DTMF stay idle when off).
2. Set **Output** / **Input** MHz (AU UHF CB example: `476.4625` / `477.2125`, or press **+750 kHz**).
3. Optionally enable **Dual-watch input**, **Log DTMF**, **Log CTCSS/DCS**, **Log carrier**.
4. Use **Tune out** / **Tune in** for single-channel checks when dual-watch cannot run.

## Heard list

With **Enable** on, detections append as plain lines (not raw log tokens):

- `CTCSS  88.5 Hz  (output)`
- `DCS  023N  (input)`
- `DTMF  1337  (input)`
- `Carrier open  (input)`

DCS shows one radio-style code (normal polarity preferred). DTMF rows are completed
sequences only. Use **Clear list** to reset the view.

## Dual-watch

When Enable + Dual-watch are on, NFM is selected, and the device sample rate covers
both legs (about `|input−output| + channel BW` inside ~90% of the IQ bandwidth),
SDR Town:

- Soft-centers the tuner near the midpoint when needed
- Plays **output** audio
- Silently DDCs the **input** for DTMF / CTCSS / DCS / carrier events

Typical AU 750 kHz pairs fit at ≥ ~2.048 Msps. Narrow rates keep DTMF/events on
whatever frequency you are tuned to; the status line explains why dual-watch is idle.

## Status API

`/v1/status` includes:

- `tones.dtmf` — last digit/sequence when the monitor is enabled
- `repeater` — enable flags, pair frequencies, dual-watch reason, recent events

## CLI

```powershell
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'tones dtmf "discriminator.wav"'
```

## Safety

Display and logging only. No tone generation, no audio mute-by-tone, and no
automatic wideband search for unknown VHF/UHF link radios.
