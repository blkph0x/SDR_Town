# Redsea block and subcarrier decoder subset

Source: https://github.com/windytan/redsea
Commit: 7555c9f6259d50718697ee8c9f218ea012c6892c
Retrieved: 2026-09-17

Unmodified block_sync.cc/.hh, group.cc/.hh, constants.hh, options.hh,
util/util.hh and util/maybe.hh. Upstream LICENSE and per-file notices retained.
Block synchronisation/FEC and group representation are compiled natively.
The DSP subcarrier/liquid_wrappers and io/bitbuffer files are included in the
isolated MinGW DLL, linked to liquid-dsp commit
9e00870e25ce9ecf473b7474875a19a3dfc52ce9 (its own LICENSE applies).
Local adaptations: input.hh replaced with extracted io/mpx_buffer.hh to avoid
sndfile dependencies; subcarrier counters widened to uint64_t to prevent the
documented seven-hour sampling-grid wrap. No recovery algorithm changes.
tests/fixtures/rds-mpx-yksi.flac is upstream test/resources/mpx-testfile-yksi.flac,
unmodified. Upstream components-mpx.cc expects at least two groups, PI 0x6201,
and programme type Serious classical (14). Our three-group identity gate
intentionally does not confirm a station from this short recording.
