#!/usr/bin/env python3
"""Guard: DEC-0028 no post-emit emptyStreak cold CQPSK escalate."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
marker = "DEC-0028: never cold-escalate after the call has"
if marker not in main:
    raise SystemExit("DEC-0028 regression failed: missing no-post-emit-cold comment")
# DEC-0035/0039 comments grew past the old 1800-char window; keep the whole
# post-DEC-0028 hot speakerLiveHot block through the hotCands assignment.
region = main.split("DEC-0028:", 1)[1][:6000]
speaker_hot = (
    region.split("if (speakerLiveHot)", 1)[1][:4500]
    if "if (speakerLiveHot)" in region
    else ""
)
checks = {
    "cites 115603": "20260908_115603" in main,
    "no emptyStreakReacq in hot block": "emptyStreakReacq" not in region,
    "no cold budget assign in hot speaker path": (
        "kP25VoiceWorkerColdRealtimeBudgetMs" not in region.split("speakerLiveHot")[0]
        if "speakerLiveHot" in region
        else False
    ),
    "speakerLiveHot uses hot cand=8": (
        "speakerLiveHot" in region
        and "hotCands = kP25VoiceWorkerHotMaxCqpskCandidates" in speaker_hot
    ),
    "soft mask rehunt still present": (
        "p25Phase2StructureNoTargetVoiceWindows" in main
        and "invalidatePhase2StickyMaskEpoch" in main
    ),
}
# Stronger: after DEC-0028 marker through end of hotPhase2TrafficJob budget set,
# cold realtime budget must not be assigned.
hot_tail = region[:2500]
if "hotBudgetMs = kP25VoiceWorkerColdRealtimeBudgetMs" in hot_tail:
    checks["no cold hotBudgetMs assign"] = False
else:
    checks["no cold hotBudgetMs assign"] = True

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0028 regression failed: " + ", ".join(failed))
print("DEC-0028 no post-emit cold-escalate regression: PASS")
