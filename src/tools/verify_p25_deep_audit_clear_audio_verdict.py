#!/usr/bin/env python3
from pathlib import Path

text = (Path(__file__).resolve().parents[1] / "tools" / "run_p25_capture_deep_audit.py").read_text(
    encoding="utf-8",
    errors="replace",
)

assert 'CLEAR_AUDIO_STATUSES = frozenset({"PASS_CONTINUOUS_AUDIO", "PASS_CLEAR_AUDIO"})' in text
assert 'clear_audio_proven = has_clear_audio_pass or has_stt_pass' in text
assert '"has_clear_audio_pass": has_clear_audio_pass' in text
assert 'cli_replay_plc_only_input_quality_rejected' in text
assert 'cli_replay_concealment_only_audio' in text
assert 'else:\n        clear_audio_proven = has_audio_pass' not in text

print("P25 deep-audit clear-audio verdict regression: PASS")
