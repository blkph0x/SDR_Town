# Native Classic Aero receive subset (DEC-0121)

- JAERO (MIT): jontio/JAERO, 1d4e515921244aec1d85a03f5f38b4e7818fdbe6.
- JFFT (MIT): jontio/JFFT, 4b74486e58e1d266f1cc3c570f3d073d40c353d6.
- libcorrect (BSD-3-Clause): quiet/libcorrect, ee82e6673a806dfdf0a969b975ab36596ecc5401.
- libaeroambe codec (ISC, wrapper MIT): jontio/libaeroambe,
  df7eebf17ca6396cc545bff4869dbeccc1e7dfcd.

Local changes: Qt 6 string compatibility; remove UI/database lookup dependency;
instance-owned modem state; sample-clock-driven framing liveness; bounded
convolutional unpack loops and zero-initialized unused decode tail; strict SU
CRC (zero fill cannot establish lock); direct CRC/SU/C-frame signals for the
adapter. Continuous MSK/OQPSK and R/T burst MSK/OQPSK are included.

Codec is an isolated shared library with a small C ABI. Its AMBE4800x3600 matrix
is the upstream mapping, not P25 AMBE3600x2450. Synthesis noise uses per-stream
MT19937 uniform samples instead of process-global libc rand(), for independent,
repeatable replays. ECC scratch variables are per-call rather than shared static
storage, eliminating an upstream concurrent-stream data race. No other
DSP/FEC/synthesis algorithm was substituted.

Retain upstream licenses/COPYRIGHT. Source license notices do not establish
patent clearance or authorization to receive private/encrypted traffic.
