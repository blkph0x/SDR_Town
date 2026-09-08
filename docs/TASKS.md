# Task list (canonical)

Update this file in the same commit as the work. SoT checkboxes move only
after the cause/effect verification gate is green.

Status: `open` | `in_progress` | `blocked` | `done`

---

## Now

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0010 | in_progress | ISS-0001 | DEC-0012 companion-louder mixed skip (041716 duty=0.87). 105622 file duty=0.645. Live drop **D** still 4.5× overlap CQPSK. |

---

## Backlog (do not start early)

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0004 | open | REQ-P2.2 | Mixed MAC-dead first hop still drops target=6 opp=28 (105622 hop 1). Post-emit mixed skip is DEC-0012. Do not reopen dual-slot 034136. |
| T-0005 | open | REQ-P2.3 | Emit proven PCM (only if drop=C) |
| T-0006 | open | REQ-P2.4 | Playout duty after extract rate is honest (drop=D residual) |
| T-0007 | open | REQ-P2.5 | Follow hold (only if drop=E) |
| T-0008 | open | REQ-P2.6 | Isolation non-regression beside every P2 change |
| T-0009 | open | ISS-0004 | Split P25 orchestration out of `main.cpp` (after voice gate is green) |

---

## Done

| ID | Status | REQ | Task |
|---|---|---|---|
| T-0012 | done | ISS-0001 | DEC-0015 tuner LO = SDRTrunk CenterFrequencyCalculator. Units: voice−11249; 095450 pair → 420.21375 MHz. File voicetest 105622 duty=0.685 / 073304 duty=0.76 unchanged (capture `--center`). |
| T-0001 | done | REQ-0.1 | Athanor-method desk: SoT, cause/effect, trackers; retract false continuous-done |
| T-0002 | done | REQ-P2.0 | Classified AppData captures + HEAD voicetest (ISS-0001) |
| T-0003 | done | REQ-P2.1 | 105622 TG 30003 slot 0 skip=97334 8 s: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735` after DEC-0008/0009; DEC-0013 re-prove duty=0.685. Slot 1 same IQ duty=0.11. |
| T-0011 | done | ISS-0001 | 073304 TG 10330 slot 1 skip=107597 8 s (live repeats): `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk` after DEC-0013 lattice de-dupe. DEC-0010/0011 lock-only starved 105622 and were superseded. |
