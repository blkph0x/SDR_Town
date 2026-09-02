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
from p25_stt_common import (
    DEFAULT_MIN_CHARS,
    DEFAULT_MIN_WORDS,
    DEFAULT_TIMEOUT_S,
    default_stt_backend,
    run_stt,
)


def default_repo() -> Path:
    return Path(__file__).resolve().parents[2]


def default_exe(repo: Path) -> Path:
    return repo / "build" / "bin" / "Release" / "SDR_Town.exe"


def safe_name(text: str) -> str:
    return "".join(ch if ch.isalnum() or ch in ("-", "_", ".") else "_" for ch in text)[:120]


def parse_source_id_token(raw: str) -> int | None:
    try:
        if raw.lower().startswith("0x") or any(ch in "ABCDEFabcdef" for ch in raw):
            return int(raw, 16)
        return int(raw, 10)
    except ValueError:
        return None


def extract_tg_source_pairs(output: str) -> list[dict]:
    pairs: dict[tuple[int, int], dict] = {}
    for line in output.splitlines():
        tg_match = re.search(r"\bTG[ =](\d+)\b", line, re.IGNORECASE)
        src_match = re.search(r"\b(?:src|source|rid|radio|sourceId)=(0x[0-9A-Fa-f]+|[0-9A-Fa-f]+)\b", line, re.IGNORECASE)
        if not (tg_match and src_match):
            continue
        source_id = parse_source_id_token(src_match.group(1))
        if source_id is None:
            continue
        tg = int(tg_match.group(1))
        pairs[(tg, source_id)] = {
            "tg": tg,
            "source_id": source_id,
            "source_hex": f"0x{source_id:06X}",
        }
    return [pairs[key] for key in sorted(pairs)]


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
    wav_pcm_opened = any(float(row.get("wav_seconds") or 0.0) > 0.0 for row in follow_records)
    audio_opened = (
        "P25 speaker-gated audio opened" in output or
        "P25 decoded audio opened" in output or
        "followCaptureReason=audio_opened" in output or
        wav_pcm_opened
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
        "grant_tg_sources_seen": extract_tg_source_pairs(output),
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
    try:
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
        returncode = proc.returncode
        timeout_error = None
    except subprocess.TimeoutExpired as ex:
        output = ex.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        timeout_error = f"waitgrant_timeout_after_{max(seconds + record_seconds + 90.0, 120.0):.1f}s"
        output += f"\n{timeout_error}\n"
        returncode = -999
    transcript.write_text(output, encoding="utf-8", errors="replace")
    parsed = parse_waitgrant_output(output)
    parsed.update(
        {
            "command": command,
            "returncode": returncode,
            "error": timeout_error,
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
    *,
    stt_enabled: bool,
    stt_backend: str,
    stt_min_chars: int,
    stt_min_words: int,
    stt_timeout: float,
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
        "720",
        "--hop-ms",
        "0",
        "--minframes",
        "2",
        "--minaudio",
        "0.05",
        "--out-dir",
        str(replay_dir),
    ]
    if stt_enabled:
        cmd.extend([
            "--stt-backend",
            stt_backend,
            "--stt-min-chars",
            str(stt_min_chars),
            "--stt-min-words",
            str(stt_min_words),
            "--stt-timeout",
            str(stt_timeout),
        ])
    else:
        cmd.append("--no-stt")
    try:
        proc = subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=max(timeout_s * max(1, max_grants) * 8.0, 120.0),
            check=False,
        )
        stdout = proc.stdout or ""
        returncode = proc.returncode
        timeout_error = None
    except subprocess.TimeoutExpired as ex:
        stdout = ex.stdout or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        timeout_error = f"deep_replay_timeout_after_{max(timeout_s * max(1, max_grants) * 8.0, 120.0):.1f}s"
        stdout += f"\n{timeout_error}\n"
        returncode = -999
    transcript = replay_dir / "deep_audit_stdout.txt"
    transcript.write_text(stdout, encoding="utf-8", errors="replace")
    summary_path = replay_dir / "deep_audit_summary.json"
    summary: dict = {}
    if summary_path.is_file():
        summary = json.loads(summary_path.read_text(encoding="utf-8", errors="replace"))
    return {
        "command": " ".join(cmd),
        "returncode": returncode,
        "error": timeout_error,
        "stdout": str(transcript),
        "summary_path": str(summary_path) if summary_path.is_file() else None,
        "findings": summary.get("findings", []),
        "counts": summary.get("counts", {}),
        "best_status": best_replay_status(summary.get("sweep_results", [])),
        "best_stt": best_replay_stt(summary.get("sweep_results", [])),
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


def best_replay_stt(results: list[dict]) -> dict:
    best: dict = {
        "pass": False,
        "transcript": "",
        "chars": 0,
        "words": 0,
        "wav_path": None,
    }
    for result in results:
        stt = result.get("stt") or {}
        score = (
            1 if stt.get("pass") else 0,
            int(stt.get("words") or 0),
            int(stt.get("chars") or 0),
            float(result.get("best_audio_seconds") or 0.0),
        )
        best_score = (
            1 if best.get("pass") else 0,
            int(best.get("words") or 0),
            int(best.get("chars") or 0),
            float(best.get("audio_seconds") or 0.0),
        )
        if score > best_score:
            best = {
                "pass": bool(stt.get("pass")),
                "ok": bool(stt.get("ok")),
                "transcript": str(stt.get("transcript") or ""),
                "chars": int(stt.get("chars") or 0),
                "words": int(stt.get("words") or 0),
                "wav_path": stt.get("wav_path") or result.get("wav_path"),
                "audio_seconds": float(result.get("best_audio_seconds") or 0.0),
                "tg": result.get("tg"),
                "slot": result.get("slot"),
                "voice_mhz": result.get("voice_mhz"),
                "skip_ms": result.get("skip_ms"),
            }
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
    live_stt = waitgrant.get("live_stt") or {}
    if live_stt.get("pass"):
        findings.append("live_stt_clear_text_seen")
    elif live_stt.get("enabled") and waitgrant.get("wav_seconds", 0.0) > 0.0:
        findings.append("live_stt_no_clear_text")
    if waitgrant.get("capture_dir") is None:
        findings.append("no_follow_iq_capture_saved")
    if replay:
        status = str(replay.get("best_status") or "")
        replay_stt = replay.get("best_stt") or {}
        if replay_stt.get("pass"):
            findings.append("replay_stt_clear_text_seen")
        elif replay_stt.get("chars", 0) == 0 and status in {"PASS_CONTINUOUS_AUDIO", "PASS_CLEAR_AUDIO", "PASS_PARTIAL_AUDIO"}:
            findings.append("replay_audio_pass_but_stt_empty")
        suppress_snapshot_only = set()
        if status in {"PASS_CONTINUOUS_AUDIO", "PASS_CLEAR_AUDIO", "PASS_PARTIAL_AUDIO"}:
            suppress_snapshot_only.update({"no_speaker_audio_pushed", "live_zero_phase2_bursts"})
        findings.extend(
            str(x)
            for x in replay.get("findings", [])
            if str(x) not in suppress_snapshot_only
        )
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
    parser.add_argument("--no-stt", action="store_true", help="Do not run STT on live/replay WAV artifacts.")
    parser.add_argument(
        "--stt-backend",
        default=default_stt_backend(),
        help="STT backend (default: SDR_TOWN_STT_BACKEND or auto).",
    )
    parser.add_argument("--stt-timeout", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--stt-min-chars", type=int, default=DEFAULT_MIN_CHARS)
    parser.add_argument("--stt-min-words", type=int, default=DEFAULT_MIN_WORDS)
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
    waitgrant["live_stt"] = (
        {
            "enabled": False,
            "ok": False,
            "pass": False,
            "transcript": "",
            "chars": 0,
            "words": 0,
            "wav_path": waitgrant.get("wav_path"),
        }
        if args.no_stt
        else run_stt(
            waitgrant.get("wav_path"),
            backend=args.stt_backend,
            min_chars=args.stt_min_chars,
            min_words=args.stt_min_words,
            timeout=args.stt_timeout,
            repo=repo,
        )
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
                    stt_enabled=not args.no_stt,
                    stt_backend=args.stt_backend,
                    stt_min_chars=args.stt_min_chars,
                    stt_min_words=args.stt_min_words,
                    stt_timeout=args.stt_timeout,
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
    stt_pass = bool((waitgrant.get("live_stt") or {}).get("pass")) or bool(((replay or {}).get("best_stt") or {}).get("pass"))
    if stt_pass:
        return 0
    if best == "PASS_CONTINUOUS_AUDIO" and args.no_stt:
        return 0
    if waitgrant.get("grant_lines", 0) == 0:
        return 3
    if waitgrant.get("follow_lines", 0) == 0:
        return 4
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
