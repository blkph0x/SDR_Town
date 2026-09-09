#!/usr/bin/env python3
"""Guard: DEC-0028 no post-emit emptyStreak cold CQPSK escalate."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
marker = "DEC-0028: never cold-escalate after the call has"
if marker not in main:
    raise SystemExit("DEC-0028 regression failed: missing no-post-emit-cold comment")
region = main.split("DEC-0028:", 1)[1][:1800]
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
        and "kP25VoiceWorkerHotMaxCqpskCandidates" in region
    ),
    "soft mask rehunt still present": (
        "p25Phase2StructureNoTargetVoiceWindows" in main
        and "invalidatePhase2StickyMaskEpoch" in main
    ),
}
# Stronger: after DEC-0028 marker through end of hotPhase2TrafficJob budget set,
# cold realtime budget must not be assigned.
hot_tail = region[:1200]
if "hotBudgetMs = kP25VoiceWorkerColdRealtimeBudgetMs" in hot_tail:
    checks["no cold hotBudgetMs assign"] = False
else:
    checks["no cold hotBudgetMs assign"] = True

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0028 regression failed: " + ", ".join(failed))
print("DEC-0028 no post-emit cold-escalate regression: PASS")
