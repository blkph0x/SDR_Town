# SDR Town 0.2.90 (experimental)

SSTV RF selection and sideband repairs are included in the source, installer
and portable build. The SSTV image helper is bundled. This also includes the
Windows CI packaging-test repair. P25 audio processing is unchanged.

## Changes

- Separate RF demodulation and image-format selectors in SSTV Images.
- RF Auto checks USB, LSB and NFM for a known, parity-valid classic VIS header.
  It retains the header, rejects ambiguous detections and holds the selected
  route for the session. It does not change the main speaker mode or tune RF.
- Manual USB, LSB, NFM and AM reception, including image-format Auto.
- Correct SSTV SSB passband: 3 kHz audio, preserving VIS and image tones.
  Satellite SSB SSTV uses the same correction and keeps its catalogue mode
  and digital Doppler path.
- Independent worker-owned RF reader; explicit errors on retune, lost samples
  or device changes rather than stitching incompatible input together.
- Local API /v1/sstv/live accepts optional rfMode; status and saved reports
  identify requested/detected routes. Existing clients default to RF Auto.

## Test This Build

1. Install the setup EXE or extract the portable ZIP to a new folder.
2. Start the main receiver and tune accurately to a known SSTV frequency.
   Disable P25 monitoring/follow for this receiver.
3. Open Tools > SSTV Images. Select Live RF - main receiver frequency,
   RF demodulation Auto, Image format Automatic, and a new output folder.
4. Press Receive before the transmission starts. Check Detected RF route,
   progressive scanlines, then Finish and save. Retuning requires a new session.
5. Compare with a manual RF route on the same known signal. Report app version,
   device, frequency, RF route, image format and the saved image/report. Share
   captures only when requested and appropriate; do not publish private data.

## Verification And Limits

Automated coverage includes USB/LSB/NFM acquisition, manual AM, invalid parity,
ambiguous sidebands, stream gaps, irregular blocks, GUI controls and recorded
image round trips. Existing full/partial Robot36 and Martin1 worker/GUI tests
also pass. These are not on-air hardware or satellite-pass certification.

Local Release CTest passed all four suites. The extracted portable app, bundled
SSTV helper and RF-control API passed smoke checks without developer runtime
paths. [Windows CI](https://github.com/blkph0x/SDR_Town/actions/runs/35986929161)
also passed build, core/GUI/SSTV tests and packaging. All eight published asset
sizes and SHA-256 digests match the verified local files.

RF Auto requires a classic 7-bit VIS header and accurate tuning. Extended-VIS
or headerless transmissions need a manual RF mode; image-format Auto still
supports its existing VIS/line-sync paths. AM/DSB may be ambiguous in RF Auto.
Noise after a real image can still create provisional partial images (ISS-0013).
Digital STWN is file-only and is not an interoperable EasyPal decoder.

FUBAR is not updated by this release; its website has no separate RF selector.
Use the included version-matched control DLL for pairing. Installer, portable
ZIP, control DLL, checksums and signed updater metadata are supplied together.
The updater channel remains experimental.
