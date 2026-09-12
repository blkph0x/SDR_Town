# Work backlog (canonical walk-through list)

Newest maintenance note at the top of LOG. This file is the **ordered queue**
for humans/agents. Update status in the same commit as the work.

Status: `open` | `in_progress` | `blocked` | `done`

---

## P0 — Product gate (clear Phase 2 continuous audio)

| ID | Status | Tracker | What | Notes / must not invent |
|---|---|---|---|---|
| B-0001 | open | ISS-0001 / T-0010 / GH#9 | **Live** clear P25 continuous audio re-prove | DEC-0050: start/stop → `*_live_speaker.wav` + `run_p25_listen_bar_harvester.py` (CLEAR vs GARBLED vs SILENT). Reset PPM≈−2. |
| B-0004 | done | T-0006 / REQ-P2.4 | Honest playout duty (drop=D / worker-busy) | DEC-0051 cooperative abort on `044651` emit p50≈212 / busy 135. Live re-prove still B-0001. |
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

1. **B-0001** live re-prove on HEAD after Apply-arm fix (`ecf9303`) — capture + CADENCE / voicetest.
2. Only then touch **B-0002…B-0006** with named A–E evidence.
3. Spec gap **B-0010** when operator has TIA excerpts.
