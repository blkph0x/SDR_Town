#!/usr/bin/env python3
"""Deep automated audit: live capture log + bounded CLI voicetest sweeps."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
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
from p25_voicetest_classify import PASS_AUDIO_STATUSES, classify_voicetest_output

CLEAR_AUDIO_STATUSES = frozenset({"PASS_CONTINUOUS_AUDIO", "PASS_CLEAR_AUDIO"})


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
    final_p2bursts: int = 0
    final_target_vcw: int = 0
    final_p2vcw: int = 0
    final_decoded_frames: int = 0
    final_audio_samples: int = 0
    final_audio_seconds: float = 0.0
    final_duty: float = 0.0
    final_plc: int = 0
    final_iq_reject: int = 0
    final_ambe_accepted: int = 0
    final_ambe_attempts: int = 0
    stt_pass: bool = False
    stt_words: int = 0
    stt_chars: int = 0
    stt_transcript: str = ""


def default_exe(repo: Path) -> Path:
    return repo / "build" / "bin" / "Release" / "SDR_Town.exe"


def run_voicetest(exe: Path, command: str, timeout_s: float) -> tuple[str, str]:
    try:
        proc = subprocess.run(
            [str(exe), "--cli", "--allow-multiple", "--cmd", command],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout_s,
            check=False,
        )
        return proc.stdout or "", classify_voicetest_output(proc.stdout or "")
    except subprocess.TimeoutExpired as ex:
        output = ex.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        output += f"\nP25 voicetest timeout after {timeout_s:.1f}s\n"
        return output, classify_voicetest_output(output, timed_out=True)


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
    explicit_release = len(re.findall(r"\bexplicit-clear-grant-(?:traffic-clear|validated)-release\b", output))
    wav_match = re.search(r'P25 voicetest wav="([^"]+)"', output)
    result_lines = re.findall(r"^P25 voicetest result=.*$", output, flags=re.MULTILINE)
    result_line = result_lines[-1] if result_lines else ""

    def final_int(name: str, default: int = 0) -> int:
        match = re.search(rf"\b{re.escape(name)}=(\d+)\b", result_line)
        return int(match.group(1)) if match else default

    def final_float(name: str, default: float = 0.0) -> float:
        match = re.search(rf"\b{re.escape(name)}=([0-9.]+)\b", result_line)
        return float(match.group(1)) if match else default

    def final_yesno(name: str) -> bool | None:
        match = re.search(rf"\b{re.escape(name)}=(yes|no)\b", result_line)
        if not match:
            return None
        return match.group(1) == "yes"

    def final_pair(name: str) -> tuple[int, int]:
        match = re.search(rf"\b{re.escape(name)}=(\d+)/(\d+)\b", result_line)
        if not match:
            return (0, 0)
        return (int(match.group(1)), int(match.group(2)))

    final_ambe = final_pair("ambe")
    final_opp_ambe = final_pair("oppAmbe")
    final_ambe_probe = final_pair("ambeProbe")
    final_status_match = re.search(r"\bresult=([A-Z0-9_]+)\b", result_line)
    metrics = {
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
        "final_status": final_status_match.group(1) if final_status_match else "",
        "final_voice_windows": final_int("voiceWindows"),
        "final_emit_windows": final_int("emitWindows"),
        "final_empty_windows": final_int("emptyWindows"),
        "final_decoded_frames": final_int("decodedFrames"),
        "final_audio_samples": final_int("audioSamples"),
        "final_speaker_samples": final_int("speakerSamples"),
        "final_audio_seconds": final_float("audioSeconds"),
        "final_span_seconds": final_float("spanSeconds"),
        "final_duty": final_float("duty"),
        "final_p2bursts": final_int("p2bursts"),
        "final_p2vcw": final_int("p2vcw"),
        "final_target_vcw": final_int("targetVcw"),
        "final_expected_vcw": final_int("expectedVcw"),
        "final_context_vcw": final_int("contextVcw"),
        "final_context_suppressed": final_int("contextSuppressed"),
        "final_fed": final_int("fed"),
        "final_emit_pcm": final_int("emitPcm"),
        "final_plc": final_int("plc"),
        "final_gaps": final_int("gaps"),
        "final_iq_reject": final_int("iqReject"),
        "final_ambe_accepted": final_ambe[0],
        "final_ambe_attempts": final_ambe[1],
        "final_opp_ambe_accepted": final_opp_ambe[0],
        "final_opp_ambe_attempts": final_opp_ambe[1],
        "final_opp_pending": final_int("oppPend"),
        "final_p2mask": final_int("p2mask"),
        "final_p2mac_crc": final_int("p2macCrc"),
        "final_trusted_clear_windows": final_int("trustedClearWindows"),
        "final_target_session_windows": final_int("targetSessionWindows"),
        "final_target_ess_clear_windows": final_int("targetEssClearWindows"),
        "final_dup_suppressed": final_int("dupSuppressed"),
        "final_abs_dup_suppressed": final_int("absDupSuppressed"),
        "final_seq_suppressed": final_int("seqSuppressed"),
        "final_timeline_ok": final_yesno("timelineOk"),
        "final_sequencer_ok": final_yesno("sequencerOk"),
        "final_concealment_ok": final_yesno("concealmentOk"),
        "final_ess_known": final_yesno("essKnown"),
        "final_ess_encrypted": final_yesno("essEncrypted"),
        "final_ambe_probe_accepted": final_ambe_probe[0],
        "final_ambe_probe_attempts": final_ambe_probe[1],
    }
    return metrics


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
    sample_rate_hz = summary.get("sample_rate_hz")
    voice_mhz = float(grant["voice_mhz"])
    replay_center_mhz = p25_capture_audit.grant_replay_center_mhz(grant, center_mhz)
    voice_center_arg = ""
    in_capture_passband = p25_capture_audit.grant_in_capture_passband(
        grant,
        replay_center_mhz,
        sample_rate_hz,
    )
    if replay_center_mhz > 0.0 and abs(replay_center_mhz - center_mhz) > 0.00001:
        voice_center_arg = f" voicecenter={replay_center_mhz:.5f}"
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
    parser.add_argument("--hop-ms", type=float, default=0.0, help="Continuous voice replay hop size. 0 uses CLI/GUI auto cadence.")
    parser.add_argument("--replay-ms", type=float, default=6000.0, help="Milliseconds to replay per sweep offset.")
    parser.add_argument("--minframes", type=int, default=2, help="Minimum decoded frames for a clear-audio pass.")
    parser.add_argument("--minaudio", type=float, default=0.05, help="Minimum decoded audio seconds for a clear-audio pass.")
    parser.add_argument("--no-wav", action="store_true", help="Do not write per-sweep decoded WAV artifacts.")
    parser.add_argument("--no-stt", action="store_true", help="Do not run STT on decoded WAV artifacts.")
    parser.add_argument(
        "--require-stt",
        action="store_true",
        help="Require a PASS_* audio status with STT pass for clear_audio_proven / exit 0.",
    )
    parser.add_argument(
        "--stt-backend",
        default=default_stt_backend(),
        help="STT backend for decoded WAV scoring (default: SDR_TOWN_STT_BACKEND or auto).",
    )
    parser.add_argument("--stt-timeout", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--stt-min-chars", type=int, default=DEFAULT_MIN_CHARS)
    parser.add_argument("--stt-min-words", type=int, default=DEFAULT_MIN_WORDS)
    args = parser.parse_args(argv)
    if args.require_stt and args.no_stt:
        parser.error("--require-stt cannot be combined with --no-stt")
    args.repo = args.repo.resolve()

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
            stt = (
                {
                    "enabled": False,
                    "ok": False,
                    "pass": False,
                    "transcript": "",
                    "chars": 0,
                    "words": 0,
                    "wav_path": metrics.get("wav_path"),
                }
                if args.no_stt
                else run_stt(
                    metrics.get("wav_path") or (str(wav_path) if wav_path else None),
                    backend=args.stt_backend,
                    min_chars=args.stt_min_chars,
                    min_words=args.stt_min_words,
                    timeout=args.stt_timeout,
                    repo=args.repo,
                )
            )
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
                final_p2bursts=metrics["final_p2bursts"],
                final_target_vcw=metrics["final_target_vcw"],
                final_p2vcw=metrics["final_p2vcw"],
                final_decoded_frames=metrics["final_decoded_frames"],
                final_audio_samples=metrics["final_audio_samples"],
                final_audio_seconds=metrics["final_audio_seconds"],
                final_duty=metrics["final_duty"],
                final_plc=metrics["final_plc"],
                final_iq_reject=metrics["final_iq_reject"],
                final_ambe_accepted=metrics["final_ambe_accepted"],
                final_ambe_attempts=metrics["final_ambe_attempts"],
                stt_pass=bool(stt.get("pass")),
                stt_words=int(stt.get("words") or 0),
                stt_chars=int(stt.get("chars") or 0),
                stt_transcript=str(stt.get("transcript") or ""),
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
                    "stt": stt,
                    "transcript": str(transcript),
                }
            )
            if best is None or (
                int(result.stt_pass),
                result.stt_words,
                result.final_duty,
                result.final_audio_seconds,
                result.final_decoded_frames,
                -result.final_plc,
                -result.final_iq_reject,
                result.final_p2bursts,
                result.final_target_vcw,
                result.final_p2vcw,
            ) > (
                int(best.stt_pass),
                best.stt_words,
                best.final_duty,
                best.final_audio_seconds,
                best.final_decoded_frames,
                -best.final_plc,
                -best.final_iq_reject,
                best.final_p2bursts,
                best.final_target_vcw,
                best.final_p2vcw,
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
    elif sweep_results and any(r["status"] == "PASS_CLEAR_AUDIO" for r in sweep_results):
        findings.append("cli_replay_clear_audio_pass")
    elif sweep_results and any(r["status"] == "PASS_PARTIAL_AUDIO" for r in sweep_results):
        findings.append("cli_replay_partial_audio_only")
    elif sweep_results and any(r["status"] == "FAIL_PLC_ONLY_INPUT_QUALITY_REJECTED" for r in sweep_results):
        findings.append("cli_replay_plc_only_input_quality_rejected")
    elif sweep_results and any(r["status"] == "FAIL_CONCEALMENT_ONLY_AUDIO" for r in sweep_results):
        findings.append("cli_replay_concealment_only_audio")
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
    if not args.no_stt and sweep_results:
        if any((r.get("stt") or {}).get("pass") for r in sweep_results):
            findings.append("stt_clear_text_seen")
        elif any(
            float(r.get("final_audio_seconds") or r.get("best_audio_seconds") or 0.0) > 0.0
            for r in sweep_results
        ):
            findings.append("stt_no_clear_text_from_decoded_audio")
        else:
            findings.append("stt_no_audio_to_score")

    if sweep_results and any(r["status"] in PASS_AUDIO_STATUSES for r in sweep_results):
        snapshot_only = {"no_speaker_audio_pushed", "live_zero_phase2_bursts"}
        findings = [finding for finding in findings if finding not in snapshot_only]

    deduped_findings: list[str] = []
    for finding in findings:
        if finding not in deduped_findings:
            deduped_findings.append(finding)

    has_audio_pass = any(r["status"] in PASS_AUDIO_STATUSES for r in sweep_results)
    has_clear_audio_pass = any(r["status"] in CLEAR_AUDIO_STATUSES for r in sweep_results)
    has_stt_pass = any(bool((r.get("stt") or {}).get("pass")) for r in sweep_results)
    all_sweeps_fail = bool(sweep_results) and not any(
        str(r.get("status") or "").startswith("PASS_") for r in sweep_results
    )
    if args.require_stt:
        clear_audio_proven = has_audio_pass and has_stt_pass
    else:
        clear_audio_proven = has_clear_audio_pass or has_stt_pass
    if all_sweeps_fail or not sweep_results:
        clear_audio_proven = False

    verdict = {
        "has_audio_pass": has_audio_pass,
        "has_clear_audio_pass": has_clear_audio_pass,
        "has_stt_pass": has_stt_pass,
        "all_sweeps_fail": all_sweeps_fail,
        "require_stt": bool(args.require_stt),
        "clear_audio_proven": clear_audio_proven,
    }

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
            "stt_enabled": not args.no_stt,
            "require_stt": bool(args.require_stt),
            "stt_backend": args.stt_backend,
            "stt_min_chars": args.stt_min_chars,
            "stt_min_words": args.stt_min_words,
        },
        "health": report["health"],
        "counts": report["counts"],
        "live_log_analysis": live,
        "findings": deduped_findings,
        "grants": grants,
        "sweep_results": sweep_results,
        "verdict": verdict,
    }
    summary_path = out_dir / "deep_audit_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if verdict["clear_audio_proven"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
