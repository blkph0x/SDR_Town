# RDS development milestone

Implemented: a native bitstream decoder using redsea's pinned block sync,
CRC and burst-error correction. Only complete validated groups reach metadata
assembly. PI needs three observations; PS needs two matching complete cycles.
Types 0A/0B provide PS/TP/TA; 2A/2B provide radiotext. PI changes reset station
state; radiotext A/B or version changes clear old text. Partial text is not
published. PTY is numeric because RDS/RBDS label mappings differ by region.
Text currently uses basic Latin; unsupported characters become `?`.

Recorded mono MPX decoding now uses pinned Redsea/liquid-dsp carrier and timing
recovery in an isolated Windows DLL. GUI receivers now decode RDS automatically
in WFM (including AUTO when it resolves to WFM). Station name, PI, numeric PTY,
traffic announcement and radiotext appear above the spectrum after validation.
No change to P25 decoding, gates or scheduling is included.

Live source discontinuities reset the data branch without resetting speech DSP.
Receiver inactivity, mode changes and retuning hide unrelated station metadata;
five seconds without a complete group hides stale identity. This is presentation
freshness only, not a decoder/audio timeout. Normal audio LPF and squelch do not
filter the independent pre-audio MPX tap. Narrow RF channel bandwidth can still
remove RDS information, so use the normal full WFM channel width.

## Test and use

```powershell
build/bin/Release/SDR_Town.exe --cli --cmd 'rds bits "tests/fixtures/rds-reference.bits"'
build/bin/Release/sdr_town_tests.exe '[rds]'
python scripts/test_rds_cli.py
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'rds mpx "tests/fixtures/rds-mpx-yksi.flac"'
```

Input is already differential-decoded RDS bits, MSB first, ASCII 0/1 and
whitespace only, maximum 2 MiB. IQ, MPX PCM, WAV and hexadecimal group files
are not accepted by this command. JSON reports complete/rejected groups,
corrected blocks and the final validated station snapshot. Error diagnostics
follow existing CLI text conventions; process exit status alone is not a
decoder-success gate. Reset the decoder on any input discontinuity/retune.

The fixture is redsea's `test/components-bits.cc` reference group repeated
five times: `22E1 2583 2065 6920`. Source revision and upstream notices are in
`external/redsea-block/UPSTREAM.md` and `LICENSE`; notices accompany deployment.

## Multiplex contract

`Demodulator::demodulateToAudio(..., FmMultiplexBlock*)` optionally copies
WFM discriminator samples before audio filtering, de-emphasis or squelch.
The default null argument makes no copy. Audio output is unchanged. Caller
owns the returned block and must use the same DSP locking as the demodulator.
There is no hidden queue or background consumer.

Samples are normalized to 75 kHz FM deviation; `sampleRate` is the actual
post-decimation rate, not an assumed 192 kHz. `firstSample` is relative to an
epoch. Mode/tuning/rate/reset changes or an omitted tap break continuity.
Call `resetState()` when the source reports missing IQ. This API does not infer
source timestamps or detect upstream sample loss independently.

DEC-0079 fixes the original tap-grid limitation: the optional data tap now has
its own causal FIR history and persistent decimation phase, using the existing
channel coefficients. Arbitrary input partitions retain one sample grid without
changing the legacy speech path. Tests compare whole-input and 137/1000-sample
chunks and verify tap-enabled/disabled audio equivalence.

`rds mpx` accepts mono WAV/FLAC at 128..384 kHz, at most 120 seconds. It rejects
normal speaker-rate audio. The recorded upstream fixture is only 0.7 seconds:
two CRC-valid groups are expected, but station identity intentionally stays
unconfirmed because three matching PI observations are required. Native tests
also verify its group PI 0x6201 and PTY 14 against the upstream reference.

Build requires x86_64-w64-mingw32 GCC/G++ and make for the isolated DSP DLL;
the main application remains MSVC. Configure SDR_TOWN_RDS_CC, SDR_TOWN_RDS_CXX
and SDR_TOWN_RDS_MAKE when they are not on PATH. Run `git submodule update --init
external/liquid-dsp` on a fresh checkout. `SDR_TOWN_BUILD_RDS_DSP=OFF` permits a
core-only build; MPX decoding then requires a compatible separately installed
DLL. Runtime loads only beside the executable, checks C ABI version 1, and
never passes C++ objects or allocator ownership between compilers. The tested
DLL imports only KERNEL32.dll and msvcrt.dll. Deployment includes both licenses.

## Next gates

DEC-0087: excessive RF gain prevented RDS reception on this host's 98.1 MHz
station. Controlled direct capture at 40.2 dB had ~32% raw ADC-rail components
and no groups, while 19.7 dB decoded 53 groups cleanly. GUI requested 20 dB
passes with PI 0x2981, PS i98FM and radiotext. This is station/hardware-specific,
not a universal gain setting; more gain is not always better. No default changed.
Automated temporary-gain test (restores previous gain five seconds before exit):
`python scripts/test_rds_live_gui.py --frequency-mhz 98.1 --expect-pi 0x2981 --expect-ps i98FM --rf-gain 20 --parity --output build/rds_gain_new`
Requires device 0 and >=30 seconds. The control API is authenticated and bound
to loopback on a temporary port. Separate shutdown issue T-0026 was reproduced
and fixed through runtime deployment, not through reception tuning; see
[native runtime QA](NATIVE_RUNTIME_QA.md) for the CDB evidence and limits.

Live automation (uses actual hardware, fails if no confirmed station):
`python scripts/test_rds_live_gui.py --frequency-mhz 98.1 --expect-pi 0x2981 --expect-ps i98FM`
Run without another SDR Town instance using the device. Optional expected values
are assertions, never injected metadata. Results/logs go to build/rds_live_qa.
The initial 45-second run received 402 groups, PI 0x2981, PS i98FM and song text.
Separate native UI inspection confirmed station text and drag-retune clearing.


1. Extend recorded MPX testing with longer station recordings and measured
   frequency-offset/noise cases. Short upstream fixture and partition gates pass.
2. Expand live tests beyond the initial 98.1 MHz reception; test more receivers
   and weak-signal conditions. A successful local station is not universal RF QA.
3. Expand Decode Log integration; the shared decoder registry and RDS adapter
   are now implemented with same-input native/adapter parity tests.
4. Add complete RDS character conversion and regional RBDS PTY labels.
