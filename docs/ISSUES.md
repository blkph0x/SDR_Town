# Issues (canonical)

Never delete a row. Close with a commit hash and a sentence.

Status: `open` | `closed`

---

## ISS-0001 — P25 Phase 2 speaker audio is partial, not continuous

- **Status:** open
- **Opened:** 2026-09-07
- **REQ:** REQ-P2.0 … P2.6
- **Measured 2026-09-08 capture `20260908_041716` + DEC-0012 voicetest:**
  - Live: 70.75 s gapless, SNR ~18 dB. Operator: one good emit then wrong-slot
    garble. Call 2 seq=131 `opp=0 p2mac=5/6`; seq=134 `target=6 opp=12
    p2mac=0/0 ess=clear gate=emit`.
  - File: TG 10330 slot 1 skip=32111 center=421.21375 8 s
    `PASS_CONTINUOUS_AUDIO duty=0.87`. Companion-louder mixed hops no longer
    emit. 073304 same gate `duty=0.795`. 105622 slot 0 `duty=0.645` (was 0.685).
- **Measured 2026-09-07 (AppData captures + HEAD voicetest):**
  - Logs live in `%APPDATA%\SDR_Town\SDR Town\iq_test_captures\` and `...\logs\`.
  - Capture `20260905_161748`: 68 s gapless IQ; **1** `gate=emit` (320 ms); **41** silence-bridge top-ups; CADENCE **30/33 s** `block=no-vcw-from-live-window` (drop **A** after the island). Still needs live re-prove on DEC-0008/0009 binary (T-0010).
  - Capture `20260905_105622` TG 30003 slot 0 skip=97334 8000 ms, after DEC-0008/0009:
    `result=PASS_CONTINUOUS_AUDIO drop=ok duty=0.735 audioSeconds=5.88 spanSeconds=8`
    `targetVcw=396 fed=294 emitPcm=294 p2macCrc=122`. Same IQ slot=1: duty=0.11
    (companion not mixed into selected). Before those DECs: `PASS_PARTIAL_AUDIO`
    drop=D duty=0.28 (sticky lattice on independent block eyes; 160 ms sustain).
- **Measured 2026-09-07 live capture `20260907_073304` (146 s gapless) + HEAD voicetest after DEC-0013:**
  - Live: some audio better; operator still heard garbled + repeats. CADENCE TG 10330 17:34:54–58 dups=78–100/s feedRatio=0.29; seq=389 ctxVcw=10 ctxDrop=0 after seq=386 emit=18 (overlap replay). TG 30302 seq=9 opp=16 p2mac=0/2 gate=emit (mixed MAC-dead garble). Later TG 30302 dutySec=1.28–1.36 with dups=0 (overfill).
  - Voicetest HEAD after DEC-0013 (ISCH lattice de-dupe, not lock-only):
    - 105622 TG 30003 slot 0 skip=97334 8 s: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685` (was 0.735 before lattice; still ≥0.65).
    - 073304 TG 10330 slot 1 skip=107597 8 s: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.76 timelineOk` `fed=304 dupSuppressed=512` (repeat slice; no overfill).
    - 073304 TG 30302 slot 1 skip=771 8 s: `PASS_PARTIAL_AUDIO duty=0.41` (short first call + hang).
    - 073304 TG 30302 slot 1 skip=128684 8 s dual-TG with 20202: duty=0.715 but `concealmentOk=no` (plc=76/286). Mixed-epoch garble remains T-0004. Post-emit companion-louder skip is DEC-0012.
- **Measured 2026-09-07 live capture `20260907_095450` (93 s gapless, SNR ~5.4 dB):**
  - Dual grant same RF: TG **30013 SLOT=1** + TG **30003 SLOT=0** at 420.225 MHz.
  - Operator: slot bleed, wrong cadence, timing issues.
  - First eye 720 ms `dsp=327 ms` then seq=2 `decode-wall-timeout`. Later hops
    160–360 ms `dsp=159–573 ms`. seq=21 `targetVcw=10 oppVcw=10 fed=10 ctxDrop=0 p2mac=0/2`.
  - First CADENCE `windows=593 dutySec=2.320` dumped 53 s as `1s` (`lastLogMs==0`).
  - Voicetest after DEC-0014 revert (block-channelize default): TG 30013 slot 1
    skip=3741 8 s `PASS_PARTIAL_AUDIO duty=0.625` `oppAmbe=759/920`
    `plc=180/290 concealmentOk=no`. Companion TG 30003 slot 0 same skip
    `duty=0.58 oppAmbe=719/872`. Isolation does not hold on this dual-TG RF
    (T-0004). Default-on streaming DDC was rejected by 105622 duty 0.095.
- **Measured 2026-09-07 live capture `20260907_115315` (162 s gapless, SNR ~17 dB):**
  - DEC-0015 two-channel LO: TG 30003 420.725 offset **261.3 kHz**; then 421.975
    `rfCenter=420.97773` offset **997.3 kHz** (voice at usable-half edge).
    CADENCE `drop=D dutySec=0.22–0.45` then `drop=A` no-vcw / no-sf-mask.
    TG 30302 421.225 reused that LO (`existing-wideband-source-low-if`) almost
    all `drop=A`. Operator: audio really bad. DEC-0016 parks voice, not the set.
- **Measured 2026-09-07 live `22:27:17`–`22:28:20` (no IQ file):**
  DEC-0016 LO ok (TG 10128 420.725, center 420.713751, offset 11249 Hz).
  Gaps are extract: 29 hops `iqRej=88/212`, emit 102, **3.8 s PCM / ~36 s**,
  gate `hard-soft-quality-low` / AMBE rejected. Drop **A**. Need start/stop
  capture to voicetest; do not retune a quality threshold from this listen.
- **Measured 2026-09-07 live capture `20260907_123525` (258 s gapless, SNR ~15 dB):**
  DEC-0016 LO ok on every follow (offset 11.2 kHz). Operator: all emits gappy,
  some clearer. Live CADENCE: 88 emit-seconds, median duty **0.34**, drop **D**
  on talk (`fed≈emit`, `dups` ≈ 0.58 of `target`). 80 ms fresh + 280 ms overlap
  hops; ring bridge + underruns. File voicetest TG 30003 slot 0 skip=20000 8 s
  `center=417.66375`: `PASS_PARTIAL_AUDIO drop=ok duty=0.64` `fed=emit=260`
  `gaps=15`; hops alternate selected emit vs `wrong TDMA slot`. SigMF meta
  still says 420.475 after retune — use the parked `--center` for replay.
- **Measured 2026-09-08 DEC-0017/0018:**
  - DEC-0017 360 ms fresh-only block hops rejected (105622 duty 0.35).
    Constants restored to DEC-0009. Re-prove 105622 `duty=0.685`.
  - DEC-0018 streaming lattice jump skipped; env=1 still duty 0.16.
    Default-on still rejected (DEC-0014). Live hole remains drop **D**.
- **Evidence already in tree (do not re-guess):**
  - README v0.2.50: clear Phase 2 ~50% of the time.
  - Baseline 20260810_134531: isolation PASS, continuity PARTIAL (`docs/P25_BASELINE_CLEAR_CONTINUOUS_20260810.md`).
  - Capture 20260808_034136: dual-slot MAC-dead + sticky ESS → blocky wrong-epoch PCM; mute raised silence.
  - Capture 20260809_004206: blanket MAC==0 mute dropped duty 0.155→0.055 on windows that had this-window ESS.
  - CADENCE/docs: high `dup`/`absDup`, `feedRatio` collapse, `no-vcw-from-live-window`, ring underruns between 80–160 ms islands.
  - `docs/P25_FULL_AUDIT_2026_07.md`: p2sf/p2mask high, p2mac=0, decoded=0 → mask epoch / ACCH, not “no grant”.
- **Must not invent:** a new grace/TTL/minFresh, PLC PCM, or “open the security gate a little” until REQ-P2.0 names the bucket on a current run.

## ISS-0002 — String-only `verify_p25_phase2_*.py` treated as continuity proof

- **Status:** open
- **Opened:** 2026-09-07
- **REQ:** REQ-P2.0 / DEVELOPMENT_RULES §8
- **Unknown:** none — the scripts assert substrings in `main.cpp`. They cannot measure dutySec.
- **Must not invent:** adding more string guards instead of voicetest / CADENCE.
- **Unblock by:** keep scripts as invariant locks if useful; never flip a SoT checkbox from them.

## ISS-0003 — Dead 180 ms speaker catch-up constants vs live planner

- **Status:** open
- **Opened:** 2026-09-07
- **REQ:** REQ-P2.4 (later)
- **Unknown:** whether any remaining path still uses `kP25Phase2VoiceDecodeSpeakerCatchUp*` after README backed the 180 ms experiment out (20260903).
- **Evidence:** `p25Phase2PlanVoiceDecodeChunk` routes active speaker to 80/40/80 sustain and refuses 180 ms live-edge skip (`src/main.cpp`). Verify scripts still assert the 180 ms names exist.
- **Must not invent:** re-enabling 180 ms catch-up to “raise duty”.
- **Unblock by:** after P2.0, either delete the dead constants or wire them only behind a DEC with a capture id.

## ISS-0004 — P25 orchestration lives in a ~34k-line `main.cpp`

- **Status:** open
- **Opened:** 2026-09-07
- **REQ:** T-0009 (after voice gate)
- **Unknown:** the exact split boundaries that will not break Qt thread/DSP ownership.
- **Must not invent:** a rewrite in the same commit as a feed-gate change.
- **Unblock by:** PASS_CONTINUOUS_AUDIO first, then a DEC for module split.

## ISS-0005 — Product docs claimed continuous audio done while field audio is partial

- **Status:** closed
- **Opened:** 2026-09-07
- **Closed:** 2026-09-07 — `docs/P25_AUDIO_PRODUCT_ROADMAP.md` item 1 marked open; SoT L3; README points at SoT.
- **REQ:** REQ-0.1

## ISS-0006 — TIA-102 Phase 2 specification not in-tree

- **Status:** open
- **Opened:** 2026-09-07
- **REQ:** SPEC_INDEX
- **Unknown:** we do not have a licensed TIA-102.BAHA/BAHB (etc.) PDF in this repo. Implementation cites SDRTrunk and OP25 under `_codex_refs/` plus field captures.
- **Must not invent:** bit offsets, LFSR taps, or MAC opcodes from memory. If SDRTrunk and OP25 disagree, open a DEC with both file paths; do not average them.
- **Unblock by:** operator-supplied spec excerpts, or a DEC that names the SDRTrunk class as the cited implementation for that field.

## ISS-0007 — No golden IQ fixture in this clone for voicetest on HEAD

- **Status:** closed
- **Opened:** 2026-09-07
- **Closed:** 2026-09-07 — used AppData `20260905_105622` TG 30003 slot 0
  skip=97334; `PASS_CONTINUOUS_AUDIO` on HEAD after DEC-0008/0009.
- **REQ:** REQ-P2.0 gate “at least one IQ or live run”
