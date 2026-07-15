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
    best_target_vcw: int
    best_opp_vcw: int
    best_p2sf: int
    best_p2mask: int
    best_p2mac_valid: int
    best_p2mac_total: int
    best_p2acch_fec: int
    best_p2acch_rs: int
    best_p2acch_direct: int
    best_p2acch_direct_rejected: int
    best_decoded: int
    best_audio: int
    best_audio_seconds: float
    best_duty: float
    best_ambe_probe_accepted: int
    best_ambe_probe_attempts: int
    gate_emit_hits: int
    explicit_clear_release_hits: int
    wav_path: str | None


def default_exe(repo: Path) -> Path:
    return repo / "build" / "bin" / "Release" / "SDR_Town.exe"


def run_voicetest(exe: Path, command: str, timeout_s: float) -> tuple[str, str]:
    proc = subprocess.run(
        [str(exe), "--cli", "--allow-multiple", "--cmd", command],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout_s,
        check=False,
    )
    return proc.stdout or "", classify_voicetest_output(proc.stdout or "")


def classify_voicetest_output(output: str) -> str:
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
    target_vcw = [int(x) for x in re.findall(r"\btargetVcw=(\d+)\b", output)]
    opp_vcw = [int(x) for x in re.findall(r"\boppVcw=(\d+)\b", output)]
    sf = [int(x) for x in re.findall(r"\bp2sf=(\d+)\b", output)]
    mask = [int(x) for x in re.findall(r"\bp2mask=(\d+)\b", output)]
    mac = [tuple(map(int, m)) for m in re.findall(r"\bp2mac=(\d+)/(\d+)\b", output)]
    acch = [
        tuple(map(int, m))
        for m in re.findall(
            r"\bp2acch=nom:(\d+) altKind:(\d+) swap:(\d+) slip:(\d+) inv:(\d+) fec:(\d+) rs:(\d+) dir:(\d+) drej:(\d+)\b",
            output,
        )
    ]
    decoded = [int(x) for x in re.findall(r"\bdecoded=(\d+)\b", output)]
    audio = [int(x) for x in re.findall(r"\baudio=(\d+)\b", output)]
    audio_seconds = [float(x) for x in re.findall(r"\baudioSeconds=([0-9.]+)\b", output)]
    duty = [float(x) for x in re.findall(r"\bduty=([0-9.]+)\b", output)]
    ambe_probe = [tuple(map(int, m)) for m in re.findall(r"\bambeProbe=(\d+)/(\d+)\b", output)]
    gate_emit = len(re.findall(r"\bgate=emit\b", output))
    explicit_release = len(re.findall(r"\bexplicit-clear-grant-validated-release\b", output))
    wav_match = re.search(r'P25 voicetest wav="([^"]+)"', output)
    return {
        "best_p2bursts": max(bursts) if bursts else 0,
        "best_p2vcw": max(vcw) if vcw else 0,
        "best_target_vcw": max(target_vcw) if target_vcw else 0,
        "best_opp_vcw": max(opp_vcw) if opp_vcw else 0,
        "best_p2sf": max(sf) if sf else 0,
        "best_p2mask": max(mask) if mask else 0,
        "best_p2mac_valid": max((a for a, _ in mac), default=0),
        "best_p2mac_total": max((b for _, b in mac), default=0),
        "best_p2acch_fec": max((m[5] for m in acch), default=0),
        "best_p2acch_rs": max((m[6] for m in acch), default=0),
        "best_p2acch_direct": max((m[7] for m in acch), default=0),
        "best_p2acch_direct_rejected": max((m[8] for m in acch), default=0),
        "best_decoded": max(decoded) if decoded else 0,
        "best_audio": max(audio) if audio else 0,
        "best_audio_seconds": max(audio_seconds) if audio_seconds else 0.0,
        "best_duty": max(duty) if duty else 0.0,
        "best_ambe_probe_accepted": max((a for a, _ in ambe_probe), default=0),
        "best_ambe_probe_attempts": max((b for _, b in ambe_probe), default=0),
        "gate_emit_hits": gate_emit,
        "explicit_clear_release_hits": explicit_release,
        "wav_path": wav_match.group(1) if wav_match else None,
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


def grant_sweep_offsets(grant: dict, span_ms: float, step_ms: float, actual_ms: float, replay_ms: float) -> list[float]:
    if grant.get("relative_ms") is None and actual_ms > 0.0:
        offsets: list[float] = []
        last_start = max(0.0, actual_ms - max(100.0, replay_ms))
        cur = 0.0
        step = max(100.0, step_ms)
        while cur <= last_start:
            offsets.append(cur)
            cur += step
        if not offsets or abs(offsets[-1] - last_start) > 1.0:
            offsets.append(last_start)
        return sorted(set(round(x, 3) for x in offsets))

    base = max(0.0, float(grant.get("relative_ms") or 0.0) - 150.0)
    offsets = []
    start = max(0.0, base - span_ms)
    end = base + span_ms
    cur = start
    while cur <= end:
        offsets.append(cur)
        cur += step_ms
    return offsets


def quote_cli_path(path: Path) -> str:
    return str(path).replace('"', '\\"')


def build_voicetest_command(
    capture_dir: Path,
    grant: dict,
    skip_ms: float,
    summary: dict,
    *,
    window_ms: float,
    hop_ms: float,
    min_frames: int,
    min_audio: float,
    replay_ms: float,
    wav_path: Path | None,
) -> str:
    center_mhz = float(summary.get("center_freq_hz") or summary.get("freq_hz") or 0.0) / 1e6
    voice_mhz = float(grant["voice_mhz"])
    voice_center_arg = ""
    if abs(voice_mhz - center_mhz) > 0.10:
        voice_center_arg = f" voicecenter={voice_mhz:.5f}"
    masks = ""
    if {"nac", "wacn", "system"}.issubset(grant):
        masks = f" nac=0x{grant['nac']:x} wacn=0x{grant['wacn']:x} system=0x{grant['system']:x}"
    security = p25_capture_audit.grant_security_suffix(grant)
    wav_arg = f' wav="{quote_cli_path(wav_path)}"' if wav_path else ""
    return (
        f'p25 voicetest "{capture_dir}" {voice_mhz:.5f} '
        f"{max(250.0, replay_ms):.0f} skip={skip_ms:.0f} center={center_mhz:.5f}{voice_center_arg} "
        f'tg={grant["tg"]} slot={grant["slot"]} phase2 noprobe{masks}{security} '
        f"stream windowms={window_ms:.0f} hopms={hop_ms:.0f} "
        f"minframes={max(0, int(min_frames))} minaudio={max(0.0, float(min_audio)):.3f}"
        f"{wav_arg} trace"
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
    parser.add_argument("--window-ms", type=float, default=720.0, help="Continuous voice replay lookback window.")
    parser.add_argument("--hop-ms", type=float, default=40.0, help="Continuous voice replay hop size.")
    parser.add_argument("--replay-ms", type=float, default=6000.0, help="Milliseconds to replay per sweep offset.")
    parser.add_argument("--minframes", type=int, default=2, help="Minimum decoded frames for a clear-audio pass.")
    parser.add_argument("--minaudio", type=float, default=0.05, help="Minimum decoded audio seconds for a clear-audio pass.")
    parser.add_argument("--no-wav", action="store_true", help="Do not write per-sweep decoded WAV artifacts.")
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
    health = report.get("health") or {}
    actual_ms = float(health.get("actual_seconds") or 0.0) * 1000.0
    for grant in grants:
        if grant.get("replay_skipped"):
            continue
        offsets = grant_sweep_offsets(grant, args.span_ms, args.step_ms, actual_ms, args.replay_ms)
        best: SweepResult | None = None
        for skip_ms in offsets:
            wav_path = None
            if not args.no_wav:
                wav_path = out_dir / (
                    f"tg{grant['tg']}_slot{grant['slot']}_"
                    f"skip{int(skip_ms)}.wav"
                )
            command = build_voicetest_command(
                capture,
                grant,
                skip_ms,
                report["health"],
                window_ms=args.window_ms,
                hop_ms=args.hop_ms,
                min_frames=args.minframes,
                min_audio=args.minaudio,
                replay_ms=args.replay_ms,
                wav_path=wav_path,
            )
            output, status = run_voicetest(exe, command, args.timeout)
            metrics = parse_voicetest_metrics(output)
            result = SweepResult(
                skip_ms=skip_ms,
                status=status,
                best_p2bursts=metrics["best_p2bursts"],
                best_p2vcw=metrics["best_p2vcw"],
                best_target_vcw=metrics["best_target_vcw"],
                best_opp_vcw=metrics["best_opp_vcw"],
                best_p2sf=metrics["best_p2sf"],
                best_p2mask=metrics["best_p2mask"],
                best_p2mac_valid=metrics["best_p2mac_valid"],
                best_p2mac_total=metrics["best_p2mac_total"],
                best_p2acch_fec=metrics["best_p2acch_fec"],
                best_p2acch_rs=metrics["best_p2acch_rs"],
                best_p2acch_direct=metrics["best_p2acch_direct"],
                best_p2acch_direct_rejected=metrics["best_p2acch_direct_rejected"],
                best_decoded=metrics["best_decoded"],
                best_audio=metrics["best_audio"],
                best_audio_seconds=metrics["best_audio_seconds"],
                best_duty=metrics["best_duty"],
                best_ambe_probe_accepted=metrics["best_ambe_probe_accepted"],
                best_ambe_probe_attempts=metrics["best_ambe_probe_attempts"],
                gate_emit_hits=metrics["gate_emit_hits"],
                explicit_clear_release_hits=metrics["explicit_clear_release_hits"],
                wav_path=metrics["wav_path"],
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
                result.best_target_vcw,
                result.best_p2vcw,
                result.best_decoded,
                result.best_audio,
                result.best_audio_seconds,
            ) > (
                best.best_p2bursts,
                best.best_target_vcw,
                best.best_p2vcw,
                best.best_decoded,
                best.best_audio,
                best.best_audio_seconds,
            ):
                best = result

    findings = list(report["findings"])
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
    if sweep_results and any(r["status"] == "PASS_CONTINUOUS_AUDIO" for r in sweep_results):
        findings.append("cli_replay_continuous_audio_pass")
    elif sweep_results and any(r["status"] == "PASS_PARTIAL_AUDIO" for r in sweep_results):
        findings.append("cli_replay_partial_audio_only")
    elif sweep_results and any(r["status"] == "FAIL_RAW_AUDIO_GATED" for r in sweep_results):
        findings.append("raw_audio_present_but_speaker_gated")
    elif sweep_results and any(r["best_ambe_probe_accepted"] > 0 and r["best_audio"] == 0 for r in sweep_results):
        findings.append("ambe_probe_accepts_but_speaker_still_gated")
    if sweep_results and any(
        r["best_p2mac_total"] > 0 and
        r["best_p2mac_valid"] == 0 and
        r["best_p2acch_fec"] == 0 and
        r["best_p2acch_rs"] == 0 and
        r["best_p2acch_direct"] == 0
        for r in sweep_results
    ):
        findings.append("phase2_acch_extracted_but_no_recovery")
    if sweep_results and any(
        r["best_p2acch_fec"] > 0 or r["best_p2acch_rs"] > 0 or r["best_p2acch_direct"] > 0
        for r in sweep_results
    ):
        findings.append("phase2_acch_recovery_seen")
    if sweep_results and any(r["best_target_vcw"] == 0 and r["best_opp_vcw"] > 0 for r in sweep_results):
        findings.append("opposite_slot_voice_seen_without_target_voice")

    summary = {
        "capture_dir": str(capture),
        "exe": str(exe),
        "diagnostic_profile": {
            "stream": True,
            "window_ms": args.window_ms,
            "hop_ms": args.hop_ms,
            "replay_ms": args.replay_ms,
            "minframes": args.minframes,
            "minaudio": args.minaudio,
            "wav_enabled": not args.no_wav,
        },
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
