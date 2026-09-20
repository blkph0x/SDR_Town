# SDR Town 0.2.79 (experimental)

Leftover SSTV families from the SSTV Handbook and QSSTV `sstvparam.cpp`,
with encode/decode round-trips of every mode. FUBAR 1.1.40 uses the same list.

## Auto path

1. 7-bit VIS + even parity (Dayton / handbook).
2. If that is not a known 7-bit mode, QSSTV **16-bit** VIS (`0xNN23`) for MP/MR/ML.
3. If VIS is missing, score **1200 Hz line-sync period** and take the closest
   layout (not the first 15% match). Needed so FAX480 is not taken as Scottie 2.
4. Manual dropdown still forces a mode.

## Added vs 0.2.78

| Mode | Source |
|------|--------|
| Martin M3 / M4 | Handbook table 4.4, VIS 36 / 32 |
| Scottie S3 / S4 | Handbook table 4.5, VIS 52 / 48 |
| Wraase SC-1 24 / 48 / 48Q / 96 | Handbook table 4.2, VIS 16 / 20 / 24 / 28 |
| FAX480 | QSSTV 512×500 luma, 133.633 s, **no VIS** |
| MP73 / 115 / 140 / 175 | QSSTV `modePD`, 16-bit VIS |
| MR73 / 90 / 115 / 140 | QSSTV `modeRobot2`, 16-bit VIS |
| ML180 / 240 / 280 / 320 | QSSTV `modeRobot2`, 16-bit VIS |

## Honest limits

- **MR175** is not included: QSSTV lists VIS `0x4A23` for both MR140 and MR175.
- **Narrow** MP/MC (2172 Hz) are not included; the demodulator is 1500–2300 Hz.
- Robot 24 remains luma geometry, not a guessed YC matrix.
- AVT still has no line sync; Auto needs VIS.
- Live RF image acceptance still open.

`sdrtown_sstv.exe --selftest-all` encodes and Auto-decodes every mode.

FUBAR website SSTV tab: same dropdown, Receive / Finish / Cancel under Take control
(`POST /v1/sstv/live|finish|cancel`). Home lat/lon stays in SDR Town.
