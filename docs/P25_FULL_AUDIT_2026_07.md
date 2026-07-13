# P25 Full Audit vs SDRTrunk — Where We Are (2026-07-12)

Scope: on-disk `maulaudio_pro` (SDR Town 0.2.23 + large uncommitted P25 work),
cross-checked against local `sdrtrunk-src` and live field logs under the repo root.

## Executive status

| Area | Status vs SDRTrunk | Notes |
|------|--------------------|-------|
| P25 Phase 1 CC (sync/NID/TSBK/PDU) | **Strong** | Live logs show nidLock, trusted TSBK/P1 PDU, grants resolved |
| Grant follow / retune / talkgroup table | **Strong** | Clear grants followed with correct freq/slot/mask params |
| One-RTL traffic source + IQ cursor | **Good (custom)** | Not identical to SDRTrunk multi-channel manager; workable |
| Phase 2 superframe lock | **Partial** | Can reach p2sf=12 on air; sometimes stays 0 under offset thrash |
| Phase 2 XOR mask generation | **Aligned** | LFSR seed/taps + 20-bit ISCH lead-in match ScramblingSequence |
| Phase 2 mask **phase** lock | **Was broken / fixed this pass** | Sticky lock without MAC/ESS/I-ISCH caused permanent wrong descramble |
| Phase 2 MAC/ACCH CRC | **Weak on air** | Field: `p2mac=0/12` while sf/mask high — deep ACCH still shallow on hot path |
| Phase 2 ESS / PTT session | **Partial** | Model matches P25P2AudioModule; needs successful ACCH first |
| Voice CW offsets 2/76/172/246 | **Aligned** | Dibit starts 1/38/86/123 after 20-dibit ISCH |
| AMBE → mbelib packing | **Aligned + variants** | Variant probe 0–3; fails when mask phase wrong |
| Audio security gate | **Strict (by design)** | Matches “queue until PTT/ESS”; clear grant may release |
| Follow hang / inactive TG | **Hardened** | Meaningful-VCW lifetime + RF carrier drop |
| Analog / multi-audio / spectrum | **Working** | Outside P25 critical path |
| DMR/NXDN/satellite/etc. | **Not started** | Roadmap only |

**Bottom line:** control-channel and grant follow are production-useful. **Clear Phase 2 voice is still not release-ready.** The dominant field failure is *not* “no grant” — it is **descramble epoch wrong or unproven**, so AMBE never yields PCM even when `p2vcw` is high.

---

## What you have already done (substantial)

### Architecture (SDRTrunk-inspired)

- Control analyzer (`P25Control`) → grant events → follow state machine → traffic processor / live decoder.
- Per-slot Phase 2 session state (ESS/MAC), session call ID, pending raw AMBE queue until security known (P25P2AudioModule model).
- Independent traffic decoder config: **6000 baud H-DQPSK**, LPF-only (no RRC) matching `P25P2DecoderHDQPSK` notes.
- Absolute dibit cursor + ring pre-roll for one-RTL follow (SDRTrunk-like continuous virtual source).

### Protocol details already matched

- Phase 2 timeslot: 40-bit ISCH + 320-bit payload (no Phase-1 status-symbol strip — previously a MAC killer).
- Scrambling: WACN/SYSID/NAC LFSR, 4320-bit superframe, segments start at bit 20 step 360.
- Voice4/2 bit offsets, DUID dibit positions, ESS fragment order comments, I-ISCH location → mask segment anchoring.
- Encryption fail-closed; encrypted grants skipped; mid-call ESS can mute.

### Ops / quality

- 130+ unit tests / 5k+ assertions green on last Release build.
- Large suite of `verify_p25_phase2_*.py` regression scripts.
- Validation JSONL path, CLI waitgrant, WAV capture of decoded audio.
- Release packaging through **0.2.23**.

---

## Smoking-gun field evidence

From `live_cli_420350_20260702_133139_current_grant_follow.log`:

1. Grant selected: **TG 30003, ENC=clear, Phase 2, slot=1, mask=known**.
2. On voice: `p2bursts=12 p2vcw=20 p2sf=12 p2mask=12` **but** `p2mac=0/12 p2ess=unknown decoded=0 audio=0`.
3. WAV saved with **samples=0**.
4. Stage text often **“waiting clear grant”** even though the grant was already clear — diagnostic mislabel.

Interpretation:

- Superframe sync and mask *application* counters can be high while the **mask phase** is wrong.
- Wrong phase ⇒ garbage ACCH ⇒ `p2mac=0` and garbage AMBE ⇒ `decoded=0`.
- Sticky phase that never re-hunts permanently mutes the call (unlike SDRTrunk’s continuous framer).

Other logs (`after_hardening`, `after_sustain_fix`) also show follow success with long “waiting clear grant” and zero audio — same class of failure, sometimes without ever reaching p2sf/p2mask lock (offset/demod thrash).

---

## Shortfalls vs SDRTrunk (priority ordered)

### P0 — Clear voice path

1. **Mask phase selection / sticky lock**  
   SDRTrunk’s scrambling segment is tied to superframe index from continuous framing.  
   We search 12 phases but previously:
   - early-exited cold hunt on raw `score >= 400` (voice/sync only),
   - sticky-locked from **voice codeword counts alone**,
   - never re-opened a sticky wrong phase.  
   **Fixed this pass** (see below).

2. **MAC/ACCH reliability on live CQPSK**  
   Even with correct phase, hot path uses shallow ACCH (`deepAcchSearch=false`). SDRTrunk recovers MAC heavily. Without MAC/ESS, unknown grants cannot open speaker; clear grants still need correct AMBE bits.

3. **Traffic demod continuity**  
   CC offset hunting (logs: ±7.5–15 kHz) and C4FM/CQPSK path flapping delay grants and starve voice windows. SDRTrunk keeps a dedicated demod per channel with continuous samples.

### P1 — Structure / maintainability

4. **Monolithic `main.cpp` P25 glue** (~5k+ uncommitted lines of gates) vs SDRTrunk’s TrafficChannelManager + per-timeslot AudioModule. Hard to reason about; easy to re-introduce mute bugs.
5. No typed SuperFrameFragment / Voice*Timeslot / P25P2AudioModule split (still one big decoder).
6. Diagnostics previously ranked “waiting clear grant” over mask/AMBE failures.

### P2 — Product completeness

7. Golden IQ replay fixtures for known-clear / encrypted / late-entry (partial offline tools; not release-gated).
8. Signed updater / Authenticode (planned).
9. Multi-SDR dedicated traffic channel like SDRTrunk (one-RTL shared ring is a permanent compromise).
10. Roadmap protocols (DMR, NXDN, weather, etc.).

---

## Fixes applied in this audit pass

### `P25LiveDecoder.cpp` / `.h`

1. **Cold mask-phase hunt**: score **12 slots**, early-exit **only** on MAC CRC or ESS (removed `score >= 400` early exit).
2. **Sticky lock criteria**: only MAC CRC, ESS, or **I-ISCH phase confirmation** — not bare VCW counts.
3. **Starve re-hunt**: if sticky phase produces voice/SF evidence with **zero MAC/ESS** for **3** annotate windows, clear sticky and re-run full 12-phase hunt.
4. Copy/move/reset track `m_phase2MaskPhaseStarveWindows`.

### `main.cpp`

5. **Diagnostics**: concrete failures (wrong slot, AMBE reject, mask-no-MAC, mask missing) rank **above** “waiting clear grant”.
6. Clear grant + VCW without usable AMBE reports **AMBE rejected / mask-no-MAC**, not “waiting clear grant”.

---

## What still must be true before claiming “SDRTrunk parity” on voice

1. Live clear call: `decoded>0`, non-empty WAV, `p2mac>0` or sustained clear-grant AMBE emit with low error.
2. Encrypted call: zero PCM.
3. Wrong slot: no cross-talk audio.
4. Return to CC after hangtime without multi-second stuck TG.
5. Unit tests still green; at least one golden IQ replay if available.

---

## Recommended next hardware test

```powershell
# After rebuild
.\build\bin\Release\SDR_Town.exe  # or CLI waitgrant on 420.35
# Watch for:
#   following TG ... enc=clear
#   p2sf / p2mask lock
#   p2mac > 0  OR  stage not stuck on waiting-clear with decoded>0
#   WAV samples > 0
```

Enable `SDR_TOWN_P25_VALIDATION_LOG=1` for JSONL AMBE/mask phase lines if still silent.

---

## Uncommitted work note

Working tree has large diffs on P25 path vs `71313ff` (v0.2.23). This audit treated **working tree** as truth. Recommend committing after green tests + one successful live clear call.
