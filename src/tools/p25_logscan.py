#!/usr/bin/env python3
"""Deep forensic scan of a P25 start/stop capture p25_log (DEC-0047).

Prints CADENCE A–E, worker-busy, wall stamps, Auto PPM, emit-gate dsp,
reject-before-feed / dup-accounted signatures, and recommended voicetest cmds.
Also usable as `p25 logscan` via CliApp once wired.
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path

# Reuse grant/command helpers when available.
ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "src" / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "src" / "tools"))

try:
    import p25_capture_audit as audit
except Exception:  # pragma: no cover
    audit = None


def _pct(vals: list[float], p: float) -> float | None:
    if not vals:
        return None
    s = sorted(vals)
    i = min(len(s) - 1, max(0, int(round((p / 100.0) * (len(s) - 1)))))
    return s[i]


def resolve_log(path: Path) -> Path:
    if path.is_file():
        return path
    matches = sorted(path.glob("*_p25_log.txt"))
    if not matches:
        raise FileNotFoundError(f"no *_p25_log.txt under {path}")
    return matches[0]


def scan_log(log_path: Path) -> dict:
    text = log_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    drop = Counter()
    duty: list[float] = []
    tg_duty: dict[str, list[float]] = defaultdict(list)
    for line in lines:
        if "P25 CADENCE 1s:" not in line:
            continue
        m = re.search(r"drop=(\w+)", line)
        if m:
            drop[m.group(1)] += 1
        d = re.search(r"dutySec=([0-9.]+)", line)
        tg = re.search(r"TG=(\d+)", line)
        if d:
            v = float(d.group(1))
            duty.append(v)
            if tg:
                tg_duty[tg.group(1)].append(v)

    dsp_ms: list[float] = []
    emit_dsp_ms: list[float] = []
    empty_dsp_ms: list[float] = []
    for line in lines:
        if "P25 DSP VOICE WORKER:" not in line:
            continue
        m = re.search(r"dsp=(\d+)us", line)
        if not m:
            continue
        ms = int(m.group(1)) / 1000.0
        dsp_ms.append(ms)
        if "gate=emit" in line:
            emit_dsp_ms.append(ms)
        if "gate=empty-audio" in line or "targetVcw=0" in line:
            empty_dsp_ms.append(ms)

    signatures = {
        "worker_busy": sum(1 for l in lines if "worker-busy" in l),
        "wall_timeout": sum(1 for l in lines if "decode-wall-timeout" in l),
        "wall_empty_kept": sum(
            1 for l in lines if "wall stamp must not wipe playout" in l or "wall-empty" in l
        ),
        "wall_evidence_kept": sum(1 for l in lines if "over-budget decode evidence" in l),
        "auto_ppm": sum(1 for l in lines if "Auto PPM:" in l),
        "no_vcw_block": sum(1 for l in lines if "block=no-vcw-from-live-window" in l),
        "rejected_before_feed": sum(
            1 for l in lines if "phase2-clear-target-vcw-rejected-before-feed" in l
        ),
        "dup_context_accounted": sum(
            1 for l in lines if "phase2-clear-target-vcw-duplicate-context-accounted" in l
        ),
        "not_fed": sum(1 for l in lines if "phase2-clear-target-vcw-not-fed" in l),
        "gate_emit": sum(1 for l in lines if "gate=emit" in l),
        "wrong_tdma": sum(1 for l in lines if "wrong-TDMA" in l or "wrongSlot=" in l and "wrongSlot=0" not in l),
    }

    # Prefer audit classifiers when available (live logs don't print issue tags).
    if audit is not None and hasattr(audit, "clear_target_feed_starvation_issue"):
        for line in lines:
            issue = audit.clear_target_feed_starvation_issue(line)
            if issue == "phase2-clear-target-vcw-rejected-before-feed":
                signatures["rejected_before_feed"] += 1
            elif issue == "phase2-clear-target-vcw-duplicate-context-accounted":
                signatures["dup_context_accounted"] += 1
            elif issue == "phase2-clear-target-vcw-not-fed":
                signatures["not_fed"] += 1

    auto_ppm_lines = [l for l in lines if "Auto PPM:" in l][:5]

    # Stage verdict from drop mix + signatures.
    total_cad = sum(drop.values()) or 1
    a_share = drop.get("A", 0) / total_cad
    d_share = drop.get("D", 0) / total_cad
    b_share = drop.get("B", 0) / total_cad
    ok_share = drop.get("ok", 0) / total_cad
    if signatures["rejected_before_feed"] or signatures["dup_context_accounted"] or signatures.get("not_fed"):
        primary = "FEED_GATE / slot-or-dup reject (VCWs exist, not reaching mbelib)"
    elif a_share >= 0.6 and signatures["no_vcw_block"] > 50:
        primary = "EXTRACT (drop A / no-vcw) — eye/sticky/CQPSK, not codec"
    elif d_share >= 0.2 or signatures["worker_busy"] > 50:
        primary = "PLAYOUT / worker-busy (drop D) — throughput, not codec"
    elif b_share >= 0.2:
        primary = "FEED starve (drop B) — DEC-0012 / security gate"
    elif ok_share >= 0.5:
        primary = "CADENCE mostly ok — check subjective audio quality (codec candidate)"
    else:
        primary = "mixed / need per-TG drill-down"

    neo = (
        "Possible — duty ok but subjective voice bad; spike neo behind P25AmbeVoiceDecoder."
        if primary.startswith("CADENCE mostly ok")
        else "NOT indicated — failure is pre-vocoder (extract/feed/worker)."
    )

    return {
        "log_path": str(log_path),
        "lines": len(lines),
        "cadence": {
            "n": sum(drop.values()),
            "drop": dict(drop),
            "duty_mean": round(statistics.mean(duty), 4) if duty else None,
            "duty_median": round(statistics.median(duty), 4) if duty else None,
            "duty_max": round(max(duty), 4) if duty else None,
            "ok_ge_0_65": sum(1 for x in duty if x >= 0.65),
            "per_tg": {
                tg: {
                    "n": len(vals),
                    "max": round(max(vals), 4),
                    "mean": round(statistics.mean(vals), 4),
                    "ok": sum(1 for x in vals if x >= 0.65),
                }
                for tg, vals in sorted(tg_duty.items(), key=lambda kv: -max(kv[1]))
            },
        },
        "worker_dsp_ms": {
            "n": len(dsp_ms),
            "p50": _pct(dsp_ms, 50),
            "p90": _pct(dsp_ms, 90),
            "max": max(dsp_ms) if dsp_ms else None,
            "emit_n": len(emit_dsp_ms),
            "emit_p50": _pct(emit_dsp_ms, 50),
            "emit_p90": _pct(emit_dsp_ms, 90),
            "empty_p50": _pct(empty_dsp_ms, 50),
        },
        "signatures": signatures,
        "auto_ppm_samples": [
            (l[l.find("Auto PPM:") :][:180] if "Auto PPM:" in l else l[-180:])
            for l in auto_ppm_lines
        ],
        "primary_failure_class": primary,
        "mbelib_neo_advice": neo,
    }


def print_human(report: dict) -> None:
    print("=== P25 LOGSCAN (DEC-0047) ===")
    print(f"log: {report['log_path']}")
    print(f"lines: {report['lines']}")
    c = report["cadence"]
    print(
        f"CADENCE n={c['n']} drop={c['drop']} "
        f"duty mean/med/max={c['duty_mean']}/{c['duty_median']}/{c['duty_max']} "
        f"ok>=0.65={c['ok_ge_0_65']}"
    )
    for tg, st in c["per_tg"].items():
        print(f"  TG{tg}: n={st['n']} max={st['max']} mean={st['mean']} ok={st['ok']}")
    w = report["worker_dsp_ms"]
    print(
        f"worker dsp ms: n={w['n']} p50={w['p50']} p90={w['p90']} max={w['max']} "
        f"| emit p50={w['emit_p50']} p90={w['emit_p90']} | empty p50={w['empty_p50']}"
    )
    print("signatures:", json.dumps(report["signatures"], sort_keys=True))
    print("PRIMARY:", report["primary_failure_class"])
    print("mbelib-neo:", report["mbelib_neo_advice"])
    for s in report["auto_ppm_samples"]:
        print(" ", s.encode("ascii", "replace").decode("ascii"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", help="capture dir or *_p25_log.txt")
    ap.add_argument("--json", action="store_true", help="emit JSON only")
    ap.add_argument("--audit", action="store_true", help="also run p25_capture_audit findings")
    args = ap.parse_args()
    path = Path(args.path)
    log = resolve_log(path)
    report = scan_log(log)
    if args.audit and audit is not None:
        cap = log.parent if log.is_file() else path
        try:
            audited = audit.audit_capture(cap) if hasattr(audit, "audit_capture") else None
            if audited is None and hasattr(audit, "main"):
                # Fall back: invoke recommend via parse only
                pass
            if isinstance(audited, dict):
                report["audit_findings"] = audited.get("findings")
                report["recommended_cli_commands"] = audited.get("recommended_cli_commands")
        except Exception as ex:  # pragma: no cover
            report["audit_error"] = str(ex)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_human(report)
        if report.get("recommended_cli_commands"):
            print("recommended CLI:")
            for cmd in report["recommended_cli_commands"]:
                print(" ", cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
