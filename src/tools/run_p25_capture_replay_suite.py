#!/usr/bin/env python3
"""Run bounded SDR Town CLI replay checks for a P25 capture folder."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import p25_capture_audit
from p25_voicetest_classify import classify_voicetest_output


def default_exe(repo: Path) -> Path:
    return repo / "build" / "bin" / "Release" / "SDR_Town.exe"


def safe_name(text: str) -> str:
    out = []
    for ch in text:
        out.append(ch if ch.isalnum() or ch in ("-", "_", ".") else "_")
    return "".join(out)[:120]


def run_cli_command(exe: Path, command: str, timeout_s: float, deep_trace: bool) -> dict:
    if " trace" not in command.lower().split("\n", 1)[0].lower():
        command = f"{command} trace"
    env = os.environ.copy()
    if deep_trace:
        env.setdefault("SDR_TOWN_P25_DEEP_TRACE", "1")
    try:
        proc = subprocess.run(
            [str(exe), "--cli", "--allow-multiple", "--cmd", command],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout_s,
            check=False,
            env=env,
        )
        return {
            "command": command,
            "returncode": proc.returncode,
            "timed_out": False,
            "output": proc.stdout,
        }
    except subprocess.TimeoutExpired as ex:
        return {
            "command": command,
            "returncode": None,
            "timed_out": True,
            "output": ex.stdout or "",
        }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", help="Capture directory. Defaults to latest SDR Town capture.")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2], help="Repository root.")
    parser.add_argument("--exe", type=Path, help="SDR_Town.exe path.")
    parser.add_argument("--latest", action="store_true", help="Use latest capture under the default capture root.")
    parser.add_argument("--timeout", type=float, default=25.0, help="Timeout per CLI command in seconds.")
    parser.add_argument("--max-commands", type=int, default=5, help="Maximum suggested replay commands to run.")
    parser.add_argument("--out-dir", type=Path, help="Transcript output directory.")
    parser.add_argument("--no-deep-trace", action="store_true", help="Do not set SDR_TOWN_P25_DEEP_TRACE=1 for replay commands.")
    args = parser.parse_args(argv)

    capture = p25_capture_audit.latest_capture_dir(p25_capture_audit.default_capture_root()) if args.latest or not args.capture else Path(args.capture)
    exe = args.exe or default_exe(args.repo)
    if not exe.exists():
        raise FileNotFoundError(f"SDR_Town executable not found: {exe}")

    report = p25_capture_audit.audit_capture(capture)
    commands = report["recommended_cli_commands"][: max(0, args.max_commands)]
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (args.repo / "build" / "p25_replay_suite" / f"{timestamp}_{capture.name}")
    out_dir.mkdir(parents=True, exist_ok=True)

    results = []
    for index, command in enumerate(commands, start=1):
        result = run_cli_command(exe, command, args.timeout, not args.no_deep_trace)
        status = classify_voicetest_output(result["output"] or "", result["timed_out"])
        transcript = out_dir / f"{index:02d}_{safe_name(status)}.txt"
        transcript.write_text(result["output"], encoding="utf-8", errors="replace")
        results.append(
            {
                "index": index,
                "status": status,
                "timed_out": result["timed_out"],
                "returncode": result["returncode"],
                "command": command,
                "transcript": str(transcript),
            }
        )

    suite = {
        "capture_dir": str(capture),
        "exe": str(exe),
        "audit_findings": report["findings"],
        "counts": report["counts"],
        "results": results,
    }
    summary_path = out_dir / "suite_summary.json"
    summary_path.write_text(json.dumps(suite, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(suite, indent=2, sort_keys=True))
    return 0 if all(r["status"] not in {"TIMEOUT", "LOAD_FAILED"} for r in results) else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
