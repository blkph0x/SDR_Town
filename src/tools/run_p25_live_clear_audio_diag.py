#!/usr/bin/env python3
"""Run a live P25 wait-grant/follow diagnostic and replay any saved capture.

The goal is to make the field test repeatable:
1. monitor a control channel,
2. follow a clear Phase 2 grant,
3. save decoded WAV + follow IQ,
4. replay the saved IQ through the continuous voice harness,
5. emit one JSON summary for bug reports.
"""

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


def default_repo() -> Path:
    return Path(__file__).resolve().parents[2]


def default_exe(repo: Path) -> Path:
    return repo / "build" / "bin" / "Release" / "SDR_Town.exe"


def safe_name(text: str) -> str:
    return "".join(ch if ch.isalnum() or ch in ("-", "_", ".") else "_" for ch in text)[:120]


def parse_waitgrant_output(output: str) -> dict:
    follow_records: list[dict] = []
    pending_record: dict | None = None
    lines = output.splitlines()
    for idx, line in enumerate(lines):
        wav_match = re.search(
            r"P25 waitgrant saved decoded WAV audio:\s*(?P<path>.+?)\s+samples=(?P<samples>\d+)\s+seconds=(?P<seconds>[0-9.]+)",
            line,
        )
        if wav_match:
            pending_record = {
                "wav_path": wav_match.group("path").strip(),
                "wav_samples": int(wav_match.group("samples")),
                "wav_seconds": float(wav_match.group("seconds")),
                "capture_dir": None,
            }
            follow_records.append(pending_record)
            continue
        if "P25 waitgrant saved follow IQ capture:" not in line:
            continue
        if idx + 1 < len(lines):
            candidate = lines[idx + 1].strip()
            if candidate:
                if pending_record is None or pending_record.get("capture_dir"):
                    pending_record = {
                        "wav_path": None,
                        "wav_samples": 0,
                        "wav_seconds": 0.0,
                        "capture_dir": candidate,
                    }
                    follow_records.append(pending_record)
                else:
                    pending_record["capture_dir"] = candidate

    reason_match = re.search(r"P25 waitgrant continuing scan after\s+([a-z0-9_]+)\s+on TG", output, re.IGNORECASE)
    audio_opened = (
        "P25 speaker-gated audio opened" in output or
        "P25 decoded audio opened" in output or
        "followCaptureReason=audio_opened" in output
    )
    grant_tgs = sorted({int(x) for x in re.findall(r"\bTG[ =](\d+)\b", output, re.IGNORECASE)})
    best_record = None
    if follow_records:
        best_record = max(
            follow_records,
            key=lambda row: (
                int(row.get("wav_samples") or 0),
                1 if row.get("capture_dir") else 0,
            ),
        )
        if int(best_record.get("wav_samples") or 0) <= 0:
            best_record = follow_records[-1]
    return {
        "wav_path": best_record.get("wav_path") if best_record else None,
        "wav_samples": int(best_record.get("wav_samples") or 0) if best_record else 0,
        "wav_seconds": float(best_record.get("wav_seconds") or 0.0) if best_record else 0.0,
        "capture_dir": best_record.get("capture_dir") if best_record else None,
        "follow_records": follow_records,
        "audio_opened": audio_opened,
        "last_retry_reason": reason_match.group(1).lower() if reason_match else None,
        "grant_talkgroups_seen": grant_tgs,
        "grant_lines": len(
            re.findall(
                r"\bInstruction: Group\b|\bTSBK: Group Grant\b|\bGroup voice channel grant\b",
                output,
            )
        ),
        "follow_lines": len(re.findall(r"\bFollowing Phase 2\b|\bAuto-following P25 TG\b", output)),
        "watchdog_lines": len(re.findall(r"\bwatchdog\b", output, re.IGNORECASE)),
        "encrypted_skips": len(re.findall(r"\bencrypted\b", output, re.IGNORECASE)),
    }


def run_waitgrant(
    exe: Path,
    *,
    cc_mhz: float,
    device: int,
    seconds: float,
    record_seconds: float,
    target_tg: int | None,
    transcript: Path,
    deep_trace: bool,
) -> dict:
    command = (
        f"p25 waitgrant {cc_mhz:.5f} dev={device} seconds={seconds:.1f} "
        f"follow record={record_seconds:.1f} wav"
    )
    if target_tg:
        command += f" tg={target_tg}"
    env = os.environ.copy()
    env.setdefault("SDR_TOWN_P25_VALIDATION_LOG", "1")
    env.setdefault("SDR_TOWN_P25_VALIDATION_REDACT", "1")
    env.setdefault("SDR_TOWN_P25_WAITGRANT_TRACE", "1")
    if deep_trace:
        env.setdefault("SDR_TOWN_P25_DEEP_TRACE", "1")
    proc = subprocess.run(
        [str(exe), "--cli", "--allow-multiple", "--cmd", command],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=max(seconds + record_seconds + 90.0, 120.0),
        check=False,
        env=env,
    )
    output = proc.stdout or ""
    transcript.write_text(output, encoding="utf-8", errors="replace")
    parsed = parse_waitgrant_output(output)
    parsed.update(
        {
            "command": command,
            "returncode": proc.returncode,
            "transcript": str(transcript),
        }
    )
    return parsed


def run_deep_replay(
    repo: Path,
    exe: Path,
    capture_dir: Path,
    out_dir: Path,
    timeout_s: float,
    max_grants: int,
) -> dict:
    replay_dir = out_dir / "replay"
    replay_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable,
        str(repo / "src" / "tools" / "run_p25_capture_deep_audit.py"),
        str(capture_dir),
        "--repo",
        str(repo),
        "--exe",
        str(exe),
        "--timeout",
        str(timeout_s),
        "--span-ms",
        "1200",
        "--step-ms",
        "300",
        "--max-grants",
        str(max_grants),
        "--window-ms",
        "360",
        "--hop-ms",
        "40",
        "--minframes",
        "2",
        "--minaudio",
        "0.05",
        "--out-dir",
        str(replay_dir),
    ]
    proc = subprocess.run(
        cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=max(timeout_s * max(1, max_grants) * 8.0, 120.0),
        check=False,
    )
    transcript = replay_dir / "deep_audit_stdout.txt"
    transcript.write_text(proc.stdout or "", encoding="utf-8", errors="replace")
    summary_path = replay_dir / "deep_audit_summary.json"
    summary: dict = {}
    if summary_path.is_file():
        summary = json.loads(summary_path.read_text(encoding="utf-8", errors="replace"))
    return {
        "command": " ".join(cmd),
        "returncode": proc.returncode,
        "stdout": str(transcript),
        "summary_path": str(summary_path) if summary_path.is_file() else None,
        "findings": summary.get("findings", []),
        "counts": summary.get("counts", {}),
        "best_status": best_replay_status(summary.get("sweep_results", [])),
        "sweep_results": summary.get("sweep_results", []),
    }


def best_replay_status(results: list[dict]) -> str:
    order = {
        "PASS_CONTINUOUS_AUDIO": 6,
        "PASS_CLEAR_AUDIO": 5,
        "PASS_PARTIAL_AUDIO": 4,
        "PASS_ENCRYPTED_GATED": 3,
        "FAIL_RAW_AUDIO_GATED": 2,
        "RAN_NO_PASS": 2,
        "FAIL_NO_AUDIO": 1,
    }
    best = "NO_REPLAY_RESULTS"
    best_score = -1
    for result in results:
        status = str(result.get("status") or "UNKNOWN")
        score = order.get(status, 0)
        if score > best_score:
            best = status
            best_score = score
    return best


def diagnose_summary(waitgrant: dict, replay: dict | None) -> list[str]:
    findings: list[str] = []
    if waitgrant.get("returncode") not in (0, None):
        findings.append("waitgrant_cli_returned_nonzero")
    if waitgrant.get("grant_lines", 0) == 0 and not waitgrant.get("grant_talkgroups_seen"):
        findings.append("no_control_channel_grants_seen")
    if waitgrant.get("follow_lines", 0) == 0:
        findings.append("no_voice_follow_attempt_seen")
    if waitgrant.get("wav_seconds", 0.0) <= 0.0:
        findings.append("live_wav_empty")
    if waitgrant.get("capture_dir") is None:
        findings.append("no_follow_iq_capture_saved")
    if replay:
        findings.extend(str(x) for x in replay.get("findings", []))
        status = str(replay.get("best_status") or "")
        if status == "PASS_CONTINUOUS_AUDIO":
            findings.append("replay_continuous_audio_confirmed")
        elif status == "PASS_PARTIAL_AUDIO":
            findings.append("replay_partial_audio_only")
        elif status == "FAIL_RAW_AUDIO_GATED":
            findings.append("replay_raw_audio_present_but_speaker_gated")
        elif status in {"FAIL_NO_AUDIO", "NO_REPLAY_RESULTS"}:
            findings.append("replay_no_clear_audio")
    return sorted(set(findings))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", type=float, required=True, help="P25 control channel in MHz.")
    parser.add_argument("--device", type=int, default=0, help="SDR device index.")
    parser.add_argument("--seconds", type=float, default=180.0, help="Control-channel monitor duration.")
    parser.add_argument("--record-seconds", type=float, default=8.0, help="Follow IQ/WAV record duration after audio opens.")
    parser.add_argument("--tg", type=int, help="Only follow this talkgroup.")
    parser.add_argument("--repo", type=Path, default=default_repo())
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--replay-timeout", type=float, default=60.0)
    parser.add_argument("--max-replay-grants", type=int, default=2)
    parser.add_argument("--no-replay", action="store_true")
    parser.add_argument("--no-deep-trace", action="store_true")
    args = parser.parse_args(argv)

    repo = args.repo.resolve()
    exe = (args.exe or default_exe(repo)).resolve()
    if not exe.is_file():
        raise FileNotFoundError(f"SDR_Town executable not found: {exe}")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = (args.out_dir or (repo / "build" / "p25_live_clear_audio_diag" / f"{timestamp}_{safe_name(str(args.cc))}MHz")).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    waitgrant = run_waitgrant(
        exe,
        cc_mhz=args.cc,
        device=args.device,
        seconds=args.seconds,
        record_seconds=args.record_seconds,
        target_tg=args.tg,
        transcript=out_dir / "waitgrant_stdout.txt",
        deep_trace=not args.no_deep_trace,
    )

    capture_dir_text = waitgrant.get("capture_dir")
    replay = None
    capture_audit = None
    if capture_dir_text:
        capture_dir = Path(capture_dir_text)
        if capture_dir.is_dir():
            try:
                capture_audit = p25_capture_audit.audit_capture(capture_dir)
                (out_dir / "capture_audit.json").write_text(
                    json.dumps(capture_audit, indent=2, sort_keys=True),
                    encoding="utf-8",
                )
            except Exception as ex:  # noqa: BLE001 - field diagnostic should keep going.
                capture_audit = {"error": str(ex)}
            if not args.no_replay:
                replay = run_deep_replay(
                    repo,
                    exe,
                    capture_dir,
                    out_dir,
                    args.replay_timeout,
                    args.max_replay_grants,
                )

    summary = {
        "out_dir": str(out_dir),
        "exe": str(exe),
        "waitgrant": waitgrant,
        "capture_audit": capture_audit,
        "replay": replay,
        "findings": diagnose_summary(waitgrant, replay),
    }
    summary_path = out_dir / "clear_audio_diag_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))

    best = (replay or {}).get("best_status")
    if best == "PASS_CONTINUOUS_AUDIO":
        return 0
    if waitgrant.get("grant_lines", 0) == 0:
        return 3
    if waitgrant.get("follow_lines", 0) == 0:
        return 4
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
