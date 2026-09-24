================================================================================
SDR TOWN — SOURCE OF TRUTH (SoT)
================================================================================
VERSION: 1.0.0
PRODUCT: Windows SDR receiver (brand: SDR Town; on-disk folder: maulaudio_pro)
CURRENT VERSION IN TREE: 0.2.93
ACTIVE PHASE: Receive decoder expansion; P25 acceptance remains open, work deferred by user
METHOD: Athanor / SovereignFoundry process (never guess, evidence, trackers).
         This tree is NOT Athanor. Qt, SoapySDR, mbelib, miniaudio stay.
DO NOT EDIT: C:\Users\Blkph0x\source\repos\SovereignFoundry
================================================================================

AI AGENT INSTRUCTION: Parse this file. Track milestones by altering [ ] to [X]
only after the verification gate in CAUSE_EFFECT_MAP.md is green. Compiling is
not a gate. A `verify_p25_phase2_*.py` string check is not a gate.

Companion files:
  DEVELOPMENT_RULES.md     binding day-to-day rules
  CAUSE_EFFECT_MAP.md      why / how / effect / gate per REQ
  docs/                    living trackers (see docs/README.md)


--------------------------------------------------------------------------------
1. WHAT THIS PRODUCT IS
--------------------------------------------------------------------------------
SDR Town is a Windows desktop SDR receiver: analog demod, spectrum/waterfall,
multi-output audio, P25 Phase 1 control-channel work, and experimental
P25 Phase 2 TDMA trunk follow. Lawful unencrypted receive only. Encrypted
traffic must never open the speaker.

It is useful RF test software. It is not a finished production trunking
scanner. Do not write as if it were.


--------------------------------------------------------------------------------
2. PRODUCT LAW (NON-NEGOTIABLE)
--------------------------------------------------------------------------------
L1. Encrypted grants, encrypted ESS, and unknown-security grants do not open
    the speaker by default. Fail closed. Lab probes are explicit flags only.

L2. Opposite TDMA slot must never mix into the selected-slot speaker path.
    Decode the companion for observe/record. Do not feed it to the ring.

L3. Clear selected TG + slot must produce **continuous** 20 ms AMBE cadence
    for the duration of the call. `PASS_PARTIAL_AUDIO` is a diagnosis, not
    done. Done is `PASS_CONTINUOUS_AUDIO` (duty ≥ 0.65 plus the cadence /
    sequencer / AMBE / concealment checks already in `src/main.cpp`) on a
    real clear capture, and/or live CADENCE `dutySec` near 1.0 while talking.

L4. A later gate must not compensate for an earlier stage that failed.
    Classify every mute/gap as A–E (CAUSE_EFFECT_MAP REQ-P2.0) before
    changing a timeout, hop size, or security predicate.

L5. Do not invent PLC / dummy PCM / silence-as-speech to hide missing
    Voice2/4. The playout bridge is an underrun guard, not a vocoder.

L6. Do not add or retune a grace/TTL/minFresh/overlap constant "to see if
    it helps." Constants require a cited spec, SDRTrunk/OP25 source under
    `_codex_refs/`, a capture id, or a DEC.

L7. Isolation (L1, L2) must not be traded for duty. The 2026-08-10 baseline
    already had isolation PASS and continuity PARTIAL. Continuity work must
    not reopen cross-TG or wrong-slot speaker leak.

L8. Analog WFM/AM/NFM paths are not the current REQ. Do not "fix P25" by
    changing analog demod unless a DEC proves the shared DSP is the cause.


--------------------------------------------------------------------------------
3. FEATURE INVENTORY (HONEST)
--------------------------------------------------------------------------------
Working well (keep; do not destabilize for P25 experiments):
  [X] Analog WFM / AM / NFM receive
  [X] GUI spectrum + waterfall, device manager, multi-output miniaudio
  [X] RTL-SDR / SoapySDR open + stub path without hardware
  [X] P25 Phase 1 control-channel C4FM: sync, NID, TSBU/TSBK, grants
  [X] P25 grant follow + one-RTL traffic retune / independent traffic source
  [X] Phase 2 superframe / ISCH / XOR mask generation (NAC/WACN/SysID)
  [X] Encrypted mute (security fail-closed when proof says encrypted)
  [X] Updater: GitHub latest + Ed25519 manifest when key configured
      (published tester: v0.2.91 experimental)

Shipped experimental in v0.2.74 (user deferred further P25 work and authorized
receive expansion). These are not P25 SoT gates and are not live-RF accepted:
  [X] SDRplay SoapySDRPlay3 profile/UI/CLI/API. Host must already have API 3.x
      + sdrPlaySupport.dll. Installer does not redistribute those vendor files.
  [X] Satcom scanner, observer map, TLE/SGP4 Doppler, ISS SSTV arm (unit tests;
      live pass/Doppler RF acceptance open)
  [X] Aircraft map (OpenSky + optional local 1090)
  [X] Inmarsat prototype UI/API/CLI (band plans + ACARS/ADS-C parse).
      Voice follow disabled (DEC-0105). Gaps: docs/INMARSAT.md.
  [X] Tuner lease: satcom/Inmarsat/aircraft cannot steal listen without force
  [X] CLI: observer, tle, satcom, inmarsat, aircraft, sdrplay, devices rescan

Active / not done (P25 product gate — still open; user deferred further P25 DSP):
  [X] REQ-P2.0  Classify every audio hole as A–E before changing gates
  [X] REQ-P2.1  Unique selected-slot Voice2/4 extraction at speech rate
  [ ] REQ-P2.2  Feed policy: once-clear, play this slot until squelch
  [ ] REQ-P2.3  Emit proven selected-slot PCM (no dual-slot MAC-dead mute
                of an already-proven slot)
  [ ] REQ-P2.4  Speaker ring continuity without bridge-as-product
  [ ] REQ-P2.5  Follow hold: do not return-to-CC during a proven call
  [ ] REQ-P2.6  Isolation non-regression (encrypted + wrong-slot)

Roadmap (not in 0.2.74 as production features):
  [ ] DMR / NXDN / pager
  [ ] Meteor LRPT / SatDump / qualified NOAA APT line-sync
  [ ] Production Smart Scan (priority / lockout / hold product)
  [ ] ONNX classifier backend (placeholder today)
  [ ] Authenticode
  [ ] P25 Phase 2 clear TX on air (sprints 0–4 are lab shells)
  [ ] Package follow-up T-0041: copy data/inmarsat into deploy staging; do not
      glob extra old `SdrTownControl-0.2.N-win64.dll` into the Town ZIP.
      The **0.2.74** versioned DLL is the FUBAR pairing asset (rename to
      `SdrTownControl.dll` beside FUBAR.exe). See docs/FUBAR_PAIRING.md.

Historical marker, not a green SoT checkbox:
  p25-clear-continuous-20260810 — isolation PASS, continuity PARTIAL.
  Binary: build/baselines/SDR_Town_p25_clear_continuous_20260810.exe
  Evidence: docs/P25_BASELINE_CLEAR_CONTINUOUS_20260810.md
  README v0.2.50 field report: clear Phase 2 voice ~50% of the time.


--------------------------------------------------------------------------------
4. PIPELINE LAW (RF → SPEAKER)
--------------------------------------------------------------------------------
  IQ ring
    → decode chunk plan (cold eye / sustain / streaming DDC)
    → P25LiveDecoder (CQPSK/C4FM, framer, XOR mask, Voice2/4, MAC/ESS)
    → selected-slot feed gate → mbelib
    → speaker security gate → PCM
    → AudioEngine ring (optional silence bridge)
    → follow SM (stay vs return-to-CC)

If stage N fails, fix stage N. Do not open stage N+1 to hide it.

Cited behaviour we implement (not invent):
  SDRTrunk P25P2AudioModule: one AudioModule per timeslot; queue Voice2/4
  until PTT/ESS establishes clear/enc **once**; then play that slot until
  squelch/reset. Never mix opposite slot. No sliding 12 s security TTL as
  the call-lifetime proof. See `_codex_refs/sdrtrunk` and DEC-0003.


--------------------------------------------------------------------------------
5. FORBIDDEN SHORTCUTS
--------------------------------------------------------------------------------
  - Another speaker-grace / TTL / minFresh tweak without a classified
    A–E measurement (kills REQ-P2.0 and rule L6).
  - Marking continuous audio done because a string verifier passed.
  - Marking docs/P25_AUDIO_PRODUCT_ROADMAP.md "done" while voicetest
    still prints PASS_PARTIAL_AUDIO.
  - Sticky ESS / grant-clear feeding mbelib through MAC-dead dual-slot
    chaos (capture 20260808_034136).
  - Muting an already-proven selected slot solely because the companion
    slot is also visible this window, without this-window proof that the
    selected-slot bits are wrong-epoch (continuity vs garble circle).
  - Linking a vendor P25 SDK or JMBE to dodge a gate.
  - Editing the Athanor repo from this project.


================================================================================
END OF MANIFEST — OBEY THIS FILE, TRACK GATES IN CAUSE_EFFECT_MAP.md
================================================================================
