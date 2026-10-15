# SDR Town tracking desk

Current review: [system audit and operator-workflow plan](AUDIT_20261015_SYSTEM_AND_UX.md)
against Town a0d4e21 / FUBAR308a653. Fresh builds,26 registered suites, native/web
inspection and isolated negative probes distinguish confirmed faults from test
gaps and UI proposals. Production/P25 code unchanged; T-0132/0133 remain first.
Current paired public releases are Town0.2.138/FUBAR1.1.50; signed installer,
portable/DLL and paired public verification are in BUILD_NOTES.
The [previous reconciliation](AUDIT_20261011_IMPLEMENTATION_REPORTS.md) remains
historical evidence, not a reason to repeat completed release requirements.
Historical [package hardening](PACKAGE_HARDENING_20261003.md) records the former
source/notice blockers; those are not a current blanket publication block.

Current repair ledger: [shared infrastructure hardening](INFRASTRUCTURE_HARDENING_20261003.md)
tracks reproduced defects, local gates, diagnostic contracts and still-open work.

Project licensing: [MIT grant](../LICENSE.txt),
[scope, reference distinction and distribution gate](../LICENSING.md), and
[acknowledgements](../ACKNOWLEDGEMENTS.md).

P25 regression diagnosis: [2026-09-27 missed-grant / stale-allocation audit](P25_REGRESSION_AUDIT_20260927.md).
Fresh live GUI capture, paired same-IQ tests and the next repair plan. No decoder
changes in this diagnostic pass; ISS-0042..0044 remain open.

DSP proposal review: [2026-09-26 evidence audit](DSP_AUDIT_20260926.md).
Confirmed FM block-boundary issues, proposal corrections and non-regression gates;
documentation only, accepted receiver paths unchanged.

In-tree trackers are canonical. GitHub issues/PRs are a public mirror.

Current baseline review: [2026-09-24 audit and repair plan](AUDIT_20260924.md).
It reconciles the original source folder to 0.2.88 and records measured HF,
orbit-prediction and pending-PR regressions, plus receiver integration gaps.
Tester repair release: [0.2.89 notes and remaining gates](RELEASE_0.2.89.md).

Method: Athanor process (`DEVELOPMENT_RULES.md`) on this product tree.
Do not edit `C:\Users\Blkph0x\source\repos\SovereignFoundry` from here.

| File | Job |
|---|---|
| [BACKLOG.md](BACKLOG.md) | Ordered walk-through queue (P0–P3). Start here. |
| [TASKS.md](TASKS.md) | Living task list. Always current. |
| [ISSUES.md](ISSUES.md) | Defects, ambiguities, blockers. Never deleted. |
| [DECISIONS.md](DECISIONS.md) | Choices made with evidence, **before** code depends on them. |
| [CODE_NOTES.md](CODE_NOTES.md) | Map of the source tree. |
| [BUILD_NOTES.md](BUILD_NOTES.md) | Compiler, flags, pass/fail. Evidence for SoT gates. |
| [LOG.md](LOG.md) | Chronological session log. |
| [SPEC_INDEX.md](SPEC_INDEX.md) | Specs and cited implementations; the anti-guess list. |
| [RECEIVE_DECODERS.md](RECEIVE_DECODERS.md) | Shared receive contracts, compiled capabilities and RDS adoption. |
| [NATIVE_RUNTIME_QA.md](NATIVE_RUNTIME_QA.md) | Native DLL provenance, RX lifecycle and CDB shutdown acceptance. |
| [SATELLITE_AND_SSTV.md](SATELLITE_AND_SSTV.md) | Planned SSTV/public satellite/weather scope, architecture and acceptance gates. |
| [SSTV.md](SSTV.md) | Recorded and experimental live NFM SSTV usage, tests and limits. |
| [SDRPLAY.md](SDRPLAY.md) | SDRplay RSP multi-model support via SoapySDRPlay3 (gains, AGC, duo, HDR). |
| [P25_ALIASES.md](P25_ALIASES.md) | System-specific names, import format and manual precedence. |
| [FUBAR_PAIRING.md](FUBAR_PAIRING.md) | Companion FUBAR versions, `SdrTownControl.dll`, and honest gaps. |
| [INMARSAT.md](INMARSAT.md) | Inmarsat prototype: what ships vs remaining gaps. |

Architecture: [`../SOURCE_OF_TRUTH.md`](../SOURCE_OF_TRUTH.md).
Why/how/effect: [`../CAUSE_EFFECT_MAP.md`](../CAUSE_EFFECT_MAP.md).
Rules: [`../DEVELOPMENT_RULES.md`](../DEVELOPMENT_RULES.md).

## Historical P25 notes (evidence, not the desk)

These stay as capture archaeology. New work is ISSUES + DECISIONS + TASKS.

- `P25_BASELINE_CLEAR_CONTINUOUS_20260810.md`
- `p25_phase2_regression_tracker.md`
- `P25_FULL_AUDIT_2026_07.md`
- `P25_SDRTRUNK_FULL_COMPARISON.md`
- `p25_phase2_release_gate.md`
- `src/*PATCH_NOTES.md` / `src/*HOTFIX_NOTES.md`
# Release operations

See [RELEASING.md](RELEASING.md) for current packaging, signature, runtime and
publication gates, and [SATELLITE_AND_SSTV.md](SATELLITE_AND_SSTV.md) for next work.
Historical RTL update notes: [0.2.97](RELEASE_0.2.97.md), [0.2.96](RELEASE_0.2.96.md).
Current pairing: [FUBAR_PAIRING.md](FUBAR_PAIRING.md), DEC-0219 and BUILD_NOTES.
SSTV: [analogue families, HamDRM, live NFM, tests and limits](SSTV.md). Inmarsat: [INMARSAT.md](INMARSAT.md).
