#!/usr/bin/env python3
"""End-to-end P25 clear-audio automation for live capture, replay, GUI, and STT.

This is the high-level harness for field/debug loops:
1. Run one or more live waitgrant/follow attempts on supplied control channels.
2. Save the waitgrant log, decoded live WAV, and follow IQ capture.
3. Deep-replay saved IQ with CLI voicetest sweeps and STT scoring.
4. Replay the best CLI/STT window through the GUI IQ replay path and STT gate.
5. Write one machine-readable summary for regression/progress tracking.
"""

from __future__ import annotations

import argparse
import json
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
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_exe(repo: Path) -> Path:
    return repo / "build" / "bin" / "Release" / "SDR_Town.exe"


def safe_name(text: str) -> str:
    return "".join(ch if ch.isalnum() or ch in ("-", "_", ".") else "_" for ch in text)[:120]


def load_json(path: Path) -> dict:
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8", errors="replace"))


def run_cmd(cmd: list[str], *, timeout_s: float, cwd: Path, transcript: Path) -> dict:
    transcript.parent.mkdir(parents=True, exist_ok=True)
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(cwd),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout_s,
            check=False,
        )
        output = proc.stdout or ""
        returncode = proc.returncode
        error = None
    except subprocess.TimeoutExpired as ex:
        output = ex.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        error = f"timeout_after_{timeout_s:.1f}s"
        returncode = -999
        output += f"\n{error}\n"
    transcript.write_text(output, encoding="utf-8", errors="replace")
    return {
        "command": cmd,
        "returncode": returncode,
        "stdout": str(transcript),
        "error": error,
    }


def build_release(repo: Path, out_dir: Path, *, timeout_s: float) -> dict:
    return run_cmd(
        ["cmake", "--build", "build", "--config", "Release", "--target", "SDR_Town", "--parallel"],
        timeout_s=timeout_s,
        cwd=repo,
        transcript=out_dir / "build_sdr_town_stdout.txt",
    )


def run_live_loop(args: argparse.Namespace, repo: Path, out_dir: Path) -> dict:
    loop_script = repo / "src" / "tools" / "run_p25_clear_audio_until_pass.py"
    loop_dir = out_dir / "live_loop"
    cmd = [
        sys.executable,
        str(loop_script),
        "--attempts",
        str(args.attempts),
        "--seconds",
        str(args.seconds),
        "--record-seconds",
        str(args.record_seconds),
        "--device",
        str(args.device),
        "--repo",
        str(repo),
        "--out-dir",
        str(loop_dir),
        "--replay-timeout",
        str(args.replay_timeout),
        "--max-replay-grants",
        str(args.max_replay_grants),
        "--stt-backend",
        args.stt_backend,
        "--stt-timeout",
        str(args.stt_timeout),
        "--stt-min-chars",
        str(args.stt_min_chars),
        "--stt-min-words",
        str(args.stt_min_words),
    ]
    for cc in args.cc:
        cmd.extend(["--cc", f"{cc:.5f}"])
    if args.tg:
        cmd.extend(["--tg", str(args.tg)])
    if args.accept_partial:
        cmd.append("--accept-partial")
    if args.no_stt:
        cmd.append("--no-stt")
    result = run_cmd(
        cmd,
        timeout_s=max(
            args.attempts * (args.seconds + args.record_seconds + args.replay_timeout * max(2, args.max_replay_grants) * 8.0),
            300.0,
        ),
        cwd=repo,
        transcript=out_dir / "live_loop_stdout.txt",
    )
    summary_path = loop_dir / "loop_summary.json"
    result["summary_path"] = str(summary_path) if summary_path.is_file() else None
    result["summary"] = load_json(summary_path)
    return result


def best_attempt(loop_summary: dict) -> dict | None:
    attempts = loop_summary.get("attempts") or []
    if not attempts:
        return None
    return max(
        attempts,
        key=lambda row: (
            int(row.get("score") or 0),
            1 if row.get("capture_dir") else 0,
            float(row.get("wav_seconds") or 0.0),
        ),
    )


def best_replay_result(summary: dict) -> dict | None:
    replay = summary.get("replay") or {}
    rows = replay.get("sweep_results") or []
    if not rows:
        return None
    return max(
        rows,
        key=lambda row: (
            1 if (row.get("stt") or {}).get("pass") else 0,
            int((row.get("stt") or {}).get("words") or 0),
            int((row.get("stt") or {}).get("chars") or 0),
            float(row.get("best_audio_seconds") or 0.0),
            int(row.get("best_target_vcw") or 0),
        ),
    )


def find_grant_for_replay(capture_dir: Path, replay: dict) -> tuple[dict | None, dict]:
    audit = p25_capture_audit.audit_capture(capture_dir)
    target_tg = int(replay.get("tg") or 0)
    target_slot = replay.get("slot")
    target_voice = float(replay.get("voice_mhz") or 0.0)
    best: dict | None = None
    for grant in audit.get("grants", []):
        if target_tg and int(grant.get("tg") or 0) != target_tg:
            continue
        if target_slot is not None and grant.get("slot") is not None and int(grant["slot"]) != int(target_slot):
            continue
        if target_voice > 0.0 and abs(float(grant.get("voice_mhz") or 0.0) - target_voice) > 0.00005:
            continue
        best = grant
        break
    return best, audit


def run_gui_replay(
    args: argparse.Namespace,
    repo: Path,
    exe: Path,
    out_dir: Path,
    capture_dir: Path,
    replay: dict,
) -> dict:
    grant, audit = find_grant_for_replay(capture_dir, replay)
    health = audit.get("health") or {}
    center_mhz = float(health.get("center_freq_hz") or health.get("freq_hz") or 0.0) / 1e6
    voice_mhz = float(replay.get("voice_mhz") or (grant or {}).get("voice_mhz") or 0.0)
    voice_center_mhz = p25_capture_audit.grant_replay_center_mhz(grant or replay, center_mhz)
    skip_ms = int(round(float(replay.get("skip_ms") or 0.0)))
    duration_ms = int(max(1000, round(float(args.gui_replay_ms))))
    gui_dir = out_dir / "gui_replay"
    ps_script = repo / "tests" / "run_gui_startup_tests.ps1"
    cmd = [
        "powershell",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ps_script),
        "-ExePath",
        str(exe),
        "-IqReplayCapture",
        str(capture_dir),
        "-IqReplayTargetMHz",
        f"{voice_mhz:.5f}",
        "-IqReplayStartMs",
        str(skip_ms),
        "-IqReplayDurationMs",
        str(duration_ms),
        "-IqReplayWindowMs",
        str(args.gui_window_ms),
        "-IqReplayHopMs",
        str(args.gui_hop_ms),
        "-IqReplayTimeoutSec",
        str(max(20, int(duration_ms / 1000) + 25)),
        "-IqReplayWithStt",
        "-IqReplayRequireStt",
        "-IqReplayRequireAudio",
        "-SttBackend",
        args.stt_backend,
        "-IqReplayMinTranscriptChars",
        str(args.stt_min_chars),
        "-IqReplayMinTranscriptWords",
        str(args.stt_min_words),
        "-TestRoot",
        str(gui_dir),
    ]
    if center_mhz > 0.0:
        cmd.extend(["-IqReplayCenterMHz", f"{center_mhz:.5f}"])
    if voice_center_mhz > 0.0:
        cmd.extend(["-IqReplayVoiceCenterMHz", f"{voice_center_mhz:.5f}"])
    if replay.get("tg"):
        cmd.extend(["-IqReplayTalkgroup", str(int(replay["tg"]))])
    if replay.get("slot") is not None:
        cmd.extend(["-IqReplaySlot", str(int(replay["slot"]))])
    if grant:
        if grant.get("nac") is not None:
            cmd.extend(["-IqReplayNac", f"0x{int(grant['nac']):03X}"])
        if grant.get("wacn") is not None:
            cmd.extend(["-IqReplayWacn", f"0x{int(grant['wacn']):05X}"])
        if grant.get("system") is not None:
            cmd.extend(["-IqReplaySystem", f"0x{int(grant['system']):03X}"])
        if str(grant.get("arm_clear", "")).lower() in {"yes", "true", "1"}:
            cmd.append("-IqReplayClear")
        if str(grant.get("arm_encrypted", "")).lower() in {"yes", "true", "1"}:
            cmd.append("-IqReplayEncrypted")

    result = run_cmd(
        cmd,
        timeout_s=max(60.0, duration_ms / 1000.0 + 60.0),
        cwd=repo,
        transcript=out_dir / "gui_replay_stdout.txt",
    )
    result["test_root"] = str(gui_dir)
    result["result_json"] = str(gui_dir / "gui_iq_replay.json")
    result["wav_path"] = str(gui_dir / "gui_iq_replay.wav")
    result["json"] = load_json(gui_dir / "gui_iq_replay.json")
    result["selected"] = {
        "capture_dir": str(capture_dir),
        "center_mhz": center_mhz,
        "voice_center_mhz": voice_center_mhz,
        "voice_mhz": voice_mhz,
        "skip_ms": skip_ms,
        "duration_ms": duration_ms,
        "tg": replay.get("tg"),
        "slot": replay.get("slot"),
        "grant": grant,
    }
    return result


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", type=float, action="append", required=True, help="Control channel MHz. Repeat for multiple CCs.")
    parser.add_argument("--attempts", type=int, default=4)
    parser.add_argument("--seconds", type=float, default=180.0)
    parser.add_argument("--record-seconds", type=float, default=8.0)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--tg", type=int)
    parser.add_argument("--repo", type=Path, default=repo_root())
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--build-timeout", type=float, default=900.0)
    parser.add_argument("--replay-timeout", type=float, default=90.0)
    parser.add_argument("--max-replay-grants", type=int, default=3)
    parser.add_argument("--accept-partial", action="store_true")
    parser.add_argument("--no-stt", action="store_true")
    parser.add_argument(
        "--stt-backend",
        default=default_stt_backend(),
        help="STT backend (default: SDR_TOWN_STT_BACKEND or auto; prefer faster-whisper then openai-whisper).",
    )
    parser.add_argument("--stt-timeout", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--stt-min-chars", type=int, default=DEFAULT_MIN_CHARS)
    parser.add_argument("--stt-min-words", type=int, default=DEFAULT_MIN_WORDS)
    parser.add_argument("--skip-gui-replay", action="store_true")
    parser.add_argument("--gui-replay-ms", type=float, default=6000.0)
    parser.add_argument("--gui-window-ms", type=int, default=720)
    parser.add_argument("--gui-hop-ms", type=int, default=0)
    args = parser.parse_args(argv)

    repo = args.repo.resolve()
    exe = (args.exe or default_exe(repo)).resolve()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    cc_label = "_".join(safe_name(f"{cc:.5f}") for cc in args.cc)
    out_dir = (args.out_dir or (repo / "build" / "p25_ai_clear_audio_pipeline" / f"{timestamp}_{cc_label}")).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    build = build_release(repo, out_dir, timeout_s=args.build_timeout) if args.build else None
    if build and build["returncode"] != 0:
        summary = {"out_dir": str(out_dir), "build": build, "ok": False, "failure": "build_failed"}
        (out_dir / "ai_clear_audio_pipeline_summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
        print(json.dumps(summary, indent=2, sort_keys=True))
        return 10
    if not exe.is_file():
        raise FileNotFoundError(f"SDR_Town executable not found: {exe}")

    live_loop = run_live_loop(args, repo, out_dir)
    loop_summary = live_loop.get("summary") or {}
    attempt = best_attempt(loop_summary)
    attempt_summary = load_json(Path(attempt["summary_path"])) if attempt and attempt.get("summary_path") else {}
    replay = best_replay_result(attempt_summary) if attempt_summary else None

    gui_replay = None
    if not args.skip_gui_replay and attempt and attempt.get("capture_dir") and replay:
        gui_replay = run_gui_replay(
            args,
            repo,
            exe,
            out_dir,
            Path(attempt["capture_dir"]),
            replay,
        )

    live_stt_pass = bool(((attempt_summary.get("waitgrant") or {}).get("live_stt") or {}).get("pass"))
    replay_stt_pass = bool(((attempt_summary.get("replay") or {}).get("best_stt") or {}).get("pass"))
    gui_ok = gui_replay is not None and gui_replay.get("returncode") == 0
    summary = {
        "schema": "sdr-town-p25-ai-clear-audio-pipeline-v1",
        "out_dir": str(out_dir),
        "exe": str(exe),
        "build": build,
        "live_loop": live_loop,
        "best_attempt": attempt,
        "best_attempt_summary_path": attempt.get("summary_path") if attempt else None,
        "best_replay": replay,
        "gui_replay": gui_replay,
        "verdict": {
            "live_stt_pass": live_stt_pass,
            "replay_stt_pass": replay_stt_pass,
            "gui_replay_stt_pass": gui_ok,
            "clear_audio_proven": live_stt_pass or replay_stt_pass or gui_ok,
        },
    }
    summary_path = out_dir / "ai_clear_audio_pipeline_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["verdict"]["clear_audio_proven"] else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
