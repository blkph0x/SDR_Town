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
1. Continuous clear audio on selected TG/slot (streaming quality) — **in progress**.
2. Dual-slot parallel decode (both slots decoded; one selected for speaker).
3. Priority list UI + auto most-active promotion.
4. Per-TG/slot WAV/JSONL capture writers.

Do not weaken slot/TG isolation when adding priority or multi-record.
