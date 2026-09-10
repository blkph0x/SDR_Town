#!/usr/bin/env python3
"""Regression guard for forensic-audit P0 timeline-safety fixes."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import definition_body, orchestration_source_text
main = orchestration_source_text()
decoder_h = (root / "include" / "P25LiveDecoder.h").read_text(
    encoding="utf-8", errors="ignore"
)
decoder_cpp = (root / "src" / "P25LiveDecoder.cpp").read_text(
    encoding="utf-8", errors="ignore"
)
session_h = (root / "include" / "P25ReceiverSession.h").read_text(
    encoding="utf-8", errors="ignore"
)

worker_fn = definition_body(
    main,
    "bool MainWindow::p25VoiceWorkerCanAcceptJob()",
    ["MainWindow::P25VoiceWorkerQueueSnapshot MainWindow::p25VoiceWorkerQueueSnapshot"],
)
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
    # Comment wording drifted; lock the still-present 20 ms timeline + metadata-exclusion intent.
    "first-frame erasure timeline": (
        "assigns monotonic speech ordinals (one per 20 ms position)" in session_h
        or "every AMBE time slot" in main
    ),
    "metadata excluded from audio key": (
        "NAC/WACN/" in session_h
        and "site metadata" in session_h
        and "Source/RID can arrive late" in session_h
    ),
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P0 timeline regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P0 timeline regression: PASS")
