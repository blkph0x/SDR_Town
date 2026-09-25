# SDR Town 0.2.98 - Experimental RSPdx / Inmarsat stability

CI-built portable tester release. Extract the ZIP into its own directory and
run `SDR_Town.exe`. This prerelease does not replace the last signed installer
or its in-app update metadata.

## What changed

- SDR Town now discovers compatible SoapySDRPlay3 modules installed under the
  standard Afreet SkyRoof `lib\SoapySDR\modules0.8` layout.
- Existing safeguards remain: the official API must expose API 3.x symbols,
  the Soapy module must match the app ABI, and it must register an SDRplay
  factory before hardware enumeration is accepted.
- The read-only preflight now finds SkyRoof and environment-provided plugin
  directories and does not reject a normal multi-architecture API installation
  when a matching x64 DLL is present.
- The RSPdx fixture can provide active IQ for five repeated Inmarsat
  start/stop/restore cycles, covering leases, worker shutdown and stream reuse.
- No Aero modem/FEC/codec, P25, RF tuning, audio, Bias-T or driver teardown
  behavior changed.

## Evidence and tester limits

On the reporting PC, release 0.2.96 initially missed the installed module. With
that exact SkyRoof module supplied to the existing loader, SDR Town registered
module 0.5.2-8ef31b2, enumerated the attached RSPdx, opened live hardware,
tuned to 1542.935 MHz, and completed five Inmarsat start/status/stop cycles
without a crash. Bias-T was not enabled.

The focused local suite passes 179 SDRplay assertions across 19 cases. The
real-handle RSPdx/Inmarsat lifecycle passes 52 assertions. Clean MSVC build,
full tests, packaging and public downloaded-asset smoke checks are performed by
the release workflow. This testing establishes discovery and lifecycle
stability only; it does not prove satellite lock, message decode or clear voice.

If the RSP is still absent, close other SDR applications, verify the SDRplay
API service is running, use `devices rescan` and `sdrplay status`, then report
the model, module/API versions and log without credentials.
