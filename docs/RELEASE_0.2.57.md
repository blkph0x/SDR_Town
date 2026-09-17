# SDR Town 0.2.57 (experimental)

## New in this build

- Offline SSTV Robot36 and Martin1 image decode to PNG with automatic/manual
  mode selection, partial-image status and a JSON reproducibility report.
- Classic SSTV VIS header inspection with parity/framing validation and
  sample-clock positions, including unknown valid seven-bit mode identifiers.
- Bounded app-adjacent MIT image helper, pinned source/lockfile and dependency
  notices. No runtime network/download requirement for decoding.
- Windows batch commands preserve Unicode paths through Qt arguments.
- README and SSTV command/build/validation documentation updated.

## Commands

```text
SDR_Town.exe --cli --no-control-server --cmd "sstv inspect recording.wav"
SDR_Town.exe --cli --no-control-server --cmd "sstv decode recording.wav new-images auto"
```

Image input: demodulated mono WAV/FLAC, 8..96 kHz, <=360 seconds /128 MiB.
VIS inspection: <=120 seconds /64 MiB. Quote individual paths containing spaces
inside the command. Output image directory must not exist; parent must exist.
Check report `images`/`complete` and error text, not process exit alone.
Manual modes: `robot36`, `martin1`. Other image modes are not advertised.

## Verification and limits

284 core/Qt cases and existing RDS/CTCSS/DCS/registry CLI regressions pass.
Independent Robot36 off-air input passes upstream reference-image error gate;
Martin1 reconstructs the independent BBC test card with visible noise/colour
differences. Actual app and helper output match pixel-for-pixel with matching
options. Auto/manual, partials, Unicode, silence, input bounds and overwrite
protection tested. These are offline experimental results, not live SSTV proof.

Existing P25 paths are unchanged. GUI SSTV preview/live reception and public
satellite decoders remain next work. Known-tone RF checks still await hardware.
No broad P25 clear-audio certification or new protocol support is implied.

Installer, portable ZIP, standalone control DLL, SHA-256 checksums and signed
update manifest are provided. Experimental channel is published as GitHub Latest
for existing testers' updater. Binaries are not Authenticode-signed.
