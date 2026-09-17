# Workspace and decoder roadmap

## First deliverable: workspace

View > Workspace selects Listening, Trunking, HF / DX or Analysis. These are
layout presets only: selecting one does not retune, change demodulation or
stop a receiver. Trunking foregrounds P25 Calls; Analysis foregrounds Capture /
Display; HF / DX removes the P25 and receiver-management panels from view.

Drag a panel title to dock or detach it. View toggles individual panels, locks
their positions, saves/restores a layout and resets to the Listening layout.
Normal window closes save geometry, visibility, docking and lock state under
the versioned workspace settings. Automated launches do not overwrite that
layout. Closing a panel hides it; it does not destroy receivers or stop DSP.

Receiver tuning, RF gain, squelch, LPF and volume remain on the central page.
Saved frequencies, receiver management, P25 calls and capture/display controls
are separate panels. The experimental TX panel is hidden by default and can
be opened explicitly through View. Existing transcript/replay/log windows stay
available through Tools; they have not been rewritten into new decoder panels.

## GUI automation

```powershell
build/bin/Release/SDR_Town.exe --gui-dry-run --gui-workspace trunking --gui-window-size 1280x900 --gui-screenshot build/trunking.png --gui-self-test build/trunking.json --gui-exit-after-ms 2200
python scripts/test_workspace_gui.py
ctest --test-dir build -C Release --output-on-failure
```

`--gui-workspace` accepts listening, trunking, hf and analysis.
`--gui-window-size` accepts WIDTHxHEIGHT, from 640x480 through 7680x4320.
`--gui-screenshot` captures this window, not other desktop applications.
The screenshot parent directory must exist. The automated matrix covers four
presets and window sizes; full GUI screenshots still require visual review.

## Next deliverables and gates

DEC-0087 checkpoint: shared registry T-0021 now passes actual live RDS parity
and reception. The previously failing station was impaired at high RF gain;
controlled lower-gain runs confirm metadata recovery without DSP changes.
Historical pending statements below are superseded by this checkpoint.
T-0026 separately tracks intermittent shutdown faults before further expansion.
DEC-0089 closes the reproduced fault: stale RTL runtime replaced by the
configured dependency, native and CDB GUI lifecycle gates pass. SSTV can now
proceed as the next decoder milestone; other hardware remains unqualified.

SSTV and public satellite/weather reception are explicitly in scope under
DEC-0083. See [Satellite and SSTV plan](SATELLITE_AND_SSTV.md) for phased targets,
Images/Satellites workspaces, Doppler/pass scheduling, backend candidates,
hardware requirements and validation gates. These are planned, not implemented.
DEC-0084 adds experimental DCS identification with tested CLI/GUI routing;
independent known-code RF acceptance remains open. Immediate coding order:
shared registry, SSTV, AX.25/APRS and weather links.
DEC-0085 implements the registry and RDS-first shared session with recorded
parity and automated CLI/GUI checks. Live RDS acceptance currently fails and
remains open; see BUILD_NOTES/ISSUES before starting dependent SSTV work.
RECEIVE_DECODERS.md describes implemented domains and ownership. IQ/audio/image
contracts remain future work, not capabilities advertised by this registry.
The remaining digital voice/data roadmap below stays queued.

2026-09-17 checkpoint: user defers further P25 optimisation. RDS protocol
foundation and pre-filter WFM MPX tap implemented under DEC-0078; see RDS.md.
DEC-0080 adds live WFM RDS metadata, validated on 98.1 MHz; the registry remains next. The
existing P25 acceptance gaps stay recorded, not treated as closed by this move.

1. Decoder interface and registry, first exercised by RDS. Explicit IQ,
   discriminator, FM multiplex and audio input contracts, timestamps, bounded
   queues and discontinuity/reset semantics. Do not force digital data through
   speech LPF, squelch or de-emphasis. Existing P25 stays behind an adapter until
   its output equivalence is established.
2. RDS: station PI, PS, radiotext and program type from validated groups;
   fixed-capture expectations and no WFM audio regression. Review redsea's
   implementation and licensing before choosing integration versus a native
   implementation. No decoder is advertised as available until it works.
3. Analog/HF: measured AGC/filter and weak/strong-signal improvements; CW pitch,
   passband shift, synchronous AM and NFM tone decoding as separate tested tasks.
4. DMR conventional: Tier II/direct mode, color code, slot/TG/RID, clear voice;
   independent protocol framing, per-slot state and reference captures. DMR
   trunking is a subsequent milestone, not part of initial conventional support.
5. Scanner product: priority, hold/skip, recording rules, explicit device and
   instantaneous-bandwidth allocation. Then NXDN and other digital voice.
6. Data modules: AX.25/APRS, AIS, ADS-B and ACARS with validated packet fixtures.
   DAB+/DRM and HF data integrations follow hardware/codec/license evaluation.

P25 audio acceptance remains tracked independently. New modules require
reference data, malformed-input tests, live/replay parity, CPU/memory soak
tests and GUI automation. STT plausibility alone is never the acceptance gate.
