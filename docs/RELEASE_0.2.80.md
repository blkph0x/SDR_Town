# SDR Town 0.2.80 (experimental)

Analogue SSTV checked against Dayton / handbook / QSSTV, plus a **HamDRM
digital** engine from HB9TLK's published Mode B tables.

## Analogue (not rewritten from the mixed spec table)

Kept Dayton values where the supplied matrix disagrees:

- Scottie 2 scan **88.064 ms**, not 69 ms
- PD Y/R-Y/B-Y/Y2 **equal** scans, not half-chroma
- Wraase SC2-180 **320×256**, not 256×256
- PD290 VIS **94**, not 103

Added: VIS **Hamming-1** if parity fails. Forced analogue prefilter 1–2.4 kHz;
Auto/HamDRM use 300–2800 Hz so OFDM edge carriers survive.

Discriminator remains interpolated **full-period** (not instant zero-cross).
A second-order PLL was tried; it did not lock inside the 10 ms tone tests.

## Digital: HamDRM

Dropdown **HamDRM digital**. File Auto with no analogue image retries HamDRM.

- Mode B, 2.5 kHz, 48 kHz, FFT 1024, guard 1/4, 15 symbols/frame, 51 carriers
- FAC 40 bits (HB9TLK), MSC 4-QAM, convolutional K=7 G0=133₈ G1=171₈
- Payload `STWN` + RGB (not EasyPal's undocumented file RS)

`sdrtown_sstv.exe --selftest-hamdrm` encode/decode MAE 0 on a 32×32 test card.

## Honest limits

- EasyPal interleaved file RS is not implemented (protocol not published; EasyDRF
  states it is incompatible).
- Live RF analogue and live HamDRM on-air still for the other tester.
- FUBAR 1.1.40 already shows Town's mode list; HamDRM appears after this Town
  drop. Pairing DLL is 0.2.80.
