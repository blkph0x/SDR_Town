================================================================================
SDR TOWN — CAUSE / EFFECT EXECUTION MAP
================================================================================
VERSION: 1.0.0  |  COMPANION TO: SOURCE_OF_TRUTH.md
STATUS RULE: Flip [ ] to [X] only after the verification gate for that REQ is met.
DEC-0100 checkpoint: system-scoped presentation aliases pass schema/isolation,
manual-override, storage conflict and actual GUI workflow tests. No grant or
audio-path mutation; directory sourcing and external formats remain separate.
DEC-0099 checkpoint: live NFM receiver attachment and GUI session are wired;
recording-driven worker/GUI parity and lifecycle gates pass. Known-image live
RF acceptance remains open; do not mark full SSTV coverage complete.
DEC-0094 checkpoint: progressive recorded previews validated against final RGB;
full/partial independent recordings match direct decode. Row protocol/resource
and latest-only UI handoff tests pass. Live RF remains open.
DEC-0093 checkpoint: recorded SSTV GUI/direct pixels match on independent
Robot36/Martin1 inputs. Single-job ownership, responsiveness, cancellation and
teardown tested; preview reviewed at two sizes. Live/progressive SSTV still open.
DEC-0092 checkpoint: offline Robot36/Martin1 image decode verified independently,
actual CLI/helper pixel parity and input resource gates pass. Full SSTV remains
open for GUI/live integration and additional independently qualified modes.
THIS FILE DOES NOT REPLACE THE SoT. It explains why each REQ exists, how to
finish it, what it causes, and what breaks if it is skipped.
================================================================================

DEC-0084 checkpoint: experimental NFM DCS uses the independent raw FM tap.
Gate passed: 105 payloads/polarities/rotations, independent waveform/bit CLI
fixtures, 274 core/Qt cases, GUI layouts and live stream-routing checks.
Known-code RF sensitivity remains OPEN; DCS does not gate audio. P25 untouched.

DEC-0085 checkpoint: shared RDS/CTCSS/DCS receive contracts implemented.
DEC-0089 checkpoint: reproduced native RTL teardown fault corrected through
deterministic runtime deployment. Twenty native lifecycle cycles, five CDB GUI
cycles, live RDS parity/reception and full suite pass; T-0026 closed locally.
DEC-0087 update: live station gate now passes with controlled lower RF gain;
native/adapter equality and correct PI/PS/RT proven, T-0021 complete. Earlier
open statement below is historical. Intermittent shutdown T-0026 remains open.
Recorded native/adapter parity, 279 core/Qt cases, CLI and GUI layout gates pass.
Live RDS acceptance OPEN: two 45-second 98.1 MHz tests failed PI/PS acquisition.
See BUILD_NOTES/ISSUES. No RF cause assumed, no P25 processing change, and no
dependent decoder milestone marked complete on recorded parity alone.

REQ-BP.1 (2026-09-17 user-authorized receive profile infrastructure)
STATUS: [X] (BUILD_NOTES: 244 tests, four GUI checks, identical CLI/GUI replay)
CAUSE: Mixed-country priors and no visible selected location/service context.
HOW: Immutable selected profiles, validated imports, specific/ambiguous lookup,
     existing AUTO priors and waterfall labels; manual/P25 trust unchanged.
GATE: Boundary/import/concurrency tests, GUI selection/cancel/persistence,
      actual GUI startup profiles/screenshots and unchanged P25 reference WAVs.
EFFECT: Explicit, extensible country/location context; partial data labelled.
REQ-BP.2 remains OPEN: complete sourced country/HF/local coverage and routing
     to implemented, validated decoders. Infrastructure is not world coverage.

DEC-0080 checkpoint: visible band sections use the same clipped frequency axis
and priority/ambiguity lookup; drag previews commit one retune on release.
WFM RDS now runs automatically from chronological pre-audio MPX with source-gap
resets and stale-aware GUI metadata. Gate: 257 native/Qt cases, four GUI layouts,
actual waterfall drag and 45-second live RTL run with 402 groups / i98FM PI+PS+RT.
General decoder registry and broader RF/character-set coverage remain open.

REQ-UI.1 (2026-09-17 user-authorized independent presentation milestone)
STATUS: [X] (2026-09-17 - BUILD_NOTES, 238 tests, GUI QA and replay equivalence)
CAUSE: One stacked window cannot accommodate additional decoder panels.
HOW: Reuse existing widgets in named dock panels; save/restore/reset layouts,
     four presets; keep live processing independent of panel visibility.
GATE: Interaction/persistence tests, small/large GUI screenshots, existing unit
      suite, byte-identical reference P25 replay. No dependency on closing
      REQ-P2 audio acceptance, because decoder/timing paths do not change.
EFFECT: Space for real decoder modules without expanding the monolithic form.

HOW TO USE THIS FILE
--------------------
1. Read SOURCE_OF_TRUTH.md first. That document is law.
2. Use this file to plan, sequence, and verify work.
3. Never start a REQ whose "Depends on" items are still [ ].
4. A REQ is not done because source exists. It is done when the verification
   gate compiles, runs, and proves the stated effect.

GLOBAL CAUSE (WHY PHASE 2 VOICE IS THE ACTIVE WORK)
---------------------------------------------------
CAUSE: Months of overlapping hotfixes in `src/main.cpp` opened gates to get
       more audio, then closed them to stop garble, then added overlap decode,
       then abs-dedupe, then silence bridges. Field result: partial / blocky
       Phase 2 speech (~50% clear as of v0.2.50). Money spent re-diagnosing
       the same circle.
EFFECT IF THIS PHASE SUCCEEDS: A clear selected TG/slot plays as continuous
       speech (PASS_CONTINUOUS_AUDIO) without leaking encrypted or opposite-slot
       audio.
EFFECT IF WE BYPASS WITH ANOTHER CONSTANT TWEAK: The circle continues. Isolation
       or duty regresses. String verifiers stay green while the speaker does not.


================================================================================
SYSTEM-LEVEL CAUSE → EFFECT CHAIN
================================================================================

  Control-channel grants (P25Control)
          ↓ produces TG / Hz / slot / enc flag
        Follow SM + one-RTL retune
          ↓ produces traffic IQ on the voice channel
        Live decoder (framer, XOR mask, Voice2/4, MAC/ESS)
          ↓ produces unique selected-slot codewords     [bucket A]
        Feed gate → mbelib
          ↓ produces accepted AMBE                      [bucket B]
        Speaker gate
          ↓ produces 20 ms PCM frames                   [bucket C]
        AudioEngine ring (+ optional bridge)
          ↓ produces continuous speaker cadence         [bucket D]
        Follow SM stay-vs-return
          ↓ keeps the tuner on the call                 [bucket E]


================================================================================
FAILURE CLASSIFICATION (LAW FOR EVERY MUTE / GAP)
================================================================================
Before changing a gate, hop, or TTL, name the dominant bucket from counters
already in CADENCE / voicetest:

  A  EXTRACT   unique selected-slot Voice2/4 not advancing
  B  FEED      target VCWs present, mbelib fed ≈ 0
  C  EMIT      fed > 0, speaker PCM emit ≈ 0
  D  PLAYOUT   emit > 0, dutySec ≪ 1 (ring / bridge / islands)
  E  FOLLOW    return-to-CC during a still-live proven call

Pipeline order: A before B before C before D before E. Fix the earliest
bucket that is firing. Classifier: `P25AudioDropClass` (DEC-0002).


================================================================================
PHASE 0 — METHOD DESK (ATHANOR PROCESS ON THIS TREE)
================================================================================
PHASE CAUSE: Without SoT, DECs, and a current task list, every session
             re-invents a hotfix.
PHASE EFFECT: One in-progress REQ, evidence in BUILD_NOTES, no guessed
             constants.


--------------------------------------------------------------------------------
REQ-0.1  In-tree SoT, rules, cause/effect map, and trackers
--------------------------------------------------------------------------------
STATUS: [X]  (2026-09-07 — desk files landed; see docs/LOG.md)

WHY (CAUSE)
  Athanor’s method stopped guess-driven work. This tree had patch-note
  archaeology instead of a binding desk.

HOW TO COMPLETE
  1. DEVELOPMENT_RULES.md (method, not Athanor sovereignty law).
  2. SOURCE_OF_TRUTH.md + this map.
  3. docs/TASKS, ISSUES, DECISIONS, CODE_NOTES, BUILD_NOTES, LOG, SPEC_INDEX.
  4. Retract false "continuous done" claims.

EFFECT IF DONE
  Later REQs have a place to live. Agents cannot honestly start a timeout
  tweak without opening an issue.

EFFECT IF SKIPPED
  The next session repeats July–September 2026.

DEPENDS ON: none
UNLOCKS:    REQ-P2.0

VERIFICATION GATE
  [X] Files exist and SoT L1–L8 are stated
  [X] Roadmap no longer marks continuous audio done
  [X] ISS-0001 open for partial Phase 2 audio


================================================================================
PHASE P2 — CLEAR CONTINUOUS PHASE 2 VOICE  (ACTIVE)
================================================================================
PHASE CAUSE: Control-channel follow works. The speaker does not stay
             intelligible for a whole clear call.
PHASE EFFECT: PASS_CONTINUOUS_AUDIO on a real clear capture; live CADENCE
             dutySec near 1.0 during talk; isolation still PASS.


--------------------------------------------------------------------------------
REQ-P2.0  Classify holes as A–E before any gate change
--------------------------------------------------------------------------------
STATUS: [X]  (BN-0001 classifier; ISS-0001 IQ `drop=` on 105622 / 161748)

WHY (CAUSE)
  Hotfixes targeted symptoms (garble vs silence) without saying which
  pipeline stage dropped the frames.

HOW TO COMPLETE
  1. Pure classifier from existing counters (DEC-0002).
  2. CADENCE 1s line prints `drop=A|B|C|D|E|ok`.
  3. `p25 voicetest` prints the same.
  4. Next code change cites the dominant bucket from a capture or voicetest.

EFFECT IF DONE
  We stop spending sessions on the wrong stage.

EFFECT IF SKIPPED
  Another TTL change lands, string tests pass, speaker still partial.

DEPENDS ON: REQ-0.1
UNLOCKS:    REQ-P2.1 … P2.5 (only the bucket that measurements name)

VERIFICATION GATE
  [X] Unit tests for A/B/C/D/E/ok vectors pass (BN-0001)
  [X] CADENCE and voicetest emit `drop=` (compiled into SDR_Town.exe)
  [X] At least one live or IQ run recorded with a dominant bucket
      (ISS-0001: 105622 voicetest drop=D then drop=ok after DEC-0008/0009;
      live 161748 CADENCE drop=A `no-vcw-from-live-window`)


--------------------------------------------------------------------------------
REQ-P2.1  Unique selected-slot Voice2/4 extraction
--------------------------------------------------------------------------------
STATUS: [X]  (2026-09-07 — 105622 voicetest PASS_CONTINUOUS_AUDIO duty=0.735; DEC-0008/0009)

WHY (CAUSE)
  Overlap re-decode + abs-dedupe can show high `targetVcw` and high `dups`
  with almost no unique feed. Mask-phase error yields high p2sf/p2mask and
  p2mac=0 (docs/P25_FULL_AUDIT_2026_07.md). That is extract failure, not a
  speaker-grace problem.

HOW TO COMPLETE
  Only after REQ-P2.0 names bucket A on a real run.
  Restore unique 20 ms selected-slot codewords without widening overlap
  windows as a substitute for a continuous framer.

EFFECT IF DONE
  Feed gate has real frames to accept.

EFFECT IF SKIPPED
  Feed/emit/playout work on ghosts. Duty stays an island.

DEPENDS ON: REQ-P2.0 (and a measured A)
UNLOCKS:    REQ-P2.2

VERIFICATION GATE
  [X] During talk, unique selected VCW rate supports continuous 20 ms speech
      (105622 skip=97334 8 s: fed=294 unique after dups, duty=0.735,
      `PASS_CONTINUOUS_AUDIO drop=ok`. Unique ~37/s on this grant's 4V/2V/SACCH
      mix; hop-2 720 ms recovered 24 slot-0 VCWs ≈ 33/s. Packed 50/s is not
      this talker's superframe.)
  [X] p2mac or this-slot ESS advances on the same windows (p2macCrc=122,
      targetEssClearWindows=90)
  [X] Isolation still PASS (slot=1 same IQ: duty=0.11, tgStaleMismatchVcw=388,
      oppAmbe=308 not mixed into selected; stickyInvert=no, slotImmutable=yes)


--------------------------------------------------------------------------------
REQ-P2.2  Feed policy: once-clear, this slot, until squelch
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  SDRTrunk queues until PTT/ESS once, then plays that timeslot. We re-prove
  clear every hop and dual-slot MAC-dead mute fights sustain (ISS-0001).

HOW TO COMPLETE
  Only after REQ-P2.0 names bucket B.
  Implement DEC-0003: selected slot with established clear plays Voice2/4
  until END/HANG/squelch. Companion slot never feeds the speaker vocoder.
  Dual-slot MAC-dead must not mute an already-proven selected slot unless
  this-window evidence says the selected bits are wrong-epoch.

EFFECT IF DONE
  Clear calls do not go silent between MAC-CRC islands.

EFFECT IF SKIPPED
  Continuity vs garble circle continues.

DEPENDS ON: REQ-P2.0, DEC-0003; P2.1 if extract is the measured hole
UNLOCKS:    REQ-P2.3

VERIFICATION GATE
  [ ] fed ≈ unique selected VCWs on a proven-clear call
  [ ] Encrypted still fed=0
  [ ] wrongSlot fed=0


--------------------------------------------------------------------------------
REQ-P2.3  Speaker emit of proven selected-slot PCM
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Input-quality / dual-slot speaker gates can wipe PCM after mbelib already
  ran (poison vocoder + holes).

HOW TO COMPLETE
  Only after REQ-P2.0 names bucket C.

DEPENDS ON: REQ-P2.0 (measured C)
UNLOCKS:    REQ-P2.4

VERIFICATION GATE
  [ ] emitPcm tracks fed on clear selected slot
  [ ] Encrypted and wrong-slot still gate=mute


--------------------------------------------------------------------------------
REQ-P2.4  Playout continuity (ring, not bridge-as-product)
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  80–160 ms PCM islands + 4.5 s silence bridge + 180/120 ms primes produce
  choppy speech even when decode is honest (capture 20260807_232020).

HOW TO COMPLETE
  Only after REQ-P2.0 names bucket D.
  Keep the ring fed from real 20 ms frames. Bridge remains an underrun
  guard (SoT L5).

DEPENDS ON: REQ-P2.0 (measured D)
UNLOCKS:    REQ-P2.5 if returns still cut calls

VERIFICATION GATE
  [ ] CADENCE dutySec ≥ 0.65 during talk (target near 1.0)
  [ ] voicetest PASS_CONTINUOUS_AUDIO on a known-clear capture
  [ ] Bridge seconds ≪ real PCM seconds on that run


--------------------------------------------------------------------------------
REQ-P2.5  Follow hold during a proven call
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Activity-silence and no-VCW timers can return to CC during dual-slot mute
  holes (bucket E). Speaker grace exists; it has been both too short and
  too sticky in prior patches. Do not retune it without a measured E.

DEPENDS ON: REQ-P2.0 (measured E)
UNLOCKS:    whole-call continuity

VERIFICATION GATE
  [ ] No ReturnNoVoiceCodewords / ACQ watchdog within 15 s of gate=emit
      on a still-talking clear TG
  [ ] Encrypted still returns immediately (existing follow unit tests)


--------------------------------------------------------------------------------
REQ-P2.6  Isolation non-regression
--------------------------------------------------------------------------------
STATUS: [ ]  (baseline 20260810 isolation PASS; must stay true on HEAD)

WHY (CAUSE)
  Continuity patches have previously leaked latch-bleed across TG hops and
  wrong-epoch dual-slot audio.

HOW TO COMPLETE
  Keep running on every P2.1–P2.5 change.

VERIFICATION GATE
  [ ] Encrypted: PASS_ENCRYPTED_GATED / zero PCM
  [ ] wrongSlot fed = 0 on emit windows
  [ ] No cross-TG speaker contamination (forensic style of 20260810_134531)


================================================================================
EXECUTION ORDER (DO NOT SKIP AHEAD)
================================================================================

  0.1 desk ─► P2.0 classify
                 ├─► if A: P2.1 extract
                 ├─► if B: P2.2 feed
                 ├─► if C: P2.3 emit
                 ├─► if D: P2.4 playout
                 └─► if E: P2.5 follow
        P2.6 isolation runs beside every change

  PASS_CONTINUOUS_AUDIO + P2.6  =  Phase P2 voice done.

FORBIDDEN SHORTCUTS
  - Starting P2.1–P2.5 without a classified capture/voicetest.
  - Opening security to raise duty, then closing it to kill garble, without
    naming the bucket.
  - Treating verify_p25_phase2_*.py as PASS_CONTINUOUS_AUDIO.


================================================================================
ROLL-UP CHECKLIST
================================================================================

PHASE 0
[X] REQ-0.1  Method desk     — CAUSE: stop hotfix archaeology; EFFECT: one REQ at a time

PHASE P2
[ ] REQ-P2.0 Classify A–E    — CAUSE: wrong-stage fixes; EFFECT: next patch has a named hole
[ ] REQ-P2.1 Unique extract  — CAUSE: overlap/mask starve unique VCWs; EFFECT: feed has frames
[ ] REQ-P2.2 Once-clear feed — CAUSE: per-hop re-prove + dual-slot mute; EFFECT: slot plays
[ ] REQ-P2.3 Emit proven PCM — CAUSE: post-mbelib mute; EFFECT: emit tracks fed
[ ] REQ-P2.4 Playout duty    — CAUSE: islands + bridge; EFFECT: dutySec during talk
[ ] REQ-P2.5 Follow hold     — CAUSE: return during holes; EFFECT: call not cut
[ ] REQ-P2.6 Isolation       — CAUSE: continuity patches leaked before; EFFECT: no mix/enc

================================================================================
END OF CAUSE / EFFECT MAP — OBEY SOURCE_OF_TRUTH.md, TRACK GATES HERE
================================================================================
# Release gate (DEC-0090 / T-0027)

Unchecked native failures and hard-coded master could publish stale assets or
the wrong branch. Checked commands, clean-source preflight, current-branch push
and independent package/signature validation fail closed. Gate: negative verifier
tests, full CTest/CLI/GUI gates, signed package check and uploaded checksum match.
This does not close P25 continuity or future SSTV/satellite reception gates.
# SSTV first gate (DEC-0091 / T-0022)

Before image or live routing, recognize classic VIS from bounded recorded audio
without guessing a mode from frequency alone. Gate: protocol/parity/framing and
arbitrary-partition tests, unknown-code preservation, reset/invalid inputs,
independent pinned M1 header, CLI negative files and full existing regression.
Header acceptance does not certify image decoding, pixel order, slant or RF quality.
