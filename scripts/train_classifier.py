#!/usr/bin/env python3
"""
Train a compact SDR Town classifier from tile manifests and export ONNX.

This script is optional and requires PyTorch:
  python -m pip install torch

Workflow:
  python scripts/generate_synthetic_classifier_data.py --count 20
  python scripts/build_classifier_manifest.py
  python scripts/train_classifier.py --epochs 5
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import struct
from pathlib import Path
from typing import Any


def default_capture_root() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / "SDR_Town" / "SDR Town" / "training_captures"
    return Path.cwd() / "training_captures"


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows = []
    if not path.exists():
        return rows
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def read_tile(path: Path, width: int = 256, height: int = 256) -> list[float]:
    expected = width * height
    data = path.read_bytes()
    count = len(data) // 4
    vals = list(struct.unpack("<" + "f" * count, data[: count * 4]))
    if len(vals) < expected:
        vals.extend([0.0] * (expected - len(vals)))
    return vals[:expected]


def build_label_map(rows: list[dict[str, Any]]) -> dict[str, int]:
    labels = sorted({str(r.get("label", "unknown")) for r in rows})
    return {label: i for i, label in enumerate(labels)}


def import_torch():
    try:
        import torch
        import torch.nn as nn
        import torch.nn.functional as F
        from torch.utils.data import DataLoader, Dataset
        return torch, nn, F, DataLoader, Dataset
    except Exception as ex:
        raise SystemExit(
            "PyTorch is required for training/export.\n"
            "Install it first, then rerun this script.\n"
            "Example: python -m pip install torch\n"
            f"Import error: {ex}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Train/export SDR Town tile classifier.")
    parser.add_argument("--manifest-dir", type=Path, default=default_capture_root() / "manifests")
    parser.add_argument("--data-root", type=Path, default=None)
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--out", type=Path, default=default_capture_root() / "models" / "sdr_town_classifier.onnx")
    args = parser.parse_args()

    torch, nn, F, DataLoader, Dataset = import_torch()
    random.seed(args.seed)
    torch.manual_seed(args.seed)

    manifest_dir = args.manifest_dir.expanduser().resolve()
    data_root = (args.data_root.expanduser().resolve() if args.data_root else manifest_dir.parent)
    train_rows = [r for r in load_jsonl(manifest_dir / "train.jsonl") if r.get("valid", True)]
    val_rows = [r for r in load_jsonl(manifest_dir / "val.jsonl") if r.get("valid", True)]
    test_rows = [r for r in load_jsonl(manifest_dir / "test.jsonl") if r.get("valid", True)]
    all_rows = train_rows + val_rows + test_rows
    if not train_rows or not all_rows:
        raise SystemExit(f"No valid training rows found in {manifest_dir}. Build manifests first.")

    label_map = build_label_map(all_rows)

    class TileDataset(Dataset):
        def __init__(self, rows: list[dict[str, Any]]):
            self.rows = rows

        def __len__(self) -> int:
            return len(self.rows)

        def __getitem__(self, idx: int):
            row = self.rows[idx]
            tile_rel = row.get("tile_f32")
            if not tile_rel:
                raise RuntimeError(f"missing tile_f32 for {row.get('id')}")
            tile = read_tile(data_root / tile_rel)
            x = torch.tensor(tile, dtype=torch.float32).view(1, 256, 256)
            label = torch.tensor(label_map[str(row.get("label", "unknown"))], dtype=torch.long)
            bw = float(row.get("standard_bandwidth_hz") or row.get("channel_bandwidth_hz") or 12500.0)
            bw_log = torch.tensor(math.log(max(1.0, bw)), dtype=torch.float32)
            return x, label, bw_log

    class TinyClassifier(nn.Module):
        def __init__(self, classes: int):
            super().__init__()
            self.features = nn.Sequential(
                nn.Conv2d(1, 16, 5, stride=2, padding=2),
                nn.BatchNorm2d(16),
                nn.ReLU(inplace=True),
                nn.Conv2d(16, 32, 3, stride=2, padding=1),
                nn.BatchNorm2d(32),
                nn.ReLU(inplace=True),
                nn.Conv2d(32, 64, 3, stride=2, padding=1),
                nn.BatchNorm2d(64),
                nn.ReLU(inplace=True),
                nn.Conv2d(64, 96, 3, stride=2, padding=1),
                nn.BatchNorm2d(96),
                nn.ReLU(inplace=True),
                nn.AdaptiveAvgPool2d((1, 1)),
            )
            self.class_head = nn.Linear(96, classes)
            self.bw_head = nn.Linear(96, 1)

        def forward(self, x):
            z = self.features(x).flatten(1)
            return self.class_head(z), self.bw_head(z).squeeze(1)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = TinyClassifier(len(label_map)).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)

    train_loader = DataLoader(TileDataset(train_rows), batch_size=args.batch_size, shuffle=True)
    val_loader = DataLoader(TileDataset(val_rows or train_rows), batch_size=args.batch_size, shuffle=False)

    def run_epoch(loader, train: bool) -> tuple[float, float]:
        model.train(train)
        total_loss = 0.0
        correct = 0
        total = 0
        for x, y, bw_log in loader:
            x = x.to(device)
            y = y.to(device)
            bw_log = bw_log.to(device)
            with torch.set_grad_enabled(train):
                logits, pred_bw = model(x)
                loss_cls = F.cross_entropy(logits, y)
                loss_bw = F.smooth_l1_loss(pred_bw, bw_log)
                loss = loss_cls + 0.05 * loss_bw
                if train:
                    optimizer.zero_grad(set_to_none=True)
                    loss.backward()
                    optimizer.step()
            total_loss += float(loss.item()) * x.size(0)
            correct += int((logits.argmax(dim=1) == y).sum().item())
            total += x.size(0)
        return total_loss / max(1, total), correct / max(1, total)

    history = []
    for epoch in range(1, args.epochs + 1):
        train_loss, train_acc = run_epoch(train_loader, True)
        val_loss, val_acc = run_epoch(val_loader, False)
        row = {
            "epoch": epoch,
            "train_loss": train_loss,
            "train_acc": train_acc,
            "val_loss": val_loss,
            "val_acc": val_acc,
        }
        history.append(row)
        print(json.dumps(row, sort_keys=True))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.zeros(1, 1, 256, 256, dtype=torch.float32, device=device)
    model.eval()
    torch.onnx.export(
        model,
        dummy,
        str(args.out),
        input_names=["waterfall_roi"],
        output_names=["class_logits", "bandwidth_log_hz"],
        dynamic_axes={"waterfall_roi": {0: "batch"}, "class_logits": {0: "batch"}, "bandwidth_log_hz": {0: "batch"}},
        opset_version=17,
    )

    sidecar = {
        "model": str(args.out),
        "labels": label_map,
        "input": {"name": "waterfall_roi", "shape": [1, 1, 256, 256], "range": [0.0, 1.0]},
        "outputs": ["class_logits", "bandwidth_log_hz"],
        "history": history,
        "train_count": len(train_rows),
        "val_count": len(val_rows),
        "test_count": len(test_rows),
    }
    args.out.with_suffix(".json").write_text(json.dumps(sidecar, indent=2, sort_keys=True), encoding="utf-8")
    print(f"Exported ONNX: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
