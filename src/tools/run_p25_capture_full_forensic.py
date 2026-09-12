#!/usr/bin/env python3
"""One-shot forensic pipeline for a P25 start/stop capture (DEC-0047/0049).

Runs: logscan --audit → recommended clear voicetests (8s) → summary JSON.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "src" / "tools"
sys.path.insert(0, str(TOOLS))

import p25_capture_audit as audit  # noqa: E402
import p25_logscan as logscan  # noqa: E402


def default_exe() -> Path:
    return ROOT / "build" / "bin" / "Release" / "SDR_Town.exe"


def run_voicetest(exe: Path, cmd: str, out_path: Path, timeout_s: float = 120.0) -> dict:
    # Stretch recommended 1800ms bars to 8000ms for continuous duty evidence.
    cmd8 = re.sub(r"\b1800\b", "8000", cmd, count=1)
    if "clear" not in cmd8 and " enc" not in cmd8 and not cmd8.endswith(" enc"):
        if "phase2" in cmd8 and "clear" not in cmd8:
            cmd8 = cmd8 + " clear"
    if "stream" not in cmd8:
        cmd8 = cmd8 + " stream noprobe"
    full = [str(exe), "--cli", "--allow-multiple", "--cmd", cmd8]
    proc = subprocess.run(full, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout_s)
    out_path.write_text(proc.stdout or "", encoding="utf-8", errors="replace")
    m = re.search(r"P25 voicetest result=(\S+).*?duty=([0-9.]+).*?drop=(\S+)", proc.stdout or "")
    if not m:
        # result line format: result=X drop=Y ... duty=Z
        m = re.search(
            r"P25 voicetest result=(\S+)\s+drop=(\S+).*?\bduty=([0-9.]+)",
            proc.stdout or "",
        )
        if m:
            return {"cmd": cmd8, "result": m.group(1), "drop": m.group(2), "duty": float(m.group(3)), "rc": proc.returncode}
        return {"cmd": cmd8, "result": "NO_RESULT", "drop": None, "duty": None, "rc": proc.returncode}
    return {"cmd": cmd8, "result": m.group(1), "duty": float(m.group(2)), "drop": m.group(3), "rc": proc.returncode}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("capture", type=Path)
    ap.add_argument("--exe", type=Path, default=None)
    ap.add_argument("--max-voicetests", type=int, default=4)
    ap.add_argument("--json-out", type=Path, default=None)
    args = ap.parse_args()
    cap = args.capture.resolve()
    exe = args.exe or default_exe()
    report = logscan.scan_log(logscan.resolve_log(cap))
    audited = audit.audit_capture(cap)
    report["audit_findings"] = audited.get("findings")
    report["recommended_cli_commands"] = audited.get("recommended_cli_commands")
    report["grants"] = audited.get("grants")

    logscan.print_human(report)
    print("findings:", report.get("audit_findings"))

    vt_results = []
    cmds = [
        c
        for c in (report.get("recommended_cli_commands") or [])
        if c.startswith("p25 voicetest") and " enc" not in c and not c.rstrip().endswith(" enc")
    ][: args.max_voicetests]
    out_dir = cap
    for i, cmd in enumerate(cmds):
        out = out_dir / f"auto_forensic_vt_{i}.txt"
        print(f"VOICETEST[{i}]: {cmd[:100]}...")
        try:
            vt_results.append(run_voicetest(exe, cmd, out))
            print(" ", vt_results[-1])
        except Exception as ex:  # pragma: no cover
            vt_results.append({"cmd": cmd, "error": str(ex)})
            print("  ERROR", ex)
    report["voicetest_results"] = vt_results

    # Operator action hints
    actions = []
    if report["signatures"].get("auto_ppm", 0):
        actions.append("Reset device PPM toward -2.0 (044651 overshot to ~-7.88 via ±1250 rail).")
    if report["primary_failure_class"].startswith("FEED_GATE"):
        actions.append("Live VCWs exist but dup/reject starve feed — keep eye-lost escalate; watch worker-busy.")
    if (report.get("worker_dsp_ms") or {}).get("emit_p50", 0) and report["worker_dsp_ms"]["emit_p50"] > 150:
        actions.append("Emit-gate dsp p50>>budget — cooperative abort still open.")
    report["operator_actions"] = actions
    print("ACTIONS:")
    for a in actions:
        print(" -", a)

    json_out = args.json_out or (cap / "auto_forensic_report.json")
    json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("wrote", json_out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
