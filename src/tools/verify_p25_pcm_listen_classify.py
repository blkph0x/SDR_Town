#!/usr/bin/env python3
"""DEC-0050: PCM listen classifier regression (CLEAR / GARBLED / SILENT)."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src" / "tools"))

import p25_pcm_listen_classify as listen  # noqa: E402


def main() -> int:
    clear = listen.classify_pcm(listen.synthesize_clear())
    garbled = listen.classify_pcm(listen.synthesize_garbled())
    silent = listen.classify_pcm(listen.synthesize_silent())

    ok = True
    for label, result in (("CLEAR", clear), ("GARBLED", garbled), ("SILENT", silent)):
        if result.label != label:
            print(f"FAIL: synth {label} got {result.label} metrics={result.metrics}")
            ok = False
        else:
            print(f"PASS: synth {label} reasons={result.reasons}")

    # Duty-like energy on garbled must NOT be CLEAR
    if garbled.metrics.active_ratio >= 0.65 and garbled.label == "CLEAR":
        print("FAIL: high-duty noise must not classify CLEAR")
        ok = False

    # Empty
    empty = listen.classify_pcm([])
    if empty.label != "SILENT":
        print("FAIL: empty pcm should be SILENT")
        ok = False

    # Script path exists for CLI/forensic wiring
    script = ROOT / "src" / "tools" / "p25_pcm_listen_classify.py"
    if not script.is_file():
        print("FAIL: classifier script missing")
        ok = False

    if not ok:
        return 1
    print("P25 pcm listen classifier regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
