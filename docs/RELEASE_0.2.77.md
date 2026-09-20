# SDR Town 0.2.77 (experimental)

SSTV robustness on the **pinned** unexcellent/sstv Dayton-paper decoder.

## What is now wired

- **18 modes** (VIS auto + manual): Scottie 1/2/DX, Martin M1/M2, Robot 36/72,
  PD 50/90/120/160/180/240/290, Wraase SC2-180, Pasokon P3/P5/P7.
- **VIS** names match those codes. Automatic uses the header; a forced mode
  locks on **1200 Hz line sync** (crate behaviour — that is the slant lock).
- **1–2.5 kHz** pre-filter on recorded PCM. Helper timeout **540 s** (PD/Scottie DX).
- Live scanline overlay; brightness/contrast on the preview (does not rewrite
  saved PNGs). PNG tEXt: mode, UTC, rows, complete.
- Helper `--selftest` encode/decode Robot36: complete=true.

## Still not in this decoder (honest)

AVT 24/90/94/188, Robot 8/12/24 B&W, Wraase SC2-30/60/120. No separate Hough
slant. Demod is the crate’s midline-crossing estimator, not a PLL. Live RF
image acceptance remains open.

Pair FUBAR **1.1.39** + `SdrTownControl-0.2.77-win64.dll` (or keep 0.2.76 DLL
for control; SSTV is Town-side).
