# SDR Town 0.2.86

## Satcom is now part of the main SDR Town receiver session

- **START / TAKE OVER**, **Arm pass**, automatic in-range capture, and ISS SSTV use the application-owned receiver session instead of behaving like a separate app.
- When Satcom uses the active Listen SDR, ordinary Listen processing is parked and restored after Stop, LOS, startup failure, lost hardware, or application shutdown.
- Users with a second SDR can select it in Satcom and keep the main Listen receiver running on the first device.
- A P25-owned receiver is never interrupted; Satcom reports the ownership conflict and requires another SDR or a stopped P25 session.

## Audio, spectrum, and SSTV

- Satellite audio now uses SDR Town's configured playback output instead of opening a competing default audio device.
- The main spectrum and waterfall follow Satcom while it owns the active Listen receiver, while the Satcom spectrum remains available for a separately selected SDR.
- Shared audio queue handling no longer trims PCM belonging to another receiver.
- SSTV source selection explicitly supports NFM, USB, and LSB. ISS/ARISS 145.800 MHz SSTV remains NFM; HF SSTV uses one selected sideband rather than a USB/LSB mix.

## Safety and validation

- No protected P25 control, follow, traffic, Phase 1/2, vocoder, or P25 audio pipeline file was modified.
- Workflow YAML validation, frozen-P25 checks, non-P25 smoke tests, SDRplay preflight syntax, MSVC Release compilation, core tests, Qt/live-decoder tests, SSTV backend tests, clean deployment, ZIP creation, and SHA-256 generation are required before publication.
- Rollback branches:
  - `backup/pre-satcom-mainwindow-integration-20260923`
  - `backup/pre-v0.2.86-release-20260923`

Hardware reception still depends on the selected SDR, antenna, installed drivers, signal strength, and the satellite actually transmitting the expected mode.
