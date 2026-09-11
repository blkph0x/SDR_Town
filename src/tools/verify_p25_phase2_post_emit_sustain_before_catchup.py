#!/usr/bin/env python3
"""Guard: DEC-0032 speaker-sustain before backlogCatchUp; post-emit empty-eye soft rehunt."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
hdr = (root / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="replace")
rx = (root / "include" / "Receiver.h").read_text(encoding="utf-8", errors="replace")

if "DEC-0032 / capture 20260909_081701" not in main:
    raise SystemExit("DEC-0032 regression failed: missing planner comment")

fn = "p25Phase2PlanVoiceDecodeChunk"
idx = main.find(fn + "(")
if idx < 0:
    idx = main.find(fn)
# Prefer definition body (has '{') over a header prototype.
def_idx = main.find(fn + "(")
while def_idx >= 0:
    brace = main.find("{", def_idx)
    semi = main.find(";", def_idx)
    if brace >= 0 and (semi < 0 or brace < semi):
        idx = def_idx
        break
    def_idx = main.find(fn + "(", def_idx + 1)
body = main[idx : idx + 4500]
sustain = body.find("kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds")
# First backlogCatchUp *return* after sustain (not the streaming branch).
catch_marker = "plan.maxChunkSeconds = kP25Phase2VoiceDecodeBacklogCatchUpChunkSeconds"
# Find sustain return region then catch-up after it in the non-streaming path.
# Speaker-sustain must appear before the post-sustain backlogCatchUp assignment.
if sustain < 0:
    raise SystemExit("DEC-0032 regression failed: speaker-sustain constant missing")
catch_after = body.find(catch_marker, sustain)
if catch_after < 0:
    raise SystemExit("DEC-0032 regression failed: backlog catch-up after sustain missing")
# Banned: backlogCatchUp block that precedes speaker-sustain constants.
pre = body[:sustain]
if "if (backlogCatchUp &&" in pre and "BacklogCatchUpChunkSeconds" in pre:
    raise SystemExit(
        "DEC-0032 regression failed: backlogCatchUp must not override speaker-sustain"
    )

if "p25Phase2PostEmitEmptyEyeWindows" not in main or "p25Phase2PostEmitEmptyEyeWindows" not in rx:
    raise SystemExit("DEC-0032 regression failed: post-emit empty-eye counter missing")
if "clearBlockCqpskHint" not in main or "clearBlockCqpskHint" not in hdr:
    raise SystemExit("DEC-0032 regression failed: clearBlockCqpskHint missing")
# Must not raise MaskEpochRepair on every post-emit empty eye anymore.
bad = (
    "hadSuccessfulEmit &&\n"
    "                   out.phase2Bursts == 0"
)
# Ensure the empty-eye branch does not set MaskEpochRepairHoldWindows.
region_start = main.find("p25Phase2PostEmitEmptyEyeWindows")
if region_start < 0:
    raise SystemExit("DEC-0032 regression failed: empty-eye handler missing")
region = main[region_start : region_start + 800]
if "MaskEpochRepairHoldWindows" in region:
    raise SystemExit(
        "DEC-0032 regression failed: post-emit empty eye must not set MaskEpochRepair"
    )
# once-clear continuation from DEC-0031 must remain
if "onceClearCall" not in main or "requireFedAudio=*/!onceClearCall" not in main:
    raise SystemExit("DEC-0032 regression failed: DEC-0031 once-clear continuation missing")

print("DEC-0032 post-emit sustain + empty-eye soft rehunt regression: PASS")
