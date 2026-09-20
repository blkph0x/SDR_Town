# Work backlog (canonical walk-through list)

Newest maintenance note at the top of LOG. This file is the **ordered queue**
for humans/agents. Update status in the same commit as the work.

Status: `open` | `in_progress` | `blocked` | `done`

## User-authorized receive expansion (2026-09-17; 0.2.74 shipped 2026-09-20)

User deferred further P25 DSP and authorized independent RX features. That does
**not** close REQ-P2.2…P2.6 / ISS-0001. v0.2.74 experimental is GitHub Latest
(tag 4f26f2e): SDRplay Soapy profile, satcom/observer/TLE/Doppler, aircraft map,
Inmarsat prototype, tuner lease, CLI. P25 path unchanged from 0.2.66.

Pairing: FUBAR GitHub Latest is **1.1.33** (0.2.66 DLL). Town Latest is **0.2.74**.
Website satcom/Inmarsat/aircraft/SDRplay tabs live in unreleased FUBAR **1.1.38**.
See `docs/FUBAR_PAIRING.md`.

Still open after 0.2.74 (not invented as done): T-0041 package follow-up,
T-0031 live SSTV RF image, T-0029 string-verifier debt, T-0036 AX.25 backend
qualification, T-0020/T-0019 tone RF acceptance, T-0024 Meteor LRPT. Live RSP
is tester hardware, not a SoT gate.

---

## P0 — Product gate (clear Phase 2 continuous audio)

| ID | Status | Tracker | What | Notes / must not invent |
|---|---|---|---|---|
| B-0001 | open | ISS-0001 / T-0010 / GH#9 | **Live** clear P25 continuous audio re-prove | After DEC-0066: dead unknown grants return ~8s (not ~45s); then seek clear emits (0065 RF-home held). |
| B-0004 | done | T-0006 / REQ-P2.4 | Honest playout duty (drop=D / worker-busy) | DEC-0051 abort + DEC-0052 skip sticky commit (`061217` busy 690). |
| B-0005 | open | T-0007 / REQ-P2.5 | Follow hold during proven call (drop=E only) | |
| B-0006 | open | T-0008 / REQ-P2.6 | Isolation non-regression (encrypted + wrong-slot) | Beside every P2 change. |

**SoT checkboxes still open:** REQ-P2.2 … P2.6 (`SOURCE_OF_TRUTH.md`).

---

## P1 — Spec / evidence gaps

| ID | Status | Tracker | What | Notes |
|---|---|---|---|---|
| B-0010 | open | ISS-0006 / GH#10 | TIA-102 Phase 2 PDF not in-tree | Cite `_codex_refs` until operator supplies excerpts. Never invent bit offsets/LFSR/MAC from memory. |
| B-0011 | open | field | Golden IQ not in git clone | **Keep-set (AppData):** `060036`, `094846`, `095846`, plus latest live follow IQ only. Delete older startstops after documenting. Cap new CLI `record=` ≤15–20 s. |
| B-0012 | open | DEC-0038 | Streaming DDC default-on still rejected | Reopen only if env=1 duty ≥ 0.65 on named capture. |

---

## P2 — Engineering hygiene / diagnosis

| ID | Status | Tracker | What | Notes |
|---|---|---|---|---|
| B-0020 | done | — | **GitHub Actions Windows CI** (Release build + `sdr_town_tests` + string verifiers) | Green on PR #11; workflow `.github/workflows/windows-ci.yml`. |
| B-0021 | open | maintainability | Further shrink `MainWindow.cpp` UI vs remaining timers | Voice path already in `MainWindowP25Voice` / `MainWindowP25Orchestration`. |
| B-0022 | open | maintainability | Delete one-shot extract scripts under `src/tools/_extract_*` / `_migrate_*` when no longer needed | |
| B-0023 | open | ISS-0002 closed process | Keep verifier anchors on **definitions** (`definition_body`) | Never flip SoT from string locks alone. |
| B-0024 | done | ops | Device Apply must keep `rx.active` armed when streaming | Fixed `ecf9303` (Apply/Scan/mode); Add Receiver arms primary too (`fix/p0-dsp-arm-add-receiver`). |

---

## P3 — Roadmap (do not start while P0 open)

From `SOURCE_OF_TRUTH.md` roadmap / TX shells — parked until clear continuous voice gate is green.

| ID | Status | What |
|---|---|---|
| B-0030 | blocked | Multi-device / remote diagnostics polish |
| B-0031 | blocked | TX path beyond lab shells (`P25TxSession` / AMBE encode) |
| B-0032 | blocked | ONNX classifier backend (`SDR_TOWN_ENABLE_ONNX`) |

---

## Recently closed (do not reopen without new evidence)

| Tracker | Closed | One-liner |
|---|---|---|
| ISS-0002 / GH#3 | 2026-09-10 | String verifiers ≠ continuity proof |
| ISS-0003 / GH#4 | 2026-09-10 | Dead SpeakerCatchUp* removed |
| ISS-0004 / T-0009 | 2026-09-10 | `main.cpp` mechanical split (DEC-0040) |
| ISS-0005 | 2026-09-07 | Docs false continuous-done retracted |
| ISS-0007 | 2026-09-07 | Voicetest fixture via AppData capture |
| ISS-0008 / GH#5 | 2026-09-10 | Cadence/tail ownership SoT map |
| ISS-0009 / GH#6 | 2026-09-10 | Verifier definition anchors |
| ISS-0010 / GH#7 | 2026-09-10 | Live pipeline extract from mega-ctor |
| ISS-0011 / GH#8 | 2026-09-10 | Live vs CLI/voicetest ownership map |

---

## Suggested next sessions (in order)

1. Tester RSP/RTL run of **v0.2.74** (Device Manager three-stage SDRplay status;
   do not claim hardware acceptance from this desk).
2. **T-0041** package follow-up (inmarsat JSON in staging; stop leftover
   `SdrTownControl-0.2.71-win64.dll` glob) — new version, do not retag 0.2.74.
3. Remaining decoder acceptance: T-0031 SSTV RF, T-0036 AX.25, T-0020 DCS RF.
4. Historical P25 **B-0001** remains open and deferred until the user reopens it.
