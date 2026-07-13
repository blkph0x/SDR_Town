#!/usr/bin/env python3
"""Deep automated audit: live capture log + bounded CLI voicetest sweeps."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import p25_capture_audit


@dataclass
class SweepResult:
    skip_ms: float
    status: str
    best_p2bursts: int
    best_p2vcw: int
    best_p2sf: int
    best_decoded: int
    best_audio: int
    gate_emit_hits: int


def default_exe(repo: Path) -> Path:
    return repo / "build" / "bin" / "Release" / "SDR_Town.exe"


def run_voicetest(exe: Path, command: str, timeout_s: float) -> tuple[str, str]:
    proc = subprocess.run(
        [str(exe), "--cli", "--allow-multiple"],
        input=f"{command}\ntrace off\nexit\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout_s,
        check=False,
    )
    return proc.stdout or "", classify_voicetest_output(proc.stdout or "")


def classify_voicetest_output(output: str) -> str:
    if "PASS_CLEAR_AUDIO" in output:
        return "PASS_CLEAR_AUDIO"
    if "PASS_ENCRYPTED_GATED" in output:
        return "PASS_ENCRYPTED_GATED"
    if "FAIL_NO_TRAFFIC_BURSTS" in output:
        return "FAIL_NO_TRAFFIC_BURSTS"
    if "FAIL_NO_AUDIO" in output:
        return "FAIL_NO_AUDIO"
    if "P25 voicetest load failed" in output:
        return "LOAD_FAILED"
    if "P25 voicetest voice" in output:
        return "RAN_NO_PASS"
    return "NO_TEST_OUTPUT"


def parse_voicetest_metrics(output: str) -> dict:
    bursts = [int(x) for x in re.findall(r"\bp2bursts=(\d+)\b", output)]
    vcw = [int(x) for x in re.findall(r"\bp2vcw=(\d+)\b", output)]
    sf = [int(x) for x in re.findall(r"\bp2sf=(\d+)\b", output)]
    decoded = [int(x) for x in re.findall(r"\bdecoded=(\d+)\b", output)]
    audio = [int(x) for x in re.findall(r"\baudio=(\d+)\b", output)]
    gate_emit = len(re.findall(r"\bgate=emit\b", output))
    return {
        "best_p2bursts": max(bursts) if bursts else 0,
        "best_p2vcw": max(vcw) if vcw else 0,
        "best_p2sf": max(sf) if sf else 0,
        "best_decoded": max(decoded) if decoded else 0,
        "best_audio": max(audio) if audio else 0,
        "gate_emit_hits": gate_emit,
    }


def analyze_live_log(log_path: Path) -> dict:
    text = log_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    worker = [ln for ln in lines if "P25 DSP VOICE WORKER:" in ln]
    audio_out = [ln for ln in lines if "P25 audio output:" in ln]
    gate_emit = [ln for ln in lines if "gate=emit" in ln]
    contexts = [int(x) for x in re.findall(r"context=(\d+)", "\n".join(worker))]
    fresh = [int(x) for x in re.findall(r"fresh=(\d+)", "\n".join(worker))]
    bursts = [int(x) for x in re.findall(r"p2bursts=(\d+)", "\n".join(worker))]
    max_chunks = [int(x) for x in re.findall(r"maxChunk=(\d+)", "\n".join(lines))]
    rolling_zero = len(re.findall(r"rolling=0\b", "\n".join(lines)))
    return {
        "worker_results": len(worker),
        "audio_output_events": len(audio_out),
        "gate_emit_worker_lines": len(gate_emit),
        "max_p2bursts": max(bursts) if bursts else 0,
        "windows_with_context_gt_0": sum(1 for c in contexts if c > 0),
        "avg_context": (sum(contexts) / len(contexts)) if contexts else 0.0,
        "avg_fresh": (sum(fresh) / len(fresh)) if fresh else 0.0,
        "max_chunk_seen": max(max_chunks) if max_chunks else 0,
        "rolling_zero_events": rolling_zero,
        "spurious_audio_suspect": len(audio_out) > 0 and len(gate_emit) == 0,
    }


def grant_sweep_offsets(grant: dict, span_ms: float, step_ms: float) -> list[float]:
    base = max(0.0, float(grant.get("relative_ms") or 0.0) - 150.0)
    offsets = []
    start = max(0.0, base - span_ms)
    end = base + span_ms
    cur = start
    while cur <= end:
        offsets.append(cur)
        cur += step_ms
    return offsets


def build_voicetest_command(capture_dir: Path, grant: dict, skip_ms: float, summary: dict) -> str:
    center_mhz = float(summary.get("center_freq_hz") or summary.get("freq_hz") or 0.0) / 1e6
    voice_mhz = float(grant["voice_mhz"])
    voice_center_arg = ""
    if abs(voice_mhz - center_mhz) > 0.10:
        voice_center_arg = f" voicecenter={voice_mhz:.5f}"
    masks = ""
    if {"nac", "wacn", "system"}.issubset(grant):
        masks = f" nac=0x{grant['nac']:x} wacn=0x{grant['wacn']:x} system=0x{grant['system']:x}"
    security = p25_capture_audit.grant_security_suffix(grant)
    return (
        f'p25 voicetest "{capture_dir}" {voice_mhz:.5f} '
        f"1200 skip={skip_ms:.0f} center={center_mhz:.5f}{voice_center_arg} "
        f'tg={grant["tg"]} slot={grant["slot"]} phase2{masks}{security} trace'
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", help="Capture directory")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--latest", action="store_true")
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--span-ms", type=float, default=20000.0, help="Sweep +/- span around each grant")
    parser.add_argument("--step-ms", type=float, default=5000.0, help="Sweep step in milliseconds")
    parser.add_argument("--max-grants", type=int, default=3)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args(argv)

    capture = (
        p25_capture_audit.latest_capture_dir(p25_capture_audit.default_capture_root())
        if args.latest or not args.capture
        else Path(args.capture)
    )
    exe = args.exe or default_exe(args.repo)
    if not exe.is_file():
        raise FileNotFoundError(f"SDR_Town executable not found: {exe}")

    report = p25_capture_audit.audit_capture(capture)
    live = analyze_live_log(Path(report["log_path"]))
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (args.repo / "build" / "p25_deep_audit" / f"{timestamp}_{capture.name}")
    out_dir.mkdir(parents=True, exist_ok=True)

    grants = [g for g in report["grants"] if g.get("phase2") and g.get("slot") is not None][: args.max_grants]
    sweep_results: list[dict] = []
    for grant in grants:
        if grant.get("replay_skipped"):
            continue
        offsets = grant_sweep_offsets(grant, args.span_ms, args.step_ms)
        best: SweepResult | None = None
        for skip_ms in offsets:
            command = build_voicetest_command(capture, grant, skip_ms, report["health"])
            output, status = run_voicetest(exe, command, args.timeout)
            metrics = parse_voicetest_metrics(output)
            result = SweepResult(
                skip_ms=skip_ms,
                status=status,
                best_p2bursts=metrics["best_p2bursts"],
                best_p2vcw=metrics["best_p2vcw"],
                best_p2sf=metrics["best_p2sf"],
                best_decoded=metrics["best_decoded"],
                best_audio=metrics["best_audio"],
                gate_emit_hits=metrics["gate_emit_hits"],
            )
            transcript = out_dir / f"tg{grant['tg']}_slot{grant['slot']}_skip{int(skip_ms)}_{status}.txt"
            transcript.write_text(output, encoding="utf-8", errors="replace")
            sweep_results.append(
                {
                    "tg": grant["tg"],
                    "slot": grant["slot"],
                    "voice_mhz": grant["voice_mhz"],
                    "skip_ms": skip_ms,
                    "status": status,
                    **metrics,
                    "transcript": str(transcript),
                }
            )
            if best is None or (
                result.best_p2bursts,
                result.best_p2vcw,
                result.best_decoded,
                result.best_audio,
            ) > (
                best.best_p2bursts,
                best.best_p2vcw,
                best.best_decoded,
                best.best_audio,
            ):
                best = result

    findings = list(report["findings"])
    health = report.get("health") or {}
    ring_resets = int(health.get("ring_epoch_resets") or 0)
    ring_overrun = int(health.get("ring_overrun_samples") or 0)
    if ring_resets >= 3:
        findings.append("iq_ring_epoch_resets_on_retune")
    if ring_overrun > 1_000_000:
        findings.append("iq_ring_overrun_samples_high")
    if live["spurious_audio_suspect"]:
        findings.append("spurious_audio_without_gate_emit")
    if live["max_p2bursts"] == 0:
        findings.append("live_zero_phase2_bursts")
    if live["rolling_zero_events"] > 0:
        findings.append("rolling_buffer_collapsed")
    if sweep_results and max(r["best_p2bursts"] for r in sweep_results) == 0:
        findings.append("cli_replay_zero_phase2_bursts")
    elif sweep_results and any(r["best_p2bursts"] > 0 for r in sweep_results):
        findings.append("cli_replay_has_phase2_sync")

    summary = {
        "capture_dir": str(capture),
        "exe": str(exe),
        "health": report["health"],
        "counts": report["counts"],
        "live_log_analysis": live,
        "findings": findings,
        "grants": grants,
        "sweep_results": sweep_results,
    }
    summary_path = out_dir / "deep_audit_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
