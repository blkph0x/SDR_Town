# 0.2.95 Experimental Aero Watch Responsiveness

- Spectrum/waterfall refresh separately from slower map/status updates. Circular
  history avoids full-image scrolling and duplicate FFT rows.
- Choose a decoder rate, enable **Click to add**, and click multiple signals.
  Channels stay saved; duplicate frequency/rate clicks select the existing entry.
- Each active channel owns a persistent worker and full decoder/vocoder state.
  One bounded IQ block is processed in parallel and joined in channel order.
  Default two concurrent decoders; adjustable to four. Other groups are scheduled,
  not falsely described as simultaneously received outside one SDR's passband.
- One constellation display selects among the active channels. It plots real
  recovered symbols, with separate protocol-lock status; no fabricated dots.
- Watch-list/timing changes can apply live at block boundaries. They flush old
  audio, tune the new group and reacquire. Speaker focus is still one conversation.
- P25 decoding/audio and the shared radio/FFT producer are unchanged.

## Tester Checks

1. Start Inmarsat and check spectrum/waterfall responsiveness. Choose a rate,
   enable Click to add and save several signals. Click the same one twice.
2. Enable Automatic data / voice watch. Select each channel above the
   constellation; verify inactive groups do not show a stale locked plot.
3. Add/remove a channel while running. Confirm the group reacquires, old audio
   does not continue, and no hang occurs. Disable automatic watch to return manual.
4. Try two/four concurrent decoders and report processing load, IQ gaps and the
   short Inmarsat JSONL log. Out-of-passband groups still require retuning.

On the development PC the fixed four-channel benchmark uses about 0.64 s per
2.05 s of IQ, versus 2.46 s before. This is not a guarantee for every PC or proof
of live satellite speech. Reference GUI/CLI PCM remains byte-identical; physical
antenna and intelligible-conversation qualification still require tester IQ.

Diagnostics remain opt-in. Constellation arrays are not uploaded.

## Verification

- Full local and packaging test suites: 11/11 passed.
- Clean Windows GitHub CI passed: run 36095455129; YAML validation also passed.
- Extracted portable passed live RTL watch edits and four GUI/CLI reference
  replays with byte-identical PCM. This is not live Aero speech acceptance.
- All eight published assets were downloaded and matched local SHA256/size;
  signed updater and installer/package validation passed.
- Source e46f437; release tag d7979ee. P25 DSP/audio unchanged.
