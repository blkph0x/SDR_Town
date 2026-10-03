# GGMorse core

Upstream: https://github.com/ggerganov/ggmorse
Pinned commit: `7b4822a8cfdbb1addfe497f3ae8186f142a4ee79` (MIT, LICENSE retained).
Only the library core is vendored; no SDL, microphone, networking or examples.

SDR Town changes (DEC-0167): explicit standard includes for MSVC; remove
unsolicited decoded-text stdout writes; replace the StackOverflow-derived FFT
(not vendored) with the already vendored MIT JFFT, per-instance unnormalized
forward transform. Native decoder output still uses takeRxData. Public input
uses a saturated Goertzel startup counter; the unused STFFT sample counter is
removed to avoid signed overflow during unattended reception. Wrapper input
is bounded/validated by CwDecoder, with an 8th-order miniaudio anti-alias
resampler to the backend's 4 kHz rate. Character recognition remains upstream.

Upstream describes automatic pitch 200-1200 Hz and speed 5-55 WPM. These are
algorithm search bounds, not a claim of guaranteed on-air accuracy. Adaptive
recognition has a three-second analysis history and can make errors on speech,
noise, very short transmissions and irregular hand-keying. GUI labels it
experimental; it never operates radio controls based on decoded characters.
