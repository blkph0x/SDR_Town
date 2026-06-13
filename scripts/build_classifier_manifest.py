#!/usr/bin/env python3
"""
Build SDR Town classifier train/val/test manifests from captured SigMF samples.

Usage:
  python scripts/build_classifier_manifest.py
  python scripts/build_classifier_manifest.py --root "C:/path/to/training_captures" --out "C:/path/to/manifests"
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from typing import Any


def default_capture_root() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / "SDR_Town" / "SDR Town" / "training_captures"
    return Path.cwd() / "training_captures"


def stable_split(key: str, val_pct: int, test_pct: int) -> str:
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()
    bucket = int(digest[:8], 16) % 100
    if bucket < test_pct:
        return "test"
    if bucket < test_pct + val_pct:
        return "val"
    return "train"


def read_json(path: Path) -> dict[str, Any] | None:
    try:
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else None
    except Exception:
        return None


def first_annotation(meta: dict[str, Any]) -> dict[str, Any]:
    anns = meta.get("annotations")
    if isinstance(anns, list) and anns and isinstance(anns[0], dict):
        return anns[0]
    return {}


def build_record(meta_path: Path, root: Path, split: str) -> dict[str, Any] | None:
    meta = read_json(meta_path)
    if not meta:
        return None

    base = meta_path.with_suffix("")
    if base.name.endswith(".sigmf"):
        base = base.with_name(base.name[:-6])
    data_path = meta_path.with_name(meta_path.name.replace(".sigmf-meta", ".sigmf-data"))
    tile_pgm = next(meta_path.parent.glob("*_tile.pgm"), None)
    tile_f32 = next(meta_path.parent.glob("*_tile.f32"), None)

    global_meta = meta.get("global", {}) if isinstance(meta.get("global"), dict) else {}
    ann = first_annotation(meta)
    label = ann.get("core:label") or global_meta.get("sdrtown:label") or "unknown"
    sample_rate = global_meta.get("core:sample_rate", 0)
    sample_count = ann.get("core:sample_count", 0)
    freq_lower = ann.get("core:freq_lower_edge")
    freq_upper = ann.get("core:freq_upper_edge")

    center_freq = None
    captures = meta.get("captures")
    if isinstance(captures, list) and captures and isinstance(captures[0], dict):
        center_freq = captures[0].get("core:frequency")

    rec = {
        "id": meta_path.parent.name,
        "split": split,
        "label": label,
        "class_hint": global_meta.get("sdrtown:classifier_label", "Unknown"),
        "classifier_confidence": global_meta.get("sdrtown:classifier_confidence", 0.0),
        "mode": global_meta.get("sdrtown:mode", "AUTO"),
        "sample_rate_hz": sample_rate,
        "sample_count": sample_count,
        "center_freq_hz": center_freq,
        "freq_lower_edge_hz": freq_lower,
        "freq_upper_edge_hz": freq_upper,
        "channel_bandwidth_hz": global_meta.get("sdrtown:channel_bandwidth_hz", 0),
        "standard_bandwidth_hz": global_meta.get("sdrtown:classifier_standard_bandwidth_hz", 0),
        "sigmf_meta": str(meta_path.relative_to(root)),
        "sigmf_data": str(data_path.relative_to(root)) if data_path.exists() else None,
        "tile_pgm": str(tile_pgm.relative_to(root)) if tile_pgm else None,
        "tile_f32": str(tile_f32.relative_to(root)) if tile_f32 else None,
        "valid": data_path.exists() and tile_f32 is not None and tile_pgm is not None,
    }
    return rec


def main() -> int:
    parser = argparse.ArgumentParser(description="Build SDR Town classifier manifests from SigMF captures.")
    parser.add_argument("--root", type=Path, default=default_capture_root(), help="training_captures folder")
    parser.add_argument("--out", type=Path, default=None, help="output folder for manifest JSONL files")
    parser.add_argument("--val-pct", type=int, default=15)
    parser.add_argument("--test-pct", type=int, default=15)
    args = parser.parse_args()

    root = args.root.expanduser().resolve()
    out_dir = (args.out.expanduser().resolve() if args.out else root / "manifests")
    out_dir.mkdir(parents=True, exist_ok=True)

    if not root.exists():
        print(f"Capture root does not exist: {root}")
        return 2

    records: list[dict[str, Any]] = []
    for meta_path in sorted(root.rglob("*.sigmf-meta")):
        split = stable_split(str(meta_path.parent.relative_to(root)), args.val_pct, args.test_pct)
        rec = build_record(meta_path, root, split)
        if rec:
            records.append(rec)

    split_files = {
        "train": out_dir / "train.jsonl",
        "val": out_dir / "val.jsonl",
        "test": out_dir / "test.jsonl",
        "all": out_dir / "all.jsonl",
    }
    handles = {k: p.open("w", encoding="utf-8") for k, p in split_files.items()}
    try:
        for rec in records:
            line = json.dumps(rec, sort_keys=True)
            handles["all"].write(line + "\n")
            handles[rec["split"]].write(line + "\n")
    finally:
        for h in handles.values():
            h.close()

    counts = {k: 0 for k in ("train", "val", "test")}
    valid = 0
    labels: dict[str, int] = {}
    for rec in records:
        counts[rec["split"]] += 1
        valid += 1 if rec.get("valid") else 0
        labels[rec["label"]] = labels.get(rec["label"], 0) + 1

    summary = {
        "root": str(root),
        "out": str(out_dir),
        "total": len(records),
        "valid": valid,
        "splits": counts,
        "labels": labels,
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
