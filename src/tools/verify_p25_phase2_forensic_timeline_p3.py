#!/usr/bin/env python3
"""Regression guard for forensic re-audit P0 cursor, backpressure, burst-tail fixes."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="ignore")
session_h = (root / ".." / "include" / "P25ReceiverSession.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)

drain = main.split("auto drainP25VoiceResults = [&]() {", 1)[1].split("while (!stopDspWorker", 1)[0]
close_burst = main.split("static void p25Phase2CloseActiveVoiceBurst", 1)[1].split(
    "static void p25Phase2BeginVoiceBurst", 1
)[0]

worker_fn = main.split("bool p25VoiceWorkerCanAcceptJob()", 1)[1].split("P25VoiceWorkerQueueSnapshot", 1)[0]

required = {
    "publish outcome enum": "enum class P25VoicePublishOutcome" in main,
    "single cursor settlement after publish": "outcome == P25VoicePublishOutcome::Published" in drain,
    "no ingest-time cursor commit": "commitDecodeAbsolute(result.iqDecodeEndAbsolute)" not in drain.split(
        "pendingVoicePublishResults.push_back", 1
    )[0],
    "pending publish backpressure": "p25VoicePendingPublishDepth" in main,
    "worker counts pending publish depth": "p25VoicePendingPublishDepth.load" in worker_fn,
    "burst tail emits pcm silence": "p25Phase2InsertSequencerGapSilence" in close_burst,
    "stale clear uses captured session key": "p25Phase2ClearStaleResultSpeakerPending" in main,
    "result captures receiver session key": "result.receiverSessionKey = job.receiverSessionKey" in main,
    "stable burst identity fields": "activeBurstStableId" in session_h,
    "unorderable frame compare": "return 2" in session_h and "Unorderable" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P3 regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P3 regression: PASS")
