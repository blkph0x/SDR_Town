# P25 traffic source watchdog/stale/slot fix

Fixes field log pattern where one-RTL traffic source stayed on a dead voice channel for ~30s with p2bursts=0, and a stale older same-wideband traffic decoder published diagnostics/audio after release.

Changes:
- Phase 2 no-VCW watchdog now treats Idle/WaitingForClearGrant as no-acquisition states and returns faster.
- Rolling IQ decode skips tiny fresh fragments (eg iq=256) without advancing cursor.
- Final generation/active check before publishing P25 voice diagnostics/audio drops stale traffic decoder snapshots.
- Slot auto-probe now targets the active independent traffic receiver instead of always receiver[0].
- Full-lock windows with targetVcw=0/oppVcw>0 can decode using the inverted logical slot immediately for that window.
