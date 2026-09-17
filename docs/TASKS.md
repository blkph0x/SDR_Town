# Task list (canonical)

2026-09-17 release task: v0.2.55 from DEC-0069 through DEC-0074 rebuilt and
tested (235 cases); clean packages and signed updater verified. Source/assets
ready for publication. Experimental channel retained; audio acceptance stays open.

Update this file in the same commit as the work. SoT checkboxes move only
after the cause/effect verification gate is green.

Status: `open` | `in_progress` | `blocked` | `done`

---

## Now

T-0010 downstream pass: reproduce/exclude exceptional audio read-cursor races
(DEC-0074), then inspect callback losses separately from decoder feed gaps.

T-0010 current pass: full-frame provenance collected (DEC-0071); physical
mapping and missing block-tail defects fixed/tested (DEC-0072/73). Live
underruns and remaining valid-frame loss remain in progress. Evidence and
non-completion caveats: `P25_MAPPING_AUDIT_20260917.md`.

2026-09-17 follow-up (T-0010 remains in progress): slot-state mismatch reproduced
and corrected with clear/encrypted companion-slot tests (DEC-0069). Reference
103841 retains four additional frames; 060515 still has six feed gaps. GUI EOF
tail priming also reproduced and fixed (DEC-0070). Remaining live continuity
and concealment are not closed by these scoped repairs.

2026-09-17: T-0010 remains in progress. Capture 060515 isolates excessive RS
recovery work; exact GF64 tables and cached syndrome columns reduce replay
wall time without changing the 103841 reference WAV. Six feed gaps remain
on 060515. Version 0.2.54 is an experimental measurement build, not completion
of REQ-P2.2-6. Evidence: `P25_AUDIO_FORENSICS_20260917.md`.

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0013 | in_progress | CI / B-0020 | GitHub Actions Windows Release build + `sdr_town_tests` + Phase 2 string verifiers |
| T-0010 | in_progress | ISS-0001 | DEC-0038: stream env=1 still duty 0.23 on 060036 (block 0.705). Default-on off. Live path = block + DEC-0035/0037; re-prove ~095846. |

---

## Backlog (do not start early)

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0004 | open | REQ-P2.2 | Mixed MAC-dead first hop still drops target=6 opp=28 (105622 hop 1). Post-emit mixed skip is DEC-0012. Do not reopen dual-slot 034136. |
| T-0005 | open | REQ-P2.3 | Emit proven PCM (only if drop=C) |
| T-0006 | open | REQ-P2.4 | Playout duty after extract rate is honest (drop=D residual) |
| T-0007 | open | REQ-P2.5 | Follow hold (only if drop=E) |
| T-0008 | open | REQ-P2.6 | Isolation non-regression beside every P2 change |

---

## Done

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0009 | done | ISS-0004 | DEC-0040: split orchestration out of mega-`main.cpp` into focused TUs; Release build + 214 tests + 129 string verifiers green |
| T-0012 | done | ISS-0001 | DEC-0015 tuner LO = SDRTrunk CenterFrequencyCalculator. Units: voice−11249; 095450 pair → 420.21375 MHz. File voicetest 105622 duty=0.685 / 073304 duty=0.76 unchanged (capture `--center`). |
| T-0001 | done | REQ-0.1 | Athanor-method desk: SoT, cause/effect, trackers; retract false continuous-done |
| T-0002 | done | REQ-P2.0 | Classified AppData captures + HEAD voicetest (ISS-0001) |
| T-0003 | done | REQ-P2.1 | 105622 TG 30003 slot 0 skip=97334 8 s: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735` after DEC-0008/0009; DEC-0013 re-prove duty=0.685. Slot 1 same IQ duty=0.11. |
| T-0011 | done | ISS-0001 | 073304 TG 10330 slot 1 skip=107597 8 s (live repeats): `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk` after DEC-0013 lattice de-dupe. DEC-0010/0011 lock-only starved 105622 and were superseded. |
