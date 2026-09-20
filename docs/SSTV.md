# SSTV receive development

Version 0.2.59 adds experimental live NFM input to **Robot36/Martin1 decoding**
and classic VIS inspection. Known-transmission RF acceptance remains open.

## Live NFM

1. Start and tune the main receiver to an SSTV transmission in NFM. P25 monitor
   or voice-follow mode must be off. SSTV does not tune or reconfigure the radio.
2. Open **Tools > SSTV Images**, choose **Live NFM - main receiver**, Automatic
   (or Robot36/Martin1), and a new output directory whose parent exists.
3. Press **Receive**. Scanlines appear progressively. **Finish and save** stops
   accepting new input, drains queued samples, and saves validated PNGs/report.
   **Cancel** or closing the window discards provisional output.

The source is raw NFM discriminator audio before speaker LPF/EQ/squelch/volume.
A retune, stream gap, overrun or mode/device loss aborts the session rather than
joining incompatible samples; restart explicitly. Sessions are bounded to six
minutes and four images. No automated repeated acquisition yet. Partial images
are labelled and retain black missing rows. HF USB/LSB audio is not supported
by this live source. Finishing after an image begins can save a partial image;
all rows received does not guarantee a noise-free image.

Independent recordings exercise the real live GUI/feed/converter/helper path,
but are not a substitute for an off-air image from a known transmission.

## Image decoding

**Tools > SSTV Images**, with Source set to **Recording**, opens a
nonmodal window. Select a mono recording, give a new output directory (parent
must exist), choose Automatic/Robot36/Martin1 and Decode. The image list shows
complete/partial status and row counts; selecting an item previews the original
PNG without changing the saved pixels. Open output accesses PNGs and the report.
One job runs at a time off the GUI thread. Cancel and closing the window request
cooperative cancellation and reap its helper. Cancellation is checked before
publishing outputs; if saving already began, the bounded save finishes instead.
The rest of the receiver is not retuned, muted or reconfigured by this window.
Decoded rows appear progressively with actual row counts, not an invented
completion/quality percentage. A latest-only preview handoff prevents accumulating
images in the GUI event queue. Failed/cancelled jobs clear the provisional preview.
The parser validates bounded row events and checks assembled preview pixels
against the final RGB file before publishing PNGs. Missing rows remain black;
preview quality is not an RF/protocol correctness guarantee.

```powershell
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd 'sstv decode "C:\recordings\sstv.wav" "C:\recordings\new-images" auto'
```

Use `auto` (VIS) or a mode id from Tools → SSTV Images (`robot36`, `martin1`,
`scottie3`, `fax480`, `mp73`, `sc124`, …). Manual mode locks on 1200 Hz line
sync if VIS is missing. Auto: 7-bit VIS, then QSSTV 16-bit VIS (MP/MR/ML),
then closest line-sync period. AVT has no line sync. FAX480 has no VIS.
Narrow 2172 Hz modes are not supported. MR175 is omitted (VIS collides with MR140).
Input is demodulated mono WAV/FLAC at
8..96 kHz, up to 360 seconds / 128 MiB, not IQ. Parent output directory must
exist; the image directory must be new. PNGs and `sstv-report.json` record mode,
rows, completeness, PCM/RGB hashes and exact backend revision. Partial pictures
have `.partial.png` filenames and black missing rows; `complete` means all rows,
not necessarily an undamaged picture. Silence can return no images. Check JSON
and `SSTV error:` text, not only the process exit code. A save failure can leave
already-written images; a retry must use a new directory.

The app-adjacent helper has no network or radio access and runs with a 120-second
deadline, 64-KiB diagnostic limit and maximum four pictures per job. Unsupported
modes fail explicitly. Progressive local stdout has a separate 4-MiB total and
4096-byte line limit; stderr remains capped at 64 KiB. The CLI intentionally
waits for this offline job; GUI decoding runs on its worker, never a live audio
callback. Helper is included in release assets.

GUI development regression: `python scripts/test_sstv_gui.py` loads the two
independent references into real Qt windows and checks saved-image parity with
direct decoding, event-loop responsiveness and preview screenshots. Core Qt
tests cover cancellation/close/teardown and errors without reference downloads.
The recording-dependent test is explicitly skipped unless the harness supplies
`SDR_TOWN_SSTV_GUI_FIXTURE`; no skipped fixture is counted as reception proof.

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
Data tone 1100 Hz is one, 1300 Hz is zero; start/stop are 1200 Hz. Eighteen Dayton-paper VIS codes are named (Robot 36/72, Martin M1/M2, Scottie
S1/S2/DX, PD50–290, Wraase SC2-180, Pasokon P3/P5/P7). Image decode uses the
same table. Other parity-valid codes remain `Unknown`.
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

Next: bounded live demodulated-audio routing using the verified scanline transport.
The NFM input queue (DEC-0095) has
fixed storage, nonwaiting producer, explicit gap/source-change events and
sample ordering tests. DEC-0099 connects it to the main NFM receiver. The
DEC-0097 converter now supplies continuous 48 kHz PCM from fractional input
rates using the bundled miniaudio resampler. It preserves state across blocks
and resets on explicit discontinuity; no artificial samples are added at EOF.
`python scripts/test_sstv_rate.py` checks full/partial independent recordings
after C++ conversion. Native tests cover fractional-rate tones, six-minute
sample counts, chunk equality, bypass and reset/error behavior. Real fractional
RF image reception is not yet qualified. The DEC-0098 C++ worker now combines
the queue, converter and helper, with bounded pipe writes and explicit EOF.
It aborts on an in-stream gap or changed identity and discards provisional
output on failure. The caller receives owned images after helper/file validation;
no temporary filenames are published. `python scripts/test_sstv_worker.py`
checks eight recording cases against the converted file decoder pixel-for-pixel.
Live GUI controls use the same worker. Receiver attach/detach is serialized
against try-lock-only publication; no queue allocations on RX. Gap recovery
requires explicit restart. Off-air image acceptance remains open.

### Developer streaming helper

The helper accepts `sdrtown_sstv.exe --stdin <integer-rate> <new-output-dir>
auto|robot36|martin1 [--progress]`. Supply raw mono signed 16-bit little-endian
PCM and close stdin at EOF. All prior mode/rate/image/session limits apply.
Rows arrive before EOF, but every output remains provisional until exit zero:
an odd final byte, read failure or excess samples invalidates the session.
The owning worker must continuously drain both output pipes, bound logs, enforce
a wall-time deadline, close the input and kill/reap on cancellation. This helper
does not provide RX controls or a continuously running unlimited receiver.

`python scripts/test_sstv_stream.py` verifies full/partial independent Robot36
and Martin1, forced/auto, with fragmented writes, identical metadata/pixels and
rows observed before closing stdin. It also checks malformed/oversized streams,
existing-output rejection and idle-pipe termination. The references stay in
build; none are redistributed. Reader unit tests are included in CTest for
SSTV-enabled builds.

Qualify Martin2/Scottie/PD modes with independent pictures before
advertising them. Satellite scheduling and public/weather payload decoding follow.
