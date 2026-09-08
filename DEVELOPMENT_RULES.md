# SDR Town development rules

These rules are binding. They sit under `SOURCE_OF_TRUTH.md` (product law)
and above day-to-day coding. If a rule here and the SoT conflict, the SoT wins.
If a habit and this file conflict, this file wins.

Companion map: `CAUSE_EFFECT_MAP.md`.
Living trackers: `docs/`.

Method source: Athanor / SovereignFoundry (`DEVELOPMENT_RULES.md`). This tree
is **not** Athanor — Qt, SoapySDR, mbelib, and miniaudio are in-tree product
deps. Copy the *method*, not the sovereignty / zero-library product law.
Desk index: `docs/README.md`. Do not edit the Athanor repo from this project.

---

## 1. Never guess

A guess is any timeout, gate, hop size, mask phase, slot rule, or "should work"
claim that is not backed by one of:

- a sentence in `SOURCE_OF_TRUTH.md` or `CAUSE_EFFECT_MAP.md`
- a cited specification (TIA-102, SDRTrunk/OP25 source we keep under `_codex_refs/`)
- a measurement we took and wrote down (`docs/BUILD_NOTES.md` or `docs/LOG.md`)
- a recorded decision (`docs/DECISIONS.md`) that itself cites evidence

If you do not know:

1. Stop that path.
2. Open an issue in `docs/ISSUES.md`.
3. Write what is unknown, what would make it known, and what must not be invented.
4. Do not commit a placeholder that pretends to be finished.

Forbidden substitutes for knowledge:

- another speaker-grace / TTL / minFresh tweak "to see if it helps"
- copying a constant from memory without a citation next to it
- marking continuous audio done because a `verify_p25_phase2_*.py` string check passed
- `TODO` that hides an unmade decision
- inventing PLC / dummy PCM to paper over missing voice codewords

---

## 2. Everything is documented

Every change that lands updates the trackers in the same commit whenever the
change affects them.

| If you… | You also… |
|---|---|
| Start work | Put it on `docs/TASKS.md` as in-progress |
| Make a choice | Record it in `docs/DECISIONS.md` *before* the code that depends on it |
| Write or change a module | Update `docs/CODE_NOTES.md` |
| Compile, fail, or pass a gate | Append `docs/BUILD_NOTES.md` |
| Hit a defect, ambiguity, or blocker | Open/update `docs/ISSUES.md` |
| Finish a session | Append `docs/LOG.md` with what changed, what was proven, what is still open |

Documentation is part of the patch, not a later courtesy.

---

## 3. Everything is commented

Comments explain **why**, **which evidence**, and **what must remain true**.
They do not narrate `i++`.

Required on magic numbers: a citation (TIA-102, SDRTrunk file, capture id, DEC).

```cpp
/* DEC-0003 / capture 20260830_064319: 40 ms context-free streaming hops
   lose the CQPSK eye. Live streaming slices stay >= 80 ms. */
```

Required on P25 security/slot code: whether this-window evidence or sticky
latch is allowed to open the speaker.

---

## 4. The task list is always current

`docs/TASKS.md` is the canonical work queue.

Rules:

- One current "in progress" implementation task unless a blocker forces a switch.
- Do not start a REQ whose `Depends on` items in the cause/effect map are still open.
- Closing a task requires: code + comments + tests + tracker updates.
- `PASS_PARTIAL_AUDIO` is not done. Continuous means the verification gate.

---

## 5. Issues are first-class

An issue is opened for a failed test or field capture, a spec ambiguity, a
missing measurement, or anything we were tempted to guess. Issues are never
deleted. They are closed with a pointer to the commit, test, or decision.

---

## 6. Build notes are evidence

`docs/BUILD_NOTES.md` records host, compiler, command, pass/fail, and the first
error on fail. A verification gate is not met because a developer said so.

---

## 7. Code notes are the map of the tree

`docs/CODE_NOTES.md` indexes every module that matters for the active REQ.
If you cannot find a P25 file's purpose in that index, the index is wrong.

---

## 8. Tooling and dependencies

Existing product libraries (Qt 6, SoapySDR, mbelib, miniaudio, liquid-dsp,
nlohmann/json, spdlog) stay. Adding a **new** third-party dependency requires
a DEC. Do not pull a vocoder, equalizer, or "P25 SDK" to dodge a gate.

Static `verify_p25_phase2_*.py` scripts may guard invariants. They are **not**
proof of continuous audio. Proof is CLI `p25 voicetest` `PASS_CONTINUOUS_AUDIO`
and/or live CADENCE `dutySec` near 1.0 during talk, with STT/listen.

---

## 9. Git

- Commit messages say *what* and *why*, and name the REQ (`REQ-P2.1`).
- Do not commit captures, keys, or machine-local secrets.
- Tracker files land in the same commit as the code they describe.
- Do not edit `C:\Users\Blkph0x\source\repos\SovereignFoundry` from this project.

---

## 10. Definition of done (any REQ)

A REQ is done only when all are true:

1. Source compiles on the frozen Windows toolchain.
2. Comments and code notes cite the spec or capture actually implemented.
3. The verification gate in `CAUSE_EFFECT_MAP.md` is green.
4. `docs/BUILD_NOTES.md` has a passing run.
5. `docs/TASKS.md` and the SoT checkbox are updated: evidence first, checkbox second.

For Phase 2 voice, the gate includes `PASS_CONTINUOUS_AUDIO` (duty ≥ 0.65 plus
cadence/sequencer/AMBE/concealment checks in `src/main.cpp`) on a real clear
capture — not a string-presence script.

---

## Quick checklist before you type code

- [ ] I can name the REQ.
- [ ] I am not about to invent a timeout to hide missing VCWs.
- [ ] The decision is already in `docs/DECISIONS.md`.
- [ ] I know which tracker files this commit will touch.
- [ ] I know how this will be proven (voicetest / CADENCE / listen), not only compiled.
