# SDR Town 0.2.137 / FUBAR 1.1.49

Experimental receive release, with installer and portable builds from the same
qualified Actions stage. Latest promotion requires a separately verified and
signed updater manifest; the signing key is not distributed to CI.
Published and verified: Town Actions 38110086896, companion Actions 38114493140.
Both releases are Latest; Town's updater channel remains experimental.
FUBAR 1.1.48 was never published: clean-runner tests caught a COM cleanup crash.
1.1.49 fixes that crash, cold-workspace timeouts and large-response truncation.

## Companion workflows

- Native per-session satellite status, catalogue, pass controls and completed
  images. FUBAR uses the chosen downlink, not a hardcoded ISS frequency.
- Named Inmarsat messages and maps follow the addressed receiver. Web controls
  select radios, channels and multi-channel watch lists; aircraft controls
  address their own receiver and local/network sources.
- SSTV publishes only live-capable modes and per-session images. Web reception
  uses the host's persistent image directory. FUBAR exposes opaque IDs only
  for images reported by Town, not arbitrary local filenames.
- Remote actions retain the operator lease and cannot force another workflow
  off its radio. Home coordinates, local files, diagnostics consent, rotor
  movement and transmit arming remain host-admin operations.

## Receive changes already accepted locally

- The pending P25 quiet-symbol gain/soft-score corrections and strict slot-bound
  security evidence are included. User listening acceptance is positive. No new
  P25 DSP changes were made for companion alignment; CC coverage and short-tail
  investigations remain open, not hidden by this release.
- Satellite SSTV sourced/custom profiles, digital Doppler, stale-TLE/observer
  checks, no-takeover scheduling, persistent image folders and epoch recovery.
- SSTV live Auto leader fade and extended VIS repairs; 49 analog modes tested
  through simulated RF. FUNcube telemetry is explicitly not analog SSTV.

## Verification and limits

Native session/isolation tests, production web-JS tests and a real authenticated
FUBAR -> SdrTownControl.dll -> Town GUI test cover routing and refusal cases.
Full CI and public download evidence are recorded in BUILD_NOTES after completion.
Synthetic/paced SSTV tests do not replace an actual satellite-pass test.
Independent per-session network audio selection is not implemented: FUBAR
continues to stream its configured Windows capture input.
