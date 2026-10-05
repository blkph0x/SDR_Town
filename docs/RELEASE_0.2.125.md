# SDR Town 0.2.125 experimental

## Classic Aero receive hardening

- ACARS fragment reassembly now accepts the protocol's letter cycle (`A`-`Z`)
  and digit cycle (`0`-`9`), including wraparound.
- An 8400 bps C-frame with no CRC-valid subunits is treated as an erasure. It
  is counted and rejected without resetting the persistent Aero codec state.
- A codec-reported erasure/tone/mute word is replaced with silence before it
  reaches the audio sink. Repeat words remain available for normal concealment.
- The Inmarsat channelizer applies symmetric signed PCM conversion with fixed
  headroom, preventing positive-only full-scale clipping.

## Verification and limits

The native Inmarsat suite passes 21,708 assertions in 16 cases. The full local
CTest run passes all 17 suites, including GUI/device lifecycle checks and the
P25 protected-change guard. The audit's longer data-survey dwell remains an
operator profile choice; parser BCS/framing variants require protocol fixtures
before acceptance rules are changed. Physical L-band reception remains a
tester qualification item.

P25 decoder, following, RF processing and audio timing are unchanged.
Portable experimental release; original source contributions remain MIT and
the combined RTL-enabled distribution follows the repository's documented
third-party licensing and notices.
