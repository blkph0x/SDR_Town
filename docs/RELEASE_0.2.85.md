# SDR Town 0.2.85

## Receiver takeover recovery

- Satcom **START / TAKE OVER**, **Arm pass**, and **Arm ISS SSTV** now reuse an already-live hardware receiver instead of stopping it and falling back through DeviceManager's temporary stub/open path.
- Manual Satcom operation can take control from an ordinary Listen session and restores the previous receiver frequency and state on Stop or pass end.
- The Satcom receiver selector remains available for users with a second SDR. P25 ownership is never interrupted.
- Inmarsat now has a stable receiver selector, **START / TAKE OVER**, live-hardware/IQ validation, applied-tune confirmation, failure rollback, and previous receiver restoration.

## SSTV demodulation

- ISS/ARISS SSTV remains NFM, which is the correct receive demodulation for its 145.800 MHz downlink.
- SSTV is not a simultaneous USB/LSB mix. HF SSTV uses one selected sideband; explicit USB and LSB sources are now accepted and preserved by the live SSTV input path.
- The packaged SSTV helper and licence notices remain mandatory release contents.

## Safety and validation

- No protected P25 receive, control, follow, traffic, Phase 1/2, vocoder, or P25 audio pipeline file was changed.
- Frozen-P25 guard, MSVC Release build, core tests, Qt/live-decoder tests, SSTV backend tests, release verifier, clean portable staging, ZIP creation, and checksum generation are required before publication.
- Rollback branch: `backup/pre-satcom-inmarsat-recovery-20260922`.
