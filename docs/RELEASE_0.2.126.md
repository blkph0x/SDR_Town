# SDR Town 0.2.126 experimental

## P25 emit-gap repair (DEC-0199)

- Spectrum FFT and power publication no longer run on the Soapy `readStream`
  loop. A joined per-stream worker reads the IQ ring so live P25 drain is not
  blocked by radix-2 work (capture bucket A: `20261006_062201_289` overflows).
- Active-clear speaker catch-up geometry is 360 ms max / 160 ms minimum /
  280 ms overlap. The previous 280 ms minimum forced long submissions after the
  360 ms catch-up change and left residual CADENCE drop D.
- Encryption, wrong-slot, normal sustain, and non-active backlog geometry are
  unchanged. No PLC or invented silence.

## Verification

Local Release build and full 17/17 CTest PASS. Planner and hardware-loss
spectrum lifecycle tests PASS. Portable experimental release; live multi-minute
2.4 Msps field re-prove remains the final continuity acceptance for overflow-free
operation on real RTL hardware.
