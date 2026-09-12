#!/usr/bin/env python3
"""Harvest listen bars from a start/stop capture (DEC-0050).

For each recommended clear voicetest:
  1) Replay IQ → WAV (file path — usually good)
  2) Classify file WAV → CLEAR/GARBLED/SILENT
  3) If live_speaker.wav exists (start/stop sidecar), classify it too
  4) Write fixture JSON for regression + human triage

Live vs file mismatch is the whole point: file CLEAR + live SILENT/GARBLED
proves the bug is live path, not RF/mbelib.
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
import p25_pcm_listen_classify as listen  # noqa: E402


def default_exe() -> Path:
    return ROOT / "build" / "bin" / "Release" / "SDR_Town.exe"


def run_voicetest_wav(exe: Path, cmd: str, wav_path: Path, log_path: Path, timeout_s: float = 120.0) -> dict:
    cmd8 = re.sub(r"\b1800\b", "8000", cmd, count=1)
    if "clear" not in cmd8 and " enc" not in cmd8 and not cmd8.endswith(" enc"):
        if "phase2" in cmd8 and "clear" not in cmd8:
            cmd8 = cmd8 + " clear"
    if "stream" not in cmd8:
        cmd8 = cmd8 + " stream noprobe"
    # Force wav= output (strip any existing wav=)
    cmd8 = re.sub(r'\bwav="[^"]*"', "", cmd8)
    cmd8 = re.sub(r"\bwav=\S+", "", cmd8)
    cmd8 = cmd8.strip() + f' wav="{wav_path.as_posix()}"'
    full = [str(exe), "--cli", "--allow-multiple", "--cmd", cmd8]
    proc = subprocess.run(full, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout_s)
    log_path.write_text(proc.stdout or "", encoding="utf-8", errors="replace")
    m = re.search(
        r"P25 voicetest result=(\S+)\s+drop=(\S+).*?\bduty=([0-9.]+)",
        proc.stdout or "",
    )
    if not m:
        m = re.search(r"P25 voicetest result=(\S+).*?duty=([0-9.]+).*?drop=(\S+)", proc.stdout or "")
        if m:
            return {"cmd": cmd8, "result": m.group(1), "duty": float(m.group(2)), "drop": m.group(3), "rc": proc.returncode}
        return {"cmd": cmd8, "result": "NO_RESULT", "drop": None, "duty": None, "rc": proc.returncode}
    return {"cmd": cmd8, "result": m.group(1), "drop": m.group(2), "duty": float(m.group(3)), "rc": proc.returncode}


def find_live_speaker_wav(cap: Path) -> Path | None:
    for p in sorted(cap.glob("*_live_speaker.wav")):
        if p.is_file() and p.stat().st_size > 44:
            return p
    for p in sorted(cap.glob("*live*speaker*.wav")):
        if p.is_file() and p.stat().st_size > 44:
            return p
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("capture", type=Path)
    ap.add_argument("--exe", type=Path, default=None)
    ap.add_argument("--max-bars", type=int, default=4)
    ap.add_argument("--json-out", type=Path, default=None)
    args = ap.parse_args()

    cap = args.capture.resolve()
    exe = args.exe or default_exe()
    audited = audit.audit_capture(cap)
    cmds = [
        c
        for c in (audited.get("recommended_cli_commands") or [])
        if c.startswith("p25 voicetest") and " enc" not in c and not c.rstrip().endswith(" enc")
    ][: args.max_bars]

    out_dir = cap / "listen_fixtures"
    out_dir.mkdir(parents=True, exist_ok=True)
    live_wav = find_live_speaker_wav(cap)
    live_listen = None
    if live_wav:
        live_listen = listen.result_to_dict(listen.classify_wav(live_wav))
        print("LIVE_SPEAKER:", live_wav.name, live_listen["label"])

    bars = []
    for i, cmd in enumerate(cmds):
        wav = out_dir / f"file_bar_{i}.wav"
        log = out_dir / f"file_bar_{i}.txt"
        print(f"BAR[{i}]: {cmd[:90]}...")
        try:
            vt = run_voicetest_wav(exe, cmd, wav, log)
        except Exception as ex:  # pragma: no cover
            bars.append({"cmd": cmd, "error": str(ex)})
            continue
        file_listen = None
        if wav.is_file() and wav.stat().st_size > 44:
            file_listen = listen.result_to_dict(listen.classify_wav(wav))
        mismatch = None
        if live_listen and file_listen:
            if file_listen["label"] == "CLEAR" and live_listen["label"] in ("SILENT", "GARBLED"):
                mismatch = "LIVE_WORSE_THAN_FILE"
            elif live_listen["label"] == "CLEAR" and file_listen["label"] in ("SILENT", "GARBLED"):
                mismatch = "FILE_WORSE_THAN_LIVE"
        bar = {
            "cmd": vt.get("cmd", cmd),
            "voicetest": vt,
            "file_wav": str(wav) if wav.is_file() else None,
            "file_listen": file_listen,
            "live_listen": live_listen,
            "mismatch": mismatch,
            "golden_expect": (file_listen or {}).get("label"),
        }
        bars.append(bar)
        print(" ", vt.get("result"), "duty=", vt.get("duty"), "file_listen=", (file_listen or {}).get("label"), "mismatch=", mismatch)
        (out_dir / f"file_bar_{i}.json").write_text(json.dumps(bar, indent=2), encoding="utf-8")

    report = {
        "capture": str(cap),
        "live_speaker_wav": str(live_wav) if live_wav else None,
        "live_listen": live_listen,
        "bars": bars,
        "operator_actions": [],
    }
    if any(b.get("mismatch") == "LIVE_WORSE_THAN_FILE" for b in bars):
        report["operator_actions"].append(
            "File replay CLEAR but live speaker SILENT/GARBLED — live path bug (worker/eye/feed), not RF."
        )
    if live_wav is None:
        report["operator_actions"].append(
            "No *_live_speaker.wav — rebuild with DEC-0050 start/stop speaker dump, then re-capture."
        )

    json_out = args.json_out or (out_dir / "listen_harvest_report.json")
    json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("wrote", json_out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
