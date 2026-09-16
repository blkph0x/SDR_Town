# Build notes

Newest entry at the top. Record facts, not hopes.

---

## BN-0057 — DEC-0066 dead-grant timeout (131458) (2026-09-15)

- **Evidence:** 0 emits; unknown-grant ACQ hangs ~45s with no VCW.
- **Fix:** unknown cold no-VCW 8s/6s; WaitingForClearGrant alone no longer
  keeps acquire for 30s; clear cold 10s/7s.
- **Gate:** DEC-0066 verifier PASS; `[p25]` **118/118**; Release rebuilt.

## BN-0056 — DEC-0065 RF-home return (125341) (2026-09-15)

- **Evidence:** return claimed CC without retune while cf still on voice low-IF.
- **Fix:** force retune/warm-standby when RF away from CC; latch RetunedPrimary
  on physical LO leave.
- **Gate:** `verify_p25_phase2_dec0065_rf_home_return.py` PASS; DEC-0064/0063 PASS;
  `[p25]` **118/118**; Release `SDR_Town.exe` rebuilt.
- **Audio note (same capture):** TG10120 had a short clear stretch then drop=A /
  ACQ watchdog; sparse emits are a separate duty track (absDup/feedRatio).

## BN-0055 — DEC-0064 warm-standby return-to-CC (2026-09-15)

- **Evidence:** after bridge follow, return claimed CC while RF on voice → validation
  disable → P25 log stopped.
- **Fix:** pause CC decode/validation in warm-standby; reset validation on real CC
  retune; idle arm requires RF on CC; expire uses returnControlFreqHz fallback.
- **Gate:** `verify_p25_phase2_dec0064_warm_standby_return_cc.py` PASS;
  DEC-0063/0055 PASS; `[p25]` **118/118**; Release `SDR_Town.exe` rebuilt.
- **Residual:** live bridge Monitor-CC → follow → return must show
  `validation armed` / continued CC lines (no `CC disabled` after warm-standby).

## BN-0054 — DEC-0063 idempotent control arm / refuse tune (2026-09-15)

- **Evidence:** FUBAR DLL follow → snap to Monitor CC 420.350 mid-call.
- **Fix:** same-CC arm keeps live follow; analog tune 409 unless force; FUBAR
  Tune status guard; control tune logging + voiceFrequencyHz status.
- **Gate:** `verify_p25_phase2_dec0063_idempotent_control_arm.py` PASS;
  DEC-0055 PASS; dual-slot garble PASS; `[p25]` **118/118**; Release
  `SDR_Town.exe` + `SdrTownControl.dll` rebuilt.
- **Residual:** live follow stick + RID audio need a post-0063 start/stop
  capture (no new keep-set IQ in this pass). FUBAR `sdr_town_bridge.cpp`
  edited; rebuild that app so Tune refuses client-side too (server 409 still
  protects with old FUBAR).

## BN-0053 — DEC-0062 talkspurt vocoder reset (225923) (2026-09-13)

- **Evidence:** same-grant multi-RID; uniqueFreshR≈1 on BAD; no mid-grant mbelib reset.
- **Fix:** MAC_PTT / post-END resets selected vocoder; abs-dedupe kept.
- **Gate:** DEC-0062 verifier; `[p25]`; Release rebuild.

## BN-0052 — DEC-0061 speaker backlog 240+280 (153932) (2026-09-13)

- **Evidence:** post-0060 jitter — WAV island p50=40 ms; bridge top-ups; ctx=80 ms.
- **Fix:** catch-up 240 ms fresh + 280 ms overlap; sustain 80+280 unchanged.
- **Gate:** DEC-0061/0060-supersede/0058/0059 verifiers; `[p25]`; Release rebuild.

## BN-0051 — DEC-0060 speaker backlog 280+80 (152348) (2026-09-13)

- **Evidence:** half-audio pcm_vs_wall≈0.5; dsp p50>160 ms fresh; absDup=context waste.
- **Fix:** catch-up 280 ms fresh + 80 ms overlap; sustain 80+280 unchanged.
- **Gate:** DEC-0060/0058/0059 verifiers PASS; `[p25]` **118/118**; Release rebuilt.

## BN-0050 — DEC-0059 companion ESS / ReturnEncrypted (145139) (2026-09-13)

- **Evidence:** Clear RID aborted mid-emit (`ReturnEncrypted ess=enc`) while
  follow still clear; companion enc opposite slot; pending targetVcw=0.
- **Fix:** this-burst ESS; recent clear clears sticky enc; target-only follow ESS;
  refuse opposite-only pending drain.
- **Gate:** verifier PASS; `[p25]` **118/118**; Release `SDR_Town.exe` rebuilt.

## BN-0049 — DEC-0058 speaker backlog / dual-slot (142104) (2026-09-13)

- **Evidence:** 80 ms fresh / 200–600 ms dsp; waiting-clear islands after Clear latch.
- **Fix:** backlog catch-up 160+280 on speaker path; latched selected-dominant keep.
- **Gate:** verifier erify_p25_phase2_dec0058_speaker_backlog_dual_slot.py; [p25]; Release.

## BN-0048 — DEC-0057 wrong-TDMA companion dwell (`135857`) (2026-09-13)

- **Evidence:** TG30003 CLEAR ~29s; wrong_tdma 47; companion oppVcw during silence.
  Catch: I-ISCH absolute index 10 → grantSlot 1 (final C); lock-rel-only was wrong.
- **Fix:** immutable grant → no wrong-slot brand; keep DEC-0055.3 absolute grantSlot.
- **Gate:** Catch I-ISCH absolute case; verifiers 0055+0057; Release rebuild.

## BN-0047 — DEC-0056 clear hang + WFM default BW (`134135`) (2026-09-12)

- **Evidence:** Clear hang ~57s vs Enc &lt;1s; P25 meta 12.5 kHz LPF off.
- **Fix:** speaker grace needs live traffic; post-speech no-VCW 12/6s;
  lastActive not structure-only after clear speech; WFM default 220 kHz.
- **Gate:** `[p25][follow]` Catch + Release rebuild.
- **Still open:** mid-call clear blocky (feed/budget/worker) — B-0001.

## BN-0046 — DEC-0055 epoch / dual-slot keep / I-ISCH origin (2026-09-12)

- **Evidence:** forensic code audit — DualSlot clear after Clear latch; soft
  epochTrusted garble arm; lock-relative grantSlot without I-ISCH origin.
- **Fix:** keep-selected PCM on dual-slot Clear; tight epochTrusted;
  absolute index rebase when A/B I-ISCH agree (no flip-only).
- **Gate:** Release rebuild; `[p25]` Catch; verifiers incl.
  `verify_p25_phase2_dec0055_epoch_dual_slot_origin.py`.
- **Not proven:** live listen CLEAR multi-second (B-0001).

## BN-0045 — DEC-0054 restore cold full-commit (`081416`) (2026-09-12)

- **Evidence:** post-0053 capture 0.36s SILENT / 1 emit; 061217 had 92s CLEAR.
- **Fix:** drop CQPSK headroom; cold full annotate +200ms; sticky cheap 120ms.
- **Gate:** budget verifier + Release rebuild.

## BN-0044 — DEC-0053 sticky cheap-commit not skip (`064509`) (2026-09-12)

- **Evidence:** post-0052 capture: 2 cold CLEAR emits then permanent no-vcw.
- **Fix:** remove sticky skip-commit; re-arm 50 ms cheap-commit allowance.
- **Gate:** `verify_p25_phase2_budget_skip_sticky_commit.py` + Release rebuild.

## BN-0043 — DEC-0052 sticky skip-commit after budget (`061217`) (2026-09-12)

- **Evidence:** emit p50≈223, busy 690, 0 DEC-0051 trip lines; listen CLEAR.
- **Fix:** skip commit on sticky sustain when deadline gone; cheap cold commit;
  CQPSK headroom; log `P25 budget trip:`.
- **Gate:** verifiers for DEC-0052 + rebuild Release.

## BN-0042 — DEC-0051 cooperative budget abort (2026-09-12)

- **Evidence:** `044651` emit dsp p50≈212 ms, worker-busy 135, rolling→15.9 s;
  wall-timeout 0 (DEC-0046 post-hoc insufficient).
- **Change:** `armRealtimeDecodeBudget` + mid-decode aborts in
  `P25LiveDecoder::processIq` / Phase-2 sync-lock-mask loops; Catch `<350 ms`.
- **Gate:** Release rebuilt; `[p25]` **114/114**; verifiers **133/133**
  (incl. `verify_p25_phase2_cooperative_budget_abort.py`)
- **Operator:** PPM≈−2; one start/stop capture; run listen-bar harvester.

## BN-0041 — DEC-0050 PCM listen classifier (2026-09-12)

- **Why:** Replay duty passes while live sounds bad/silent — need automated
  CLEAR vs GARBLED vs SILENT on speaker PCM, plus live WAV sidecar.
- **Tools:** `p25_pcm_listen_classify.py`, `run_p25_listen_bar_harvester.py`,
  `p25 listenclassify`, forensic wav= + live_listen.
- **Capture:** start/stop writes `*_live_speaker.wav` from speaker-push path.

## BN-0040 — DEC-0049 Auto PPM harden after `044651` (2026-09-12)

- **Evidence:** Auto PPM AFC=1250 conf=0.45 → device ppm −7.88; CC TSBK
  corrections climbed; TG10120 live ok≤0.909 vs TG20202 drop D
- **Change:** reject ±1250 rail; conf≥0.55; step≤1.5; cooldown 120s; trusted
  offset only; full forensic script
- **Result:** `[p25]` 113/113; verifiers 131/131
- **Operator:** set PPM near **−2.0** before next listen (undo −7.88)

## BN-0039 — DEC-0047/0048 logscan + eye-lost streak=1 (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Evidence:** `041612` live A-cliff vs file TG20202 duty 0.805
- **Change:** `p25 logscan`; `kP25LiveEyeLostReplayCandStreak=1`
- **Result:** `[p25]` 112/112; verifiers 131/131; Release rebuilt
- **CLI:** `SDR_Town.exe --cli --cmd "p25 logscan <capture_dir> --audit"`

## BN-0038 — DEC-0046 wall clamp rejected / pending guard (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Trigger:** post-0045 “really bad audio” regression
- **Change:** revert healthy/eye-lost wall clamp; never clear speaker pending
  on decode-wall stamps; keep DEC-0044 auto PPM
- **Result:** `[p25]` **112/112**; verifiers **131/131**; Release rebuilt
- **Not proven:** live CADENCE recovery (operator listen + startstop)

## BN-0037 — DEC-0044/0045 auto PPM + healthy wall (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Trigger:** `032907` — promising then lose-it; emit-gate dsp p50≈451 ms;
  ppm=0 with AFC≈884 Hz
- **Change:** auto PPM on return-to-control; healthy wall 105 / eye-lost 145
- **Result:** `[p25]` 109/109; verifiers **131/131** (new auto-ppm/wall
  verifier). Release `SDR_Town.exe` rebuilt.
- **Not proven:** live CADENCE / `Auto PPM:` log line (operator listen)

## BN-0036 — DEC-0043 twin rescue reverted after `024000` (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Trigger:** live `024000` clear TG30003 @421.975 file duty 0.705 vs live
  max 0.649 / wrong-TDMA / worker-busy
- **Change:** remove ±1 DUID lock-twin rescue; keep post-speak opp-dominant
  invalidate debounce ≥3
- **Result:** `[p25]` 109/109; verifiers 130/130
- **Not proven:** live CADENCE recovery (operator re-listen required)

---

## BN-0035 — DEC-0043 wrong-TDMA sticky debounce (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests "[p25]"` 109 cases; `verify_p25_phase2_*.py` 130/130
- **Voicetest (`020758`):** TG20201 clear slot1 skip=18439 8s
  `PASS_CONTINUOUS_AUDIO duty=0.715`; TG12069
  `PASS_ENCRYPTED_GATED` essEncrypted=yes
- **Missing on disk:** keep-set 060036 / 095846 (only `020758` present)
- **Not proven:** live CADENCE continuity on new GUI follow (B-0001 / T-0010)

---

## BN-0034 — DEC-0041 live eye-lost budget (2026-09-12)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests "[p25]"` 109 cases; `verify_p25_phase2_*.py` 129/129
- **Voicetest:** 060036 TG10301 slot0 skip=261000 8s `PASS_CONTINUOUS duty=0.705`;
  095846 TG10301 slot1 skip=68700 `PASS_CONTINUOUS duty=0.8`
- **Not proven:** live CADENCE drop-D cut on new GUI follow (B-0004 / T-0010)

---

## BN-0033 — Add Receiver arms primary DSP (2026-09-11)

- **Host:** Windows 10.0.22631 x64
- **Change:** `MainWindow` Add Receiver now `syncMonitorVarsToReceiver(0)` +
  `setReceiverActive(0, true)` after `startStreaming` (same class as Apply/Scan
  `ecf9303`).
- **File bar:** 060036 TG 10301 skip=261000 block path
  `PASS_CONTINUOUS_AUDIO duty=0.705` (HEAD rebuild).
- **Not proven:** live CADENCE re-prove (T-0010 / B-0001).

---

## BN-0032 — GitHub Actions Windows CI added (2026-09-11)

- **Workflow:** `.github/workflows/windows-ci.yml`
- **Gate:** MSVC Release `SDR_Town` + `sdr_town_tests`, then all
  `verify_p25_phase2_*.py`
- **Deps:** jurplel Qt 6.7.3 + bootstrap vcpkg (manifest) + `external/miniaudio` +
  `external/mbelib` only (skip broken `_codex_refs` gitlinks)
- **CI fix notes:** missing `#include <set>` in `DeviceManager::getAvailableDrivers`
- **Backlog:** `docs/BACKLOG.md` (B-0020)

---

## BN-0031 — ISS-0008…0011 verifier/docs + live pipeline extract (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests.exe` 214 cases / 10194 assertions; `verify_p25_phase2_*.py` 129/129
- **Layout:** `MainWindowP25Orchestration.cpp` ~1.4k (`startP25LiveDecodePipeline`);
  `MainWindow.cpp` ~10.4k; 14 verifiers on `definition_body` anchors
- **Not proven:** live CADENCE re-prove (T-0010)

## BN-0030 — ISS-0004 MainWindowP25Voice TU split (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests.exe` 214 cases / 10194 assertions; `verify_p25_phase2_*.py` 129/129
- **Layout:** `MainWindowP25Voice.cpp` ~1.3k (worker/submit/backpressure/publish);
  `MainWindow.cpp` ~11.8k (ctor/UI remainder); ISS-0010 / ISS-0011 filed
- **Not proven:** live CADENCE re-prove (T-0010)

---

## BN-0029 — ISS-0004 Phase A–B MainWindow out-of-line (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `ctest` UnitTests PASS; `verify_p25_phase2_*.py` 129/129
- **Layout:** `main.cpp` ~200; `MainWindow.h` ~520 (decls); `MainWindow.cpp` ~13k;
  plus `P25VoiceSession` / `P25DecodeConfig` / `DemodModeUtils` / `SavedFrequencies`
- **Not proven:** live CADENCE re-prove (T-0010); further MainWindow ctor/worker TU split

---

## BN-0028 — DEC-0040 / ISS-0004 split `main.cpp` (2026-09-10)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests -j 8`
- **Result:** PASS; `sdr_town_tests.exe` 214 cases / 10194 assertions; `verify_p25_phase2_*.py` 129/129
- **Layout:** `P25VoiceTiming` / `P25TalkgroupRegistry` / `P25AppGlobals` / `P25RollingIq` /
  `P25VoiceDecode` / `P25VoiceTest` / `CliApp` / `AppBootstrap` / `MainWindow`;
  `main.cpp` ~2k leftovers + entry. Corpus: `src/tools/p25_orchestration_sources.py`
- **Not proven:** live CADENCE re-prove (T-0010); leftover helpers still in `main.cpp`

---

## BN-0027 — DEC-0038 streaming sticky Gardner (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_streaming_cqpsk_lock_create.py` PASS
- **Voicetest 060036 TG 10301 skip=261000 center=421.96375:**
  - Block unset: `PASS_CONTINUOUS duty=0.705`
  - Stream env=1 sticky Gardner: `PASS_PARTIAL duty=0.23`
  - Stream + discrete lock create (rejected): duty **0.12**
- **Not shipped:** default-on streaming (still ≪0.65)

---

## BN-0026 — DEC-0037 restore clear hold, no purge (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_rolling_hold_no_purge.py` PASS
- **Evidence:** 100909 chirp regression (duty 0.40) vs 095846 duty 0.947
- **Not proven:** live CADENCE after GUI reopen

---

## BN-0025 — DEC-0036 no rolling hold when unqueued (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_rolling_no_hold_unqueued.py` PASS
- **Evidence:** 095846 start TG 30302 cursor hold after targetVcw=14;
  later TG 10301 max duty 0.947
- **Not proven:** live start-unknown follow after GUI reopen

---

## BN-0024 — DEC-0035 live eye-lost uses replay CQPSK caps (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_live_eyelost_replay_caps.py` PASS
- **Evidence:**
  - Live 094846: drop A 60/62; max duty 0.338 (DEC-0034 exe)
  - Same IQ voicetest TG 30302: duty 0.43 targetVcw=652
  - Live/replay split = hot cand 8 vs 16 after speak
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0023 — DEC-0034 keep block CQPSK hint after emit (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_post_emit_keep_block_cqpsk_hint.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS (186 assertions / 49 cases)
- **Voicetest 060036 TG 10301 skip=261000 center=421.96375:**
  `PASS_CONTINUOUS_AUDIO duty=0.705` (held)
- **Evidence:** 092250 post-emit `clearBlockCqpskHint` cliff; companion-only
  gated to streaming
- **Not proven:** live CADENCE on new exe (T-0010). Default-on streaming still off.

---

## BN-0022 — DEC-0033 sticky HDQPSK / persistent framer (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_streaming_framer_commit.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS (186 assertions / 49 cases)
- **Voicetest 060036 TG 10301 skip=261000 center=421.96375:**
  - Block (env unset): `PASS_CONTINUOUS_AUDIO duty=0.705`
  - Stream `SDR_TOWN_P25_STREAMING_DDC=1`: `PASS_PARTIAL_AUDIO drop=D duty=0.25`
    (improved vs DEC-0014/0018 ~0.09–0.16 class; still ≪0.65)
- **Evidence:** 083254 extract cliff; framer Cold-gated + companion sticky
- **Not proven:** env=1 duty≥0.65; 105622 (IQ absent); live CADENCE (T-0010).
  Default-on still off (DEC-0014).

---

## BN-0021 — DEC-0032 post-emit sustain before catch-up (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_post_emit_sustain_before_catchup.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS
- **Voicetest:** 060036 TG 10301 skip≈261000: `PASS_CONTINUOUS_AUDIO duty=0.705` (held)
- **Evidence:** 081701 post-emit fresh=120 ms → no voice sync hang
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0020 — DEC-0031 backlog catch-up + once-clear continuation (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_backlog_catchup_before_speaker_sustain.py` PASS;
  `sdr_town_tests "[p25][follow]"` PASS
- **Voicetest:** 062006 TG 30003 slot1 skip=0: duty **0.46** (unchanged; DEC-0012
  companion-louder holes). 060036 TG 10301 skip≈261000: duty **0.705** (held).
- **Evidence:** planner ignored backlogCatchUp after speak; security
  requireFedAudio chicken-egg vs dual-slot mute
- **Not proven:** live CADENCE after GUI reopen (T-0010); soft PostEmitMixedMacDead

---

## BN-0019 — DEC-0030 active rolling 4s clamp (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `verify_p25_phase2_active_rolling_4s_clamp.py` PASS
- **Voicetest:** 060036 TG 10301 skip≈261000 center=421.96375:
  `PASS_CONTINUOUS_AUDIO duty=0.705` (proves live starve, not RF)
- **Evidence:** live rolling capped 4194304 after emit; file continuous
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0018 — DEC-0029 clear-trusted hold + structure cold-exit (2026-09-09)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS; `sdr_town_tests "[p25][follow]"` 49 cases / 186 assertions;
  `verify_p25_phase2_clear_trusted_hold_and_structure_cold_exit.py` PASS;
  `verify_p25_phase2_no_post_emit_cold_escalate.py` PASS
- **Voicetest:** pending (re-run 105622 skip=97334 after GUI live prove)
- **Evidence:** 053448 quiet-return +5s after clear emit; structureNoVcw ~484 ms
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0017 — DEC-0028 no post-emit cold escalate (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_no_post_emit_cold_escalate.py` PASS
- **Voicetest:** 105622 skip=97334: duty **0.645** (unchanged; PASS_PARTIAL drop=D)
- **Evidence:** 115603 first emit 0.553 then dsp 470–605 ms; emptyStreakReacq removed
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0016 — DEC-0027 emptyEye-only cold escalate (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_empty_eye_only_cold_escalate.py` PASS;
  `verify_p25_phase2_opposite_slot_no_cold_escalate.py` PASS
- **Voicetest:**
  - 105622 skip=97334: duty **0.645** (unchanged; PASS_PARTIAL drop=D)
  - 112922 TG 30302 slot 0 skip=164000 center=421.21375: `PASS_CONTINUOUS duty=0.735`
- **Evidence:** 112922 structureNoVcw dsp med ~462 ms; CADENCE peak 0.639 drop D;
  file continuous proves live starvation not RF
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0015 — DEC-0026 opposite-slot no cold escalate (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_opposite_slot_no_cold_escalate.py` PASS
- **Voicetest:** 105622 skip=97334: duty **0.645** (unchanged)
- **Evidence:** 110146 wrong-slot dsp p90 ~434 ms; 20202 file 0.85 / live 5 ok
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0014 — DEC-0025 Clear→Encrypted MAC bar (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_clear_to_encrypted_mac_bar.py` PASS
- **Voicetest:**
  - 105622 skip=97334: duty **0.645** (unchanged)
  - 103955 RID 0x1F83FF skip=98700: duty **0.46** PARTIAL (RF-limited)
  - 103955 RID 0x1F95EB skip=119800: duty **0.83** CONTINUOUS
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0013 — DEC-0024 backlog catch-up overlap 280 ms (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_backlog_catchup_overlap.py` PASS
- **Voicetest:** 105622 skip=97334: duty **0.645** (unchanged)
- **Evidence:** 101644 first-call `context=81920` spiral; late 10330 280 ms
- **Not proven:** live CADENCE after GUI reopen (T-0010)

---

## BN-0012 — DEC-0023 rolling protect 280 ms (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_rolling_protect_overlap.py` PASS
- **Voicetest:**
  - 105622 skip=97334: duty **0.645** (unchanged)
  - 095936 TG 30302 slot 0 skip=11000 center=421.21375:
    `PASS_CONTINUOUS_AUDIO duty=0.685`
- **Not proven:** live CADENCE after GUI reopen on new desktop exe (T-0010)

---

## BN-0011 — Live speed trials rejected; hard hint stop only (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile after reverts
- **Voicetest (streaming DDC unset, block 80+280):**
  - 105622 skip=97334: duty **0.645**, wall **~17 s** / 8 s span
- **Rejected (duty collapse):**
  - DEC-0020 80/0 after emit: wall 2.8 s, 105622 duty **0.055** drop=A
  - Hot cand=3 after speak (live proxy): wall ~7 s, duty **0.055** drop=A
  - Streaming env=1 @ 80 ms: duty ~0.01; @ 160 ms (DEC-0022): **0.125**
- **Kept:** DEC-0019 hard CQPSK hint early-stop; live hot cand=**8**
- **Not proven:** live CADENCE on desktop HEAD (operator still on 0.2.51)

---

## BN-0010 — DEC-0019 hard CQPSK hint stop (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS; `verify_p25_phase2_block_cqpsk_hint_early_stop.py` PASS
- **Voicetest (streaming DDC unset):**
  - Soft early-stop trial: 105622 duty=0.305 / 041716=0.5 — **rejected**
  - Hard-only (cand still 8/16): 105622 **0.645**, 073304 **0.795**, 041716
    **0.87**, 060221 peak **0.922** (parity with 0.2.51)
  - Later: cand=3 after speak **rejected** (see BN-0011)
- **Not proven:** live CADENCE drop D after GUI reopen (T-0010)

---

## BN-0009 — 053241 context-only DEC-0012 trial rejected (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Trial:** PostEmit skip only when `codewordEndsBeforeFresh` (fresh selected
  still feeds on companion-louder mixed MAC-dead).
- **Voicetest (file `--center`, streaming DDC unset):**
  - 053241 TG 30003 slot 0 skip=0 center=421.96375: duty=0.715 continuous;
    companion-louder `fed>0` returned.
  - 105622 TG 30003 slot 0 skip=97334: duty=**0.62** (two reruns; was 0.645).
  - 073304 TG 10330 slot 1 skip=107597: duty=**0.72** (was 0.795).
  - 041716 TG 10330 slot 1 skip=32111: duty=0.875 but **0**
    `unknown-waiting-clear`; 8 companion-louder fresh emits (isolation regress).
- **Action:** reverted to hop-wide DEC-0012 (v0.2.51). No release bump.

---

## BN-0008 — DEC-0012 companion-louder mixed MAC-dead skip (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile; dual-slot / session-release / sticky-ESS string
  verifiers PASS
- **Voicetest (file `--center`, streaming DDC unset):**
  - 041716 TG 10330 slot 1 skip=32111 center=421.21375:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.87`. Companion-louder mixed
    `p2mac=0` hops `unknown-waiting-clear` (seq=134 class).
  - 073304 TG 10330 slot 1 skip=107597 center=420.975:
    `PASS_CONTINUOUS_AUDIO duty=0.795`
  - 105622 TG 30003 slot 0 skip=97334 center=421.725:
    `PASS_PARTIAL_AUDIO drop=D duty=0.645` (was 0.685). Slot 1 duty=0.09.
- **Not proven:** live CADENCE on a new GUI follow (T-0010 drop D).

---

## BN-0007 — DEC-0017 revert + DEC-0018 streaming lattice (2026-09-08)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile
- **Voicetest 105622 TG 30003 slot 0 skip=97334 center=421.725:**
  - Default block 80+280: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685` (DEC-0017 360/0 had 0.35)
  - `SDR_TOWN_P25_STREAMING_DDC=1`: duty 0.16 (80 ms), 0.09 (hopms=160), 0.045 (hopms=360)
- **Not proven:** live CADENCE vs 123525 duty 0.34. Streaming DDC stays opt-in.

---

## BN-0006 — DEC-0016 voice-park follow LO (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile; `sdr_town_tests.exe "[tune]"` 18/5; one-RTL verifier PASS
- **Voicetest:** 105622 TG 30003 slot 0 skip=97334 `PASS_CONTINUOUS_AUDIO duty=0.685`
- **Not proven:** live listen on 115315-style 1.5 MHz grant (T-0010). File IQ was
  recorded at 420.97773 so replay cannot un-do the 997 kHz edge.

## BN-0005 — DEC-0015 SDRTrunk tuner center (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town sdr_town_tests`
- **Result:** PASS compile (`main.cpp` rebuilt); `sdr_town_tests.exe "[tune][dec0015]"` 14/4; one-RTL / force-retune / inband string verifiers PASS
- **Voicetest (file `--center` geometry, not live LO):**
  - 105622 TG 30003 slot 0 skip=97334: `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - 073304 TG 10330 slot 1 skip=107597 center=420.975: `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`
- **Not proven:** live waterfall on a follow (close/reopen `build\bin\Release\SDR_Town.exe`). Expect log `P25 tuner center aligned (SDRTrunk CenterFrequencyCalculator)` and voice ~11 kHz right of center, not 250 kHz off.

## BN-0004 — DEC-0014 CADENCE + 80 ms stream slice; default DDC rejected (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS compile; streaming DSP verifier PASS; `[drop]` 14/8
- **Voicetest:**
  - Default-on streaming DDC: 105622 TG 30003 slot 0 skip=97334
    `PASS_PARTIAL_AUDIO drop=A duty=0.095` — reverted
  - After revert: 105622 `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - 073304 TG 10330 slot 1 skip=107597 center=420.975:
    `PASS_CONTINUOUS_AUDIO duty=0.76 timelineOk`
  - 095450 TG 30013 slot 1 skip=3741: `PASS_PARTIAL_AUDIO duty=0.625`
    `oppAmbe=759/920 concealmentOk=no`; companion slot 0 duty=0.58
- **Not proven:** live CADENCE listen on 095450-style dual-TG (T-0010)

## BN-0003 — DEC-0013 lattice overlap de-dupe (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target SDR_Town`
- **Result:** PASS
  - Verifiers: fresh/context, overlap de-dupe, dual-slot garble
  - Voicetest 105622 TG 30003 slot 0 skip=97334 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.685`
  - Voicetest 073304 TG 10330 slot 1 skip=107597 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.76 timelineOk`
- **Not proven:** live CADENCE after this binary (T-0010). Mixed MAC-dead
  garble on 073304 first 30302 / last dual-TG concealment (T-0004).

## BN-0002 — DEC-0008/0009 Phase 2 extract (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` via VS 2022 MSBuild 17.14.40, config Release
- **Command:** `cmake --build build --config Release --target sdr_town_tests SDR_Town`
- **Result:** PASS
  - `sdr_town_tests.exe "[drop]"` — 14 assertions / 8 cases
  - Verifiers: dual-slot garble, block-channelize continuity, fresh/context
    gate, overlap decode window, speaker sustain overlap
  - Voicetest 105622 TG 30003 slot 0 skip=97334 8000 ms:
    `PASS_CONTINUOUS_AUDIO drop=ok duty=0.735`
  - Same IQ slot=1: `PASS_PARTIAL_AUDIO duty=0.11` (isolation)
- **Not proven:** live CADENCE on 161748 after this binary (T-0010)

## BN-0001 — Method desk + drop classifier (2026-09-07)

- **Host:** Windows 10.0.22631 x64
- **Compiler:** MSVC `cl` 19.44.35227 for x64 (VS 2022 Community), SDK 10.0.26100.0
- **CMake:** Visual Studio 17 2022 / x64, config Release, `BUILD_TESTS=ON`, `SDR_TOWN_ENABLE_MBELIB=ON`
- **Command:** `cmake --build build --config Release --target sdr_town_tests SDR_Town`
- **Result:** PASS
  - `sdr_town_tests.exe "[drop]"` — 14 assertions / 8 cases
  - `sdr_town_tests.exe` — 10166 assertions / 206 cases
  - `build/bin/Release/SDR_Town.exe` linked (main.cpp compiled with `P25AudioDropClass`)
- **Not proven:** live or IQ `drop=` on a clear Phase 2 call (ISS-0001 / ISS-0007)
