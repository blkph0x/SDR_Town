# SSTV receive development

Version 0.2.57 adds **experimental recorded-audio Robot36/Martin1 images** and
classic VIS inspection. No live SSTV, GUI preview, or demodulator changes yet.

## Image decoding

```powershell
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'sstv decode "C:\recordings\sstv.wav" "C:\recordings\new-images" auto'
```

Use `auto`, `robot36` or `martin1`. Input is demodulated mono WAV/FLAC at
8..96 kHz, up to 360 seconds / 128 MiB, not IQ. Parent output directory must
exist; the image directory must be new. PNGs and `sstv-report.json` record mode,
rows, completeness, PCM/RGB hashes and exact backend revision. Partial pictures
have `.partial.png` filenames and black missing rows; `complete` means all rows,
not necessarily an undamaged picture. Silence can return no images. Check JSON
and `SSTV error:` text, not only the process exit code. A save failure can leave
already-written images; a retry must use a new directory.

The app-adjacent helper has no network or radio access and runs with a 120-second
deadline, 64-KiB diagnostic limit and maximum four pictures per job. Unsupported
modes fail explicitly. The CLI intentionally waits for this offline job; it is
not connected to a GUI/live audio callback. Helper is included in release assets.

Backend: MIT [unexcellent/sstv](https://github.com/unexcellent/sstv/tree/16bf34aac81b0041f5fdce52a1aef64eea0d5f6e),
pinned with Cargo.lock (libm 0.2.16). Build with Rust 1.88.0 and
`cmake -S . -B build -DSDR_TOWN_ENABLE_SSTV_IMAGES=ON`; Cargo must be on PATH or
under `build/toolchains/cargo/bin`. Default OFF permits core-only builds without
Rust; then image decoding reports unavailable. Native VIS inspection still works.
Notices ship in `licenses/sstv`. Recordings and reference images are not shipped.

Independent off-air Robot36 produces 240/240 rows and RGB mean absolute error
10.4065 forced /14.3024 auto against upstream patch.png (upstream threshold <15). Independent Martin1
produces the 320x256 BBC test card; raw RGB error against colaclanth's decoder
image is 20.7794. Martin1 has visible noise/colour differences and is experimental,
not pixel-identical to that decoder. These are recorded-file tests, not live RF
qualification. `scripts/test_sstv_backend.py` reproduces reference checks;
`scripts/test_sstv_images_cli.py` checks the actual app and its failure cases.

## VIS inspection

```powershell
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'sstv inspect "C:\recordings\sstv.wav"'
build/bin/Release/sdr_town_tests.exe '[sstv]'
python scripts/test_sstv_cli.py --reference build/reference-sstv/test/data/m1.ogg
```

Input: mono WAV/FLAC at 8..96 kHz, maximum 120 seconds and 64 MiB. Input must be
demodulated audio, not IQ or WFM multiplex. Inspection does not open an SDR or
audio output. JSON reports sample rate/count, header events with seven-bit VIS
codes and approximate start/end sample positions, candidate and rejection counts,
and `imageDecoded: false`. No headers is a legitimate negative result. Like the
other diagnostic CLI commands, textual errors must be checked; process exit
alone is not a positive decode result.
Windows batch commands preserve Unicode via Qt arguments, and this SSTV loader
uses the wide-file API. This does not certify Unicode handling in every other
legacy file command.

The classic header has two 300 ms 1900 Hz leaders, a 10 ms 1200 Hz break,
30 ms start, seven LSB-first data bits plus even parity, and 30 ms stop.
Data tone 1100 Hz is one, 1300 Hz is zero; start/stop are 1200 Hz. Eight known
IDs are named: Robot 36/72, Martin M1/M2, Scottie S1/S2/DX and PD120. A name
only identifies a validated header, not implemented image-mode support.
Other parity-valid seven-bit codes are reported as Unknown; narrow and extended
VIS are not supported by the inspector. Image acquisition is delegated to the
pinned backend; this is not a promise of arbitrary headerless/late-entry recovery.

## Implementation and limits

SstvVisDetector consumes bounded blocks on one owner thread, using <=910 ms
history plus rounding room. Explicit reset starts a new sample epoch; callers
must reset on loss, retune or source change. Invalid/non-finite input resets and
throws instead of splicing a damaged stream. Search runs on a 1 ms sample-clock
grid, not GUI/wall-clock scheduling. Recognition is independent of caller chunk
size; successful headers are not re-emitted on overlapping searches.

Ten-ms Goertzel tone measurements require >=75% normalized sinusoidal energy
and a 2:1 winner ratio. These conservative diagnostic thresholds are not a weak-
signal specification. Detection positions can differ from true tone transitions
by the search/measurement resolution; they are not yet image line-sync anchors.
Rejected counts represent candidate offsets, not unique rejected transmissions.
No confidence, image quality or RF sensitivity score is invented from these counts.

## Sources and independent validation

Protocol facts checked against [QSSTV](https://github.com/ON4QZ/QSSTV/tree/8c27d6d169d8c6c197eb47c2089870e39bc06a02),
`src/sstv/sstvtx.cpp` and `sstvparam.cpp`; and
[colaclanth/sstv](https://github.com/colaclanth/sstv/tree/3e556eee8ad4c4425799cb652bac26ee58f8e113),
`sstv/spec.py`, `decode.py`, `test/data/m1.ogg`. Both source projects are GPLv3;
no implementation code is copied/linked. The reference audio is not shipped.
Clone the pinned reference under build for local QA; the test script converts
only its first three seconds to a temporary mono WAV and removes it afterwards.
The script verifies upstream audio SHA-256
`051bdb63f6f84abd3abdf90281361bf79bd1c95ad0c4e2255fb2657b1bd37c16`.
This host identifies VIS 44 (Martin M1) at approximately 0.832 s, with valid parity
and start/stop framing. This proves this header case, not the image or a live pass.
Synthetic tests alone are not independent reception proof. Live RF acceptance,
fading and broad offset/noise characterization remain open.

Next: GUI preview/gallery and cancellable replay, then bounded live demodulated-
audio routing. Qualify Martin2/Scottie/PD modes with independent pictures before
advertising them. Satellite scheduling and public/weather payload decoding follow.
