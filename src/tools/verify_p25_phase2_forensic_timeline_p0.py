#!/usr/bin/env python3
"""Regression guard for forensic-audit P0 timeline-safety fixes."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="ignore")
decoder_h = (root / ".." / "include" / "P25LiveDecoder.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)
decoder_cpp = (root / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
session_h = (root / ".." / "include" / "P25ReceiverSession.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)

worker_fn = main.split("bool p25VoiceWorkerCanAcceptJob()", 1)[1].split("P25VoiceWorkerQueueSnapshot", 1)[0]
publish_wait_section = main.split("// Block until the GUI DSP worker drains completed voice", 1)[1].split(
    "p25VoiceWorkerBusy.store", 1
)[0]
required = {
    "lossless publish queue": "pendingVoicePublishResults" in main,
    "publish returns terminal outcome": "P25VoicePublishOutcome publishP25VoiceDecodeResult(" in main,
    "publication lock miss counter": "p25VoicePublicationLockMisses" in main,
    "no completed-result backlog eviction": "Never evict decoded PCM after state mutation" in publish_wait_section
    and "p25VoiceCompletedResults.push_back(std::move(result));" in publish_wait_section
    and "pop_" not in publish_wait_section
    and "erase" not in publish_wait_section
    and "p25VoiceDroppedResults.fetch_add" not in publish_wait_section,
    "worker blocks on full result backlog": "p25VoicePendingPublishDepth.load" in worker_fn,
    "stream dibit on codeword": "streamDibitKnown" in decoder_h and "codeword.streamDibit = streamDibit" in decoder_cpp,
    "frame key uses stream dibit": "key.streamDibitKnown = cw.streamDibitKnown" in main,
    "first-frame erasure timeline": "Every accepted AMBE feed position must occupy one 20 ms slot" in main,
    "metadata excluded from audio key": "NAC/WACN/system/source/grant epoch are late-arriving metadata" in session_h,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P0 timeline regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P0 timeline regression: PASS")
