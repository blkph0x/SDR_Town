# 0.2.112 - Inmarsat diagnostic recordings (experimental)

Open Inmarsat > Diagnostic recording. Enter an already monitored channel,
explicitly agree to recording, then Record 5 seconds. Keep that channel running.
When ready, save locally or Review and send. Remote diagnostics must be enabled
in the app menu before sending. Recording consent is separate, not remembered,
and never implied by ordinary telemetry consent. Closing discards the memory copy.

The bundle contains exact 48 kHz signed-16 little-endian real modem IF (8 kHz IF),
up to five seconds of emitted 8 kHz decoded PCM, rate/mode/frequency, UTC time,
before/after decoder counters, and on upload installation ID/app version.
It ALSO contains up to 16384 original complex float IQ samples with exact rate,
center and starting sample index (about 6.83 ms at 2.4 MS/s). The send dialog
shows the actual IQ duration. This excerpt is not a full five-second RF capture.
IQ/IF may contain speech, aircraft identities and positions;
only share signals you are authorized to record and disclose. No automatic
upload, background retry or permanently running recorder. A retune/gap/decoder
stop cancels; an incomplete capture expires after 90 seconds.

Payload is below 1 MiB, four accepted uploads per installation per rolling day.
Collector storage caps at 128 MiB and expires recordings after 30 days (checked
each minute while running). Full storage returns an error rather than deleting
newer evidence. Shared restricted credentials are not proof of authenticity.
HTTPS is mandatory; redirects are refused; receipt ID confirms server acceptance.
Ordinary telemetry remains counters-only. Saved local copies are user-managed.

Reproduce the modem portion with:

```text
SDR_Town.exe --cli --allow-multiple --no-remote-diagnostics --inmarsat-iq test.inmarsat.json --inmarsat-format diagnostic_if --inmarsat-wav new-output.wav --inmarsat-result result.json
```

This starts the modem cold, not from its original internal PLL/FEC/vocoder state.
The final IF block is clipped at five seconds; PCM contains bounded emissions
from processed blocks, not a sample-aligned speaker timeline. Missing PCM is
valid evidence, not a reason to synthesize silence. This cannot diagnose upstream
wideband filtering or reproduce warmed decoder state exactly. Keep original IQ
for those cases. Exit zero means reproduction completed, not clear voice proven.

Collector administrators: LAN/local authenticated `/api/recordings` lists receipt IDs;
`/api/recordings?id=ID` retrieves a bundle with the separate admin credential.
Files are stored under `recordings` beside the collector database. No public
recording download endpoint exists. Deploy the matching collector before testing.
`scripts/unpack_inmarsat_recording.py recording.inmarsat.json new-folder` exports
the original IQ as SigMF and separate modem/decoded-audio WAVs for inspection.

No P25/NFM/WFM signal processing changes. Portable experimental release, not a
replacement for the signed-installer updater channel. Live RF still needs testing.
