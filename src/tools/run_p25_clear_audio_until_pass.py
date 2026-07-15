#!/usr/bin/env python3
"""Repeat live P25 clear-audio diagnostics until a useful pass is captured."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path


PASS_SCORES = {
    "PASS_CONTINUOUS_AUDIO": 100,
    "PASS_CLEAR_AUDIO": 90,
    "PASS_PARTIAL_AUDIO": 60,
    "PASS_ENCRYPTED_GATED": 30,
    "FAIL_RAW_AUDIO_GATED": 15,
    "FAIL_NO_AUDIO": 10,
    "NO_REPLAY_RESULTS": 0,
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def safe_name(text: str) -> str:
    return "".join(ch if ch.isalnum() or ch in ("-", "_", ".") else "_" for ch in text)[:120]


def load_json(path: Path) -> dict:
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8", errors="replace"))


def score_summary(summary: dict) -> tuple[int, str]:
    replay = summary.get("replay") or {}
    waitgrant = summary.get("waitgrant") or {}
    status = str(replay.get("best_status") or "NO_REPLAY_RESULTS")
    score = PASS_SCORES.get(status, 0)
    wav_seconds = float(waitgrant.get("wav_seconds") or 0.0)
    if wav_seconds >= 0.5:
        score += 20
    elif wav_seconds > 0.0:
        score += 5
    if waitgrant.get("audio_opened"):
        score += 10
    if waitgrant.get("grant_lines", 0) > 0:
        score += 2
    return score, status


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", type=float, action="append", required=True, help="Control channel MHz. Repeat for multiple CCs.")
    parser.add_argument("--attempts", type=int, default=4, help="Total attempts across the CC list.")
    parser.add_argument("--seconds", type=float, default=120.0, help="Seconds per live waitgrant attempt.")
    parser.add_argument("--record-seconds", type=float, default=3.0)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--tg", type=int)
    parser.add_argument("--repo", type=Path, default=repo_root())
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--replay-timeout", type=float, default=60.0)
    parser.add_argument("--max-replay-grants", type=int, default=2)
    parser.add_argument("--accept-partial", action="store_true", help="Treat PASS_PARTIAL_AUDIO as a successful stop.")
    args = parser.parse_args(argv)

    repo = args.repo.resolve()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = (args.out_dir or (repo / "build" / "p25_clear_audio_until_pass" / timestamp)).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    attempts: list[dict] = []
    best_summary: dict | None = None
    best_score = -1
    live_diag = repo / "src" / "tools" / "run_p25_live_clear_audio_diag.py"

    for index in range(max(1, args.attempts)):
        cc = args.cc[index % len(args.cc)]
        attempt_dir = out_dir / f"attempt_{index + 1:02d}_{safe_name(f'{cc:.5f}MHz')}"
        cmd = [
            sys.executable,
            str(live_diag),
            "--cc",
            f"{cc:.5f}",
            "--device",
            str(args.device),
            "--seconds",
            str(args.seconds),
            "--record-seconds",
            str(args.record_seconds),
            "--repo",
            str(repo),
            "--out-dir",
            str(attempt_dir),
            "--replay-timeout",
            str(args.replay_timeout),
            "--max-replay-grants",
            str(args.max_replay_grants),
        ]
        if args.tg:
            cmd.extend(["--tg", str(args.tg)])
        proc = subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=max(args.seconds + args.record_seconds + args.replay_timeout * 10.0 + 180.0, 240.0),
            check=False,
        )
        (attempt_dir / "runner_stdout.txt").write_text(proc.stdout or "", encoding="utf-8", errors="replace")
        summary = load_json(attempt_dir / "clear_audio_diag_summary.json")
        score, status = score_summary(summary)
        row = {
            "attempt": index + 1,
            "cc_mhz": cc,
            "returncode": proc.returncode,
            "score": score,
            "status": status,
            "summary_path": str(attempt_dir / "clear_audio_diag_summary.json"),
            "out_dir": str(attempt_dir),
            "findings": summary.get("findings", []),
            "wav_seconds": (summary.get("waitgrant") or {}).get("wav_seconds", 0.0),
            "wav_path": (summary.get("waitgrant") or {}).get("wav_path"),
            "capture_dir": (summary.get("waitgrant") or {}).get("capture_dir"),
        }
        attempts.append(row)
        if score > best_score:
            best_score = score
            best_summary = summary

        loop_summary = {
            "out_dir": str(out_dir),
            "attempts": attempts,
            "best_score": best_score,
            "best_summary": best_summary,
        }
        (out_dir / "loop_summary.json").write_text(
            json.dumps(loop_summary, indent=2, sort_keys=True),
            encoding="utf-8",
        )
        print(json.dumps(row, indent=2, sort_keys=True))

        if status in {"PASS_CONTINUOUS_AUDIO", "PASS_CLEAR_AUDIO"}:
            print(f"clear-audio loop PASS on attempt {index + 1}: {status}")
            return 0
        if args.accept_partial and status == "PASS_PARTIAL_AUDIO":
            print(f"clear-audio loop PARTIAL PASS on attempt {index + 1}")
            return 0

    print(json.dumps(load_json(out_dir / "loop_summary.json"), indent=2, sort_keys=True))
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
