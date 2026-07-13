#!/usr/bin/env python3
"""Summarize SDR Town IQ capture health from a capture directory or manifest row.

Usage:
  python tools/analyze_iq_capture.py /path/to/capture_dir

The script is intentionally dependency-free so it can run on tester machines.
"""
from __future__ import annotations
import csv
import json
import pathlib
import sys


def find_one(root: pathlib.Path, suffix: str) -> pathlib.Path | None:
    hits = sorted(root.glob(f"*{suffix}"))
    return hits[0] if hits else None


def load_json(path: pathlib.Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def summarize_ring_csv(path: pathlib.Path) -> dict:
    rows = 0
    total_gap = 0
    max_gap = 0
    total_appended = 0
    zero_appends = 0
    epoch_resets = 0
    epoch_reset_skipped_samples = 0
    last_total = 0
    last_bytes = 0
    with path.open("r", newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            rows += 1
            gap = int(float(row.get("gap_samples") or 0))
            appended = int(float(row.get("samples_appended") or 0))
            total_gap += gap
            max_gap = max(max_gap, gap)
            total_appended += appended
            if appended == 0:
                zero_appends += 1
            epoch_resets = max(epoch_resets, int(float(row.get("ring_epoch_resets") or epoch_resets)))
            epoch_reset_skipped_samples = max(
                epoch_reset_skipped_samples,
                int(float(row.get("ring_epoch_reset_skipped_samples") or epoch_reset_skipped_samples)),
            )
            last_total = int(float(row.get("total_written") or last_total))
            last_bytes = int(float(row.get("bytes_written") or last_bytes))
    return {
        "poll_rows": rows,
        "total_gap_samples": total_gap,
        "max_gap_samples": max_gap,
        "total_appended_samples": total_appended,
        "zero_append_polls": zero_appends,
        "ring_epoch_resets": epoch_resets,
        "ring_epoch_reset_skipped_samples": epoch_reset_skipped_samples,
        "last_total_samples": last_total,
        "last_bytes_written": last_bytes,
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    root = pathlib.Path(argv[1]).expanduser().resolve()
    if not root.exists():
        print(f"not found: {root}", file=sys.stderr)
        return 2
    if root.is_file():
        root = root.parent

    summary_path = find_one(root, "_summary.json")
    ring_path = find_one(root, "_ring_health.csv")
    meta_path = find_one(root, ".sigmf-meta")

    result: dict = {"capture_dir": str(root)}
    if summary_path:
        result["summary"] = load_json(summary_path)
    if meta_path:
        meta = load_json(meta_path)
        result["sigmf"] = {
            "sample_rate_hz": meta.get("global", {}).get("core:sample_rate"),
            "datatype": meta.get("global", {}).get("core:datatype"),
            "capture_type": meta.get("global", {}).get("sdrtown:capture_type"),
            "health": meta.get("global", {}).get("sdrtown:health"),
        }
    if ring_path:
        result["ring"] = summarize_ring_csv(ring_path)

    print(json.dumps(result, indent=2, sort_keys=True))
    health = (result.get("summary") or {}).get("health") or (result.get("sigmf") or {}).get("health")
    return 0 if health in (None, "ok_gapless") else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
