#!/usr/bin/env python3
"""
Generate synthetic SDR Town classifier training captures.

This is not a replacement for real captures. It creates clean, labeled bootstrap
examples in the same SigMF + normalized tile layout produced by the app.

Usage:
  python scripts/generate_synthetic_classifier_data.py --out "%APPDATA%/SDR_Town/SDR Town/training_captures/synthetic" --count 20
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import struct
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


CLASSES = [
    "noise_or_unknown",
    "wfm_broadcast",
    "nfm_voice_12k5",
    "nfm_voice_25k",
    "am_voice_20k",
    "usb_voice",
    "lsb_voice",
    "cw",
    "p25_c4fm",
    "dmr_4fsk",
    "other_digital_fsk",
]


CLASS_SETTINGS = {
    "noise_or_unknown": ("Unknown", "AUTO", 25000.0, 3000.0),
    "wfm_broadcast": ("WFM Broadcast", "WFM", 180000.0, 15000.0),
    "nfm_voice_12k5": ("NFM", "NFM", 12500.0, 3000.0),
    "nfm_voice_25k": ("NFM", "NFM", 25000.0, 4500.0),
    "am_voice_20k": ("AM", "AM", 20000.0, 9000.0),
    "usb_voice": ("USB", "USB", 3000.0, 3000.0),
    "lsb_voice": ("LSB", "LSB", 3000.0, 3000.0),
    "cw": ("CW", "USB", 800.0, 900.0),
    "p25_c4fm": ("P25 Phase 1 / C4FM", "NFM", 12500.0, 6000.0),
    "dmr_4fsk": ("Digital FSK", "NFM", 12500.0, 6000.0),
    "other_digital_fsk": ("Digital FSK", "NFM", 9600.0, 4800.0),
}


def default_out_root() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / "SDR_Town" / "SDR Town" / "training_captures" / "synthetic"
    return Path.cwd() / "training_captures" / "synthetic"


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_%f")[:-3]


def sanitize(s: str) -> str:
    out = []
    for ch in s:
        out.append(ch if ch.isalnum() or ch in "-_" else "_")
    return "".join(out).strip("_") or "unknown"


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def write_cf32(path: Path, iq: Iterable[complex]) -> int:
    count = 0
    with path.open("wb") as f:
        for s in iq:
            f.write(struct.pack("<ff", float(s.real), float(s.imag)))
            count += 1
    return count


def noise(rng: random.Random, scale: float = 0.03) -> complex:
    return complex(rng.gauss(0.0, scale), rng.gauss(0.0, scale))


def gen_iq(label: str, rng: random.Random, sample_rate: float, samples: int) -> list[complex]:
    out: list[complex] = []
    phase = rng.random() * math.tau
    amp = rng.uniform(0.35, 0.85)

    if label == "noise_or_unknown":
        return [noise(rng, 0.08) for _ in range(samples)]

    if label in {"p25_c4fm", "dmr_4fsk", "other_digital_fsk"}:
        symbol_rate = 4800.0 if label != "dmr_4fsk" else 4800.0
        levels = [-1.5, -0.5, 0.5, 1.5]
        dev = 2400.0 if label != "other_digital_fsk" else 1800.0
        sps = max(1, int(sample_rate / symbol_rate))
        current = rng.choice(levels)
        for i in range(samples):
            if i % sps == 0:
                current = rng.choice(levels)
            phase += math.tau * (current * dev) / sample_rate
            out.append(amp * complex(math.cos(phase), math.sin(phase)) + noise(rng, 0.025))
        return out

    if label.startswith("nfm_voice"):
        dev = 2500.0 if "12k5" in label else 5000.0
        tone1 = rng.uniform(500.0, 1500.0)
        tone2 = rng.uniform(1700.0, 2600.0)
        for i in range(samples):
            t = i / sample_rate
            audio = 0.65 * math.sin(math.tau * tone1 * t) + 0.30 * math.sin(math.tau * tone2 * t)
            phase += math.tau * dev * audio / sample_rate
            out.append(amp * complex(math.cos(phase), math.sin(phase)) + noise(rng, 0.025))
        return out

    if label == "wfm_broadcast":
        dev = 75000.0
        tones = [rng.uniform(300.0, 2500.0), rng.uniform(3000.0, 9000.0)]
        for i in range(samples):
            t = i / sample_rate
            audio = 0.55 * math.sin(math.tau * tones[0] * t) + 0.25 * math.sin(math.tau * tones[1] * t)
            phase += math.tau * dev * audio / sample_rate
            out.append(amp * complex(math.cos(phase), math.sin(phase)) + noise(rng, 0.025))
        return out

    if label == "am_voice_20k":
        audio_hz = rng.uniform(700.0, 2400.0)
        depth = rng.uniform(0.35, 0.85)
        for i in range(samples):
            t = i / sample_rate
            env = amp * (1.0 + depth * math.sin(math.tau * audio_hz * t))
            out.append(complex(env, 0.0) + noise(rng, 0.025))
        return out

    if label in {"usb_voice", "lsb_voice"}:
        sign = 1.0 if label == "usb_voice" else -1.0
        tones = [rng.uniform(400.0, 1100.0), rng.uniform(1400.0, 2600.0)]
        for i in range(samples):
            t = i / sample_rate
            sig = sum(math.sin(math.tau * sign * tone * t + rng.random() * 0.01) for tone in tones)
            q = sum(math.cos(math.tau * sign * tone * t) for tone in tones)
            out.append(complex(0.28 * sig, 0.28 * q) + noise(rng, 0.025))
        return out

    if label == "cw":
        tone = rng.uniform(450.0, 900.0)
        for i in range(samples):
            phase += math.tau * tone / sample_rate
            out.append(amp * complex(math.cos(phase), math.sin(phase)) + noise(rng, 0.018))
        return out

    return [noise(rng, 0.06) for _ in range(samples)]


def paint_tile(label: str, rng: random.Random, width: int = 256, height: int = 256) -> list[float]:
    pixels = [clamp(rng.gauss(0.08, 0.035), 0.0, 1.0) for _ in range(width * height)]

    def add_band(center: float, half: float, level: float, jitter: float = 0.04) -> None:
        for y in range(height):
            fade = clamp(rng.gauss(1.0, 0.06), 0.6, 1.0)
            c = center + rng.gauss(0.0, jitter)
            for x in range(width):
                pos = (x + 0.5) / width
                dist = abs(pos - c)
                if dist <= half:
                    edge = 1.0 - (dist / max(half, 1e-6)) ** 2
                    idx = y * width + x
                    pixels[idx] = max(pixels[idx], clamp(level * fade * (0.65 + 0.35 * edge), 0.0, 1.0))

    if label == "noise_or_unknown":
        return pixels
    if label == "wfm_broadcast":
        add_band(0.5, 0.36, 0.78, 0.015)
    elif label == "am_voice_20k":
        add_band(0.5, 0.012, 0.95, 0.002)
        add_band(0.43, 0.045, 0.55, 0.006)
        add_band(0.57, 0.045, 0.55, 0.006)
    elif label.startswith("nfm_voice"):
        add_band(0.5, 0.08 if "25k" in label else 0.045, 0.68, 0.01)
        add_band(0.5, 0.012, 0.80, 0.004)
    elif label in {"p25_c4fm", "dmr_4fsk", "other_digital_fsk"}:
        add_band(0.5, 0.055, 0.72, 0.006)
    elif label == "cw":
        add_band(0.5, 0.008, 0.92, 0.002)
    elif label == "usb_voice":
        add_band(0.56, 0.055, 0.67, 0.008)
    elif label == "lsb_voice":
        add_band(0.44, 0.055, 0.67, 0.008)
    return pixels


def write_tile(out_dir: Path, base_name: str, pixels: list[float], width: int = 256, height: int = 256) -> tuple[Path, Path]:
    pgm = out_dir / f"{base_name}_tile.pgm"
    f32 = out_dir / f"{base_name}_tile.f32"
    with pgm.open("wb") as f:
        f.write(f"P5\n{width} {height}\n255\n".encode("ascii"))
        f.write(bytes(int(clamp(v, 0.0, 1.0) * 255.0) for v in pixels))
    with f32.open("wb") as f:
        for v in pixels:
            f.write(struct.pack("<f", float(clamp(v, 0.0, 1.0))))
    return pgm, f32


def write_capture(root: Path, label: str, idx: int, rng: random.Random, sample_rate: float, samples: int) -> dict:
    class_hint, mode, bw_hz, lpf_hz = CLASS_SETTINGS[label]
    tuned_hz = 100_000_000.0 + rng.uniform(-100_000.0, 100_000.0)
    center_hz = tuned_hz
    stamp = utc_stamp()
    base_name = f"{stamp}_{sanitize(label)}_{idx:05d}"
    out_dir = root / base_name
    out_dir.mkdir(parents=True, exist_ok=True)

    iq = gen_iq(label, rng, sample_rate, samples)
    data_path = out_dir / f"{base_name}.sigmf-data"
    sample_count = write_cf32(data_path, iq)

    tile_pixels = paint_tile(label, rng)
    pgm_path, f32_path = write_tile(out_dir, base_name, tile_pixels)

    now = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    meta = {
        "global": {
            "core:datatype": "cf32_le",
            "core:sample_rate": sample_rate,
            "core:version": "1.2.0",
            "core:description": "SDR Town synthetic classifier bootstrap sample",
            "core:recorder": "scripts/generate_synthetic_classifier_data.py",
            "sdrtown:synthetic": True,
            "sdrtown:label": label,
            "sdrtown:mode": mode,
            "sdrtown:channel_bandwidth_hz": bw_hz,
            "sdrtown:audio_lpf_hz": lpf_hz,
            "sdrtown:audio_lpf_enabled": label not in {"p25_c4fm", "dmr_4fsk", "other_digital_fsk"},
            "sdrtown:squelch_db": -80.0,
            "sdrtown:classifier_label": class_hint,
            "sdrtown:classifier_confidence": 1.0,
            "sdrtown:classifier_reason": "synthetic labeled bootstrap sample",
            "sdrtown:classifier_filter": "Root-raised cosine" if label == "p25_c4fm" else "Low-pass",
            "sdrtown:classifier_standard_bandwidth_hz": bw_hz,
        },
        "captures": [
            {
                "core:sample_start": 0,
                "core:frequency": center_hz,
                "core:datetime": now,
            }
        ],
        "annotations": [
            {
                "core:sample_start": 0,
                "core:sample_count": sample_count,
                "core:freq_lower_edge": tuned_hz - bw_hz * 0.5,
                "core:freq_upper_edge": tuned_hz + bw_hz * 0.5,
                "core:label": label,
                "sdrtown:classifier_label": class_hint,
                "sdrtown:estimated_bandwidth_hz": bw_hz,
                "sdrtown:standard_bandwidth_hz": bw_hz,
                "sdrtown:snr_db": 30.0,
            }
        ],
        "sdrtown:artifacts": {
            "tile_preview_pgm": pgm_path.name,
            "tile_f32": f32_path.name,
            "tile_width": 256,
            "tile_height": 256,
            "tile_min_db": -120.0,
            "tile_max_db": -20.0,
        },
    }
    meta_path = out_dir / f"{base_name}.sigmf-meta"
    meta_path.write_text(json.dumps(meta, indent=2, sort_keys=True), encoding="utf-8")

    return {
        "created_utc": now,
        "label": label,
        "freq_hz": tuned_hz,
        "sample_rate_hz": sample_rate,
        "sample_count": sample_count,
        "meta": meta_path.name,
        "data": data_path.name,
        "directory": str(out_dir),
        "classifier_label": class_hint,
        "classifier_confidence": 1.0,
        "synthetic": True,
    }


def parse_classes(text: str) -> list[str]:
    if text.strip().lower() in {"all", "*"}:
        return list(CLASSES)
    out = [x.strip() for x in text.split(",") if x.strip()]
    bad = [x for x in out if x not in CLASSES]
    if bad:
        raise SystemExit(f"Unknown classes: {', '.join(bad)}\nKnown: {', '.join(CLASSES)}")
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate synthetic SDR Town classifier captures.")
    parser.add_argument("--out", type=Path, default=default_out_root(), help="output folder")
    parser.add_argument("--count", type=int, default=10, help="samples per class")
    parser.add_argument("--classes", default="all", help="comma-separated class list or all")
    parser.add_argument("--sample-rate", type=float, default=480000.0)
    parser.add_argument("--samples", type=int, default=65536)
    parser.add_argument("--seed", type=int, default=1337)
    args = parser.parse_args()

    classes = parse_classes(args.classes)
    root = args.out.expanduser().resolve()
    root.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)

    rows = []
    for label in classes:
        for i in range(args.count):
            rows.append(write_capture(root, label, i, rng, args.sample_rate, args.samples))

    manifest = root / "manifest.jsonl"
    with manifest.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, sort_keys=True) + "\n")

    summary = {
        "root": str(root),
        "classes": classes,
        "count_per_class": args.count,
        "total": len(rows),
        "sample_rate_hz": args.sample_rate,
        "samples_per_capture": args.samples,
        "manifest": str(manifest),
    }
    (root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
