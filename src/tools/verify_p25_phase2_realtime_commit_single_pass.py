#!/usr/bin/env python3
from pathlib import Path

decoder = (Path(__file__).resolve().parents[2] / "src" / "P25LiveDecoder.cpp").read_text(
    encoding="utf-8",
    errors="replace",
)
commit_region = decoder.split("const auto commitStarted = std::chrono::steady_clock::now();", 1)[1].split(
    "m_pendingFramerBursts.clear();",
    1,
)[0]

assert "allowCleanTailCommitRetry" in commit_region
assert "!(m_config.realtimeVoiceSearch && m_config.phase2CqpskTrafficDemod)" in commit_region
assert "allowCleanTailCommitRetry &&" in commit_region
assert "auto cleanTailCommitted = processHardDibitsInternal(\n                    best.dibits,\n                    true," in commit_region

print("P25 Phase 2 realtime commit single-pass regression: PASS")
