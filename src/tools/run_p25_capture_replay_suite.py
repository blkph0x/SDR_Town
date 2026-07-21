#!/usr/bin/env python3
"""Run bounded SDR Town CLI replay checks for a P25 capture folder."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import p25_capture_audit


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


def classify_output(output: str, timed_out: bool) -> str:
    if timed_out:
        return "TIMEOUT"
    if "PASS_CONTINUOUS_AUDIO" in output:
        return "PASS_CONTINUOUS_AUDIO"
    if "PASS_CLEAR_AUDIO" in output:
        return "PASS_CLEAR_AUDIO"
    if "PASS_PARTIAL_AUDIO" in output:
        return "PASS_PARTIAL_AUDIO"
    if "PASS_ENCRYPTED_GATED" in output:
        return "PASS_ENCRYPTED_GATED"
    if "FAIL_RAW_AUDIO_GATED" in output:
        return "FAIL_RAW_AUDIO_GATED"
    if "FAIL_NO_AUDIO" in output:
        if re.search(r"\bp2bursts=0\b", output) and not re.search(r"\bp2bursts=[1-9]\d*\b", output):
            return "FAIL_NO_TRAFFIC_BURSTS"
        target_values = [int(x) for x in re.findall(r"\btargetVcw=(\d+)\b", output)]
        opp_values = [int(x) for x in re.findall(r"\boppVcw=(\d+)\b", output)]
        ambe_probe = [tuple(map(int, m)) for m in re.findall(r"\bambeProbe=(\d+)/(\d+)\b", output)]
        target_vcw = max(target_values) if target_values else 0
        opp_vcw = max(opp_values) if opp_values else 0
        probe_accepted = max((a for a, _ in ambe_probe), default=0)
        probe_attempts = max((b for _, b in ambe_probe), default=0)
        if target_vcw > 0 and "p2ess=unknown" in output and re.search(r"\bp2mac=0/\d+\b", output):
            if (
                re.search(r"\baudioSamples=0\b", output)
                and re.search(r"\bspeakerSamples=0\b", output)
                and (
                    "gate=unknown-raw-queued-waiting-clear" in output
                    or "gate=unknown-waiting-clear" in output
                )
            ):
                return "PASS_UNKNOWN_GATED"
            return "FAIL_SECURITY_UNKNOWN_TARGET_VOICE_PROBED" if probe_attempts > 0 else "FAIL_SECURITY_UNKNOWN_TARGET_VOICE"
        if target_vcw == 0 and opp_vcw > 0:
            return "FAIL_OPPOSITE_SLOT_ONLY"
        if probe_accepted > 0:
            return "FAIL_AMBE_PROBE_ACCEPTED_BUT_GATED"
        return "FAIL_NO_AUDIO"
    if "NO_FOLLOW_CANDIDATE" in output:
        return "NO_FOLLOW_CANDIDATE"
    if "P25 replay load failed" in output or "P25 voicetest load failed" in output:
        return "LOAD_FAILED"
    if "P25 voicetest voice" in output or "P25 followtest control" in output:
        return "RAN_NO_PASS"
    return "NO_TEST_OUTPUT"


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
        status = classify_output(result["output"], result["timed_out"])
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
