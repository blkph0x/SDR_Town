# SDR Town 0.2.56 (experimental)

## Included

- Detachable/tabbed Listening, Trunking, HF/DX and Analysis workspaces.
- Region/country/location band-plan selection, partial built-in AU/GB/US plans,
  validated imports, waterfall band labels and drag tuning.
- Automatic WFM RDS station display, validated PI/PS/radiotext, recorded MPX and
  bitstream CLI decoding using a pinned Redsea/liquid-dsp backend.
- Experimental CTCSS/DCS identification and CLI tools; no tone-based audio gate.
- Shared receive decoder registry, explicit stream/gap contracts and same-input
  live RDS parity diagnostics. CLI `decoders` lists actual adapter capabilities.
- Prior P25 PCM resampling regression work and tests are included; this release
  does not certify universal clear Phase 2 audio or relax security/slot gates.
- Configured RTL-SDR runtime deployment fixes a locally reproduced native
  shutdown access violation caused by a stale DLL. Debugger/lifecycle QA tools
  and dependency notices are included in source.
- Release commands stop on failures, sign with the existing trusted key, verify
  package contents/hashes, and publish the actual reviewed branch.

## Verified

279 C++/Qt cases (189174 assertions), RDS/CTCSS/DCS/registry CLI tests, four GUI
layout sizes, 11 package verifier tests, command-failure/signing-key rejection
tests and diagnostic regression scripts pass. The rebuilt GUI received 262
RDS groups from i98FM at 98.1 MHz under CDB with zero adapter/native differences
and no access violation. Extracted portable RDS/registry and four GUI layout
checks pass. Installer/ZIP/control DLL hashes and manifest signature verified.
Prior runtime acceptance included 20 configured/deployed native RX cycles and
five GUI shutdown cycles under CDB. This is one host's RTL hardware evidence,
not all-device certification or an installer-upgrade test on a clean machine.

## Limits and next work

P25 remains experimental with live continuity acceptance open. Known-tone/code
RF qualification awaits the user's radio. RDS character mapping is partial.
SSTV and public/weather satellite decoders are planned, not shipped: next gate
is independently validated recorded SSTV reception, then live image workflow.
DMR/NXDN/DRM decoding and an ONNX classifier are not advertised as implemented.

## Downloads

Use the installer or portable ZIP. The versioned SdrTownControl DLL is also
provided separately for local application integrations. update.json and its
Ed25519 signature support the existing consent-based updater; SHA256SUMS.txt
covers the three binary assets. This is the experimental channel, published
as GitHub Latest so existing testers can discover the update. Binaries are
not Authenticode-signed. No private signing key or local capture is packaged.
