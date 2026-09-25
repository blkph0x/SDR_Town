# SDRplay and release comparison, 26 September 2026

## Confirmed findings

- InmarScope 1.0.19-sdrplay.9 ships a rebuilt SoapySDRPlay3 plugin at
  48bd8b41072534018de1d74deb3dea5874d9e0e0, compiled against API 3.15 and
  its packaged Soapy runtime. Source: local InmarScope tools/ci/windows-sdk.ps1
  and shipped SDRPLAY.md. It still requires the official vendor API/service.
- SDR Town 0.2.97's public portable has no sdrPlaySupport.dll. Its documented
  setup requires a separately installed compatible plugin as well as the API.
  Consequently an API-only tester installation does not match InmarScope's
  prerequisites. This is a confirmed packaging gap, not proof of the exact
  failure on the remote RSPdx machine.
- This PC's latest runtime logs select Pothos module 0.3.0-206b241 and report
  the SDRplay service not installed. Only an RTL is enumerated. That cannot
  establish the remote tester's service state or hardware behaviour.
- GitHub's Latest is v0.2.96; the newer portable prerelease is
  v0.2.97-experimental. The latter's downloaded executable hash matches
  f6f46ee305f19c50b18ef312f7f4628b75ad178344caaf5266308d3cc7bba487 and its
  provenance identifies bf83d97. That source builds RtlBiasTWidget and adds
  it to Device Manager; it is visible for a selected rtlsdr row. This does
  not reproduce or dismiss the tester's missing-control report.

## P25 limits of evidence

Comparison v0.2.96 to bf83d97 shows no differences in src/P25*, Receiver*,
Demod* or AudioEngine*. DeviceManager did change for RTL bias-T, and CI
libraries/toolchains differ from the developer build, so this is not proof
of equivalent live behaviour. Opening an RTL now explicitly applies saved
bias-T intent, default OFF; verify the intended setting for an active antenna,
without automatically enabling power on unknown hardware.

The usual IQ capture folder's newest capture is 17 September; validation
JSONL was last modified 24 September. Latest 25/26 September application
logs do not supply the reported failing P25 session. No timeout, security,
decoder or audio changes are justified by those records alone.

## Next gates

1. Build and package a pinned SoapySDRPlay3 plugin against SDR Town's own
   Soapy runtime, include its licence/provenance, and validate actual factory
   registration from the downloaded release. Do not copy an arbitrary DLL
   or redistribute the proprietary API/service installer.
2. Obtain remote RSPdx sdrplay status, ZIP name/version and startup log;
   verify official API/service, enumeration, open and stream independently.
3. Reproduce missing bias-T in the exact downloaded executable and selected
   device row before changing visibility logic.
4. Obtain the current failing P25 capture/session log and exact executable
   identity; compare grant decisions, retune completion, call-end reasons
   and PCM output on identical input before any DSP changes.

No functional code change or claimed hardware/audio repair in this audit.
