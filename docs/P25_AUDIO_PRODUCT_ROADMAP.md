# P25 audio product roadmap

Target end-state (user requirements, 2026-08):

## 1. Auto priority output
- Auto mode scores inbound Phase-2 (and later Phase-1) calls by activity.
- Most-active talkgroup/slot is promoted to a **priority audio path** above the main monitor output.
- Encrypted calls never open the speaker (fail-closed).

## 2. User priority controls
- UI to rank talkgroups and TDMA slots (sticky priority list).
- Higher priority grants preempt lower ones when active (with configurable hold/grace).
- Manual lock on a TG/slot optional (“hold this call”).

## 3. Multi-source recording
- Option to **record every followed talkgroup and slot** to separate files (or multiplexed with TG/slot metadata).
- Independent of which stream is on the speaker.
- Requires per-slot/per-TG decoder instances (sdrtrunk-style dual AudioModule model), not a single selected speaker path only.

## Current foundation (already in tree)
- Grant follow + independent traffic source (one-RTL / low-IF).
- Hard single-slot feed isolation (only `grantSlotKnown` matching followed slot).
- Security gate: grant selects, traffic PTT/ESS proves clear.
- Streaming sustain after first emit (short hops; no dual-call mix).

## Implementation order (when scheduled)
1. Continuous clear audio on selected TG/slot (streaming quality) — **open (ISS-0001)**. Not done: README ~50% clear; baseline 20260810 continuity PARTIAL. Gate is `PASS_CONTINUOUS_AUDIO` / CADENCE `dutySec` near 1.0 (SoT L3). Dual-slot MAC-dead mute is isolation, not continuity.
2. Dual-slot parallel decode (both slots decoded; one selected for speaker) — **done** (`p25AmbeVoiceDecoderOpposite` / `pendingAudioOpposite` + observe).
3. Priority list UI + auto most-active promotion — **foundation live** (`userPriority` / `activityScore` + preempt + Set Priority UI).
4. Per-TG/slot WAV writers — **live** (`oppwav=` / companion CLI WAV; selected+companion when follow records).
5. Companion→speaker promote on selection change — **live** (`p25Phase2PromoteCompanionModules` swap AMBE+pending+resampler; no PCM mix).

Do not weaken slot/TG isolation when adding priority or multi-record.

## Recoverable baseline (2026-08-10)

Best clear continuous build so far: **`p25-clear-continuous-20260810`**.

- Docs: `docs/P25_BASELINE_CLEAR_CONTINUOUS_20260810.md`
- Binary: `build/baselines/SDR_Town_p25_clear_continuous_20260810.exe`
- Live proof log: capture `20260810_134531` (TG 10330 → 10120, slot 1; no cross-TG emit; `wrongSlot` not fed)
- Code id: `SDR_TOWN_P25_AUDIO_BASELINE` in `src/main.cpp`
