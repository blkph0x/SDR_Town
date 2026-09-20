# SDR Town 0.2.78 (experimental)

SSTV Auto now covers the modes you asked for, using **published VIS/timings**
(QSSTV `sstvparam.cpp` + SSTV Handbook ch.4), not invented tables.

## Auto path

1. Decode **VIS** (7-bit + parity).
2. If VIS is missing/corrupt, score **1200 Hz line-sync period** against known
   layouts (AVT skipped — no line sync).
3. Manual dropdown still forces a mode.

## Added vs 0.2.77

| Mode | Source |
|------|--------|
| Robot B&W 8 / 12 | Handbook + QSSTV VIS 2 / 6 |
| Robot 24 | QSSTV 160×120 VIS 4 (luminance; not a guessed YC matrix) |
| Wraase SC2-30 / 60 / 120 | Handbook RGB + QSSTV VIS 51 / 59 / 63 |
| AVT 24 / 90 / 94 / 188 | Handbook RGB, no line sync; QSSTV VIS 64 / 68 / 72, AVT188 VIS 74 |

Plus the 18 Dayton modes from 0.2.77 (Martin, Scottie, Robot 36/72, PD, SC2-180, Pasokon).

## Honest limits

- Robot 24 is **luma** from QSSTV geometry, not a full YC reconstruction.
- AVT has **no** 1200 Hz line sync; Auto needs VIS; forced AVT starts at the
  image (header may garbage the first lines).
- FAX480, narrow MP/MR, SC-1, Martin M3/M4, Scottie S3/S4 are **not** in this drop.
- Live RF image acceptance still open.

Decoder is the vendored MIT unexcellent/sstv crate with extra layouts.
