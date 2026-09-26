#!/usr/bin/env python3
"""DEC-0146: opt-in synthetic FM benchmark; no RF, network or application changes."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
from datetime import datetime, timezone


def parse_rows(output):
    rows = [json.loads(line[len("FM_BENCH "):]) for line in output.splitlines()
            if line.startswith("FM_BENCH ")]
    expected = {(mode, rate, placement, power)
                for mode in ("NFM", "WFM") for rate in (2048000, 2400000, 10000000)
                for placement in ("adjacent", "first_image") for power in (0, 20, 40)}
    keys = [(r["mode"], r["sampleRate"], r["placement"], r["blockerExcessDb"]) for r in rows]
    if len(rows) != 36 or set(keys) != expected:
        raise ValueError("Incomplete or duplicate benchmark matrix")
    for row in rows:
        for name in ("wantedGainDb", "blockerAudioRelativeDb", "differenceRelativeDb",
                     "processingUs", "realtimeRatio", "inputSamples", "audioSamples",
                     "channelizerUs", "discriminatorUs", "resamplerUs", "postAudioUs"):
            if not isinstance(row[name], (int, float)) or not math.isfinite(row[name]):
                raise ValueError(f"Invalid measurement: {name}")
        if row["processingUs"] <= 0 or row["audioSamples"] < 4800:
            raise ValueError("Invalid processing time or missing audio")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeat", type=int, choices=range(1, 11), default=3)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    repo = Path(__file__).resolve().parent.parent
    source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=repo, text=True).strip())
    report = {"schema": "sdr-town-fm-benchmark-v1", "utc": datetime.now(timezone.utc).isoformat(),
              "host": platform.platform(), "sourceHead": source, "sourceDirty": dirty,
              "executableSha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "scope": "synthetic IQ; no ADC overload model; demod-call time only; not SINAD or BER",
              "runs": []}
    for index in range(args.repeat):
        result = subprocess.run([str(exe), "[.fm-benchmark]"], cwd=repo,
                                capture_output=True, text=True, timeout=300)
        if result.returncode:
            raise RuntimeError(f"Benchmark failed: {result.stdout[-4000:]}\n{result.stderr[-1000:]}")
        report["runs"].append(parse_rows(result.stdout))
        print(f"Validated run {index + 1}/{args.repeat}: 36 measurements", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"Report: {args.output.resolve()}")


if __name__ == "__main__":
    main()
