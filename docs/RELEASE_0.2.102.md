# SDR Town 0.2.102 - Continuous in-band Aero watch

- Data and voice workers run together continuously on one SDR when ALL enabled
  channels fit its usable sampled passband and selected decoder budget. Enabled
  by default; disable Simultaneous in-band and Save timing for legacy rotation.
- Out-of-band or over-budget watch lists retain data/voice scheduling. No worker
  is silently omitted. Set Concurrent decoders to cover the desired list, up to
  16, subject to measured processing load and IQ gaps on your PC.
- Wheel zoom anchors the frequency under the pointer. Drag pans inside captured
  RF without tuning or adding channels. Double-click restores full span.
  Spectrum, waterfall, saved markers and click selection share the same axis.
- Channel-aware LO/DC avoidance is retained. No hard-coded 2 MHz shift, fictional
  RSPdx L-band notch or unverified gain automation is introduced. Native modems
  retain their required 48 kHz real IF input; this is not a new PFB/SIMD backend.
- Aero FIR history access is faster with exactly tested arithmetic equivalence.
  Synthetic 16-worker processing at 2.048 MS/s improved from about 0.51x to 0.41x
  RF duration on the development PC. At 10 MS/s it is still about 1.74x: NOT
  realtime. Use a lower sample rate that covers your channels and watch IQ gaps
  and processing load. These measurements do not qualify live satellite speech.

The larger multi-SDR workspace and cross-mode audio focus are NOT implemented
in this release. Global ownership migration remains T-0062. P25/shared audio
implementation is unchanged. Real satellite and high-rate hardware acceptance
still require tester evidence; unit tests do not prove RF sensitivity or speech.

Regular portable testing release. Extract the entire ZIP to a fresh folder.
SDRplay requires the official API 3.15+ and running service. This is not a signed
installer or replacement for the existing signed in-app updater release.
