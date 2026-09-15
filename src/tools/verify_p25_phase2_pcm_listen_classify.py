#!/usr/bin/env python3
"""DEC-0050: live speaker WAV dump + listenclassify CLI wiring."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src" / "tools"))

from p25_orchestration_sources import orchestration_source_text


def main() -> int:
    text = orchestration_source_text()
    checks = [
        ("startLiveIqSpeakerWavCapture", "live speaker WAV start missing"),
        ("stopLiveIqSpeakerWavCapture", "live speaker WAV stop missing"),
        ("appendLiveIqSpeakerWavCapture", "live speaker WAV append missing"),
        ("_live_speaker.wav", "live_speaker.wav path token missing"),
        ("listenclassify", "CLI p25 listenclassify missing"),
        ("p25_pcm_listen_classify.py", "listen classifier script path missing"),
    ]
    fail = False
    for needle, msg in checks:
        if needle not in text:
            # Also search individual files for tools not in orchestration corpus
            print(f"WARN corpus miss: {msg} ({needle}) — checking files")
            fail_file = True
            for rel in (
                "src/MainWindow.cpp",
                "src/P25VoiceDecode.cpp",
                "src/P25VoiceTest.cpp",
                "src/CliApp.cpp",
                "include/P25VoiceTest.h",
            ):
                p = ROOT / rel
                if p.is_file() and needle in p.read_text(encoding="utf-8", errors="replace"):
                    fail_file = False
                    break
            if fail_file:
                print(f"FAIL: {msg}")
                fail = True

    script = ROOT / "src" / "tools" / "p25_pcm_listen_classify.py"
    harvester = ROOT / "src" / "tools" / "run_p25_listen_bar_harvester.py"
    if not script.is_file():
        print("FAIL: p25_pcm_listen_classify.py missing")
        fail = True
    if not harvester.is_file():
        print("FAIL: run_p25_listen_bar_harvester.py missing")
        fail = True

    # Classifier self-check
    import p25_pcm_listen_classify as listen

    if listen.classify_pcm(listen.synthesize_clear()).label != "CLEAR":
        print("FAIL: synth CLEAR")
        fail = True
    if listen.classify_pcm(listen.synthesize_garbled()).label != "GARBLED":
        print("FAIL: synth GARBLED")
        fail = True
    if listen.classify_pcm(listen.synthesize_silent()).label != "SILENT":
        print("FAIL: synth SILENT")
        fail = True

    if fail:
        return 1
    print("PASS: DEC-0050 live speaker WAV + listen classify wiring")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
