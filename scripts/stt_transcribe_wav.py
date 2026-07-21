#!/usr/bin/env python3
"""Offline WAV → text helper for SDR Town Decode Log (faster-whisper).

Usage:
  python scripts/stt_transcribe_wav.py path/to/segment.wav

Prints transcript text to stdout. Exit non-zero on failure.
Requires: pip install faster-whisper
Optional env:
  SDR_TOWN_WHISPER_MODEL   (default: base.en)
  SDR_TOWN_WHISPER_DEVICE  (default: cpu)
  SDR_TOWN_WHISPER_COMPUTE (default: int8)
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: stt_transcribe_wav.py <wav>", file=sys.stderr)
        return 2
    wav = Path(argv[1])
    if not wav.is_file():
        print(f"missing wav: {wav}", file=sys.stderr)
        return 2

    try:
        from faster_whisper import WhisperModel
    except Exception as ex:  # noqa: BLE001
        print(f"faster-whisper import failed: {ex}", file=sys.stderr)
        print("install with: pip install faster-whisper", file=sys.stderr)
        return 3

    model_name = os.environ.get("SDR_TOWN_WHISPER_MODEL", "base.en")
    device = os.environ.get("SDR_TOWN_WHISPER_DEVICE", "cpu")
    compute = os.environ.get("SDR_TOWN_WHISPER_COMPUTE", "int8")

    try:
        model = WhisperModel(model_name, device=device, compute_type=compute)
        segments, _info = model.transcribe(
            str(wav),
            language="en",
            vad_filter=True,
            beam_size=1,
            best_of=1,
        )
        parts = []
        for seg in segments:
            t = (seg.text or "").strip()
            if t:
                parts.append(t)
        text = " ".join(parts).strip()
        sys.stdout.write(text + ("\n" if text else ""))
        return 0
    except Exception as ex:  # noqa: BLE001
        print(f"transcribe failed: {ex}", file=sys.stderr)
        return 4


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
