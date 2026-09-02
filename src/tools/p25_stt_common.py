#!/usr/bin/env python3
"""Shared STT helpers for P25 clear-audio automation.

Defaults match the proven AI clear-audio pipeline:
  min_chars=12, min_words=3, backend=SDR_TOWN_STT_BACKEND or "auto".

`auto` is resolved by scripts/stt_transcribe_wav.py. The diagnostics prefer the
project-local .venv-stt Python when it exists so CLI replay and GUI STT exercise
the same backend instead of silently falling back to a broken system Python.
Do not invent alternate pass thresholds here.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_MIN_CHARS = 12
DEFAULT_MIN_WORDS = 3
DEFAULT_TIMEOUT_S = 90.0
DEFAULT_BACKEND = "auto"


def default_repo() -> Path:
    return Path(__file__).resolve().parents[2]


def default_stt_backend() -> str:
    env = (os.environ.get("SDR_TOWN_STT_BACKEND") or "").strip()
    return env or DEFAULT_BACKEND


def default_stt_python(repo: Path | None = None) -> Path | str:
    env = (os.environ.get("SDR_TOWN_STT_PYTHON") or "").strip()
    if env:
        return env
    root = (repo or default_repo()).resolve()
    candidates = [
        root / ".venv-stt" / "Scripts" / "python.exe",
        root / ".venv-stt" / "bin" / "python",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return sys.executable


def count_transcript_words(transcript: str) -> list[str]:
    return re.findall(r"[A-Za-z0-9']+", transcript or "")


def classify_stt_transcript(
    transcript: str,
    *,
    min_chars: int = DEFAULT_MIN_CHARS,
    min_words: int = DEFAULT_MIN_WORDS,
) -> dict:
    text = (transcript or "").strip()
    words = count_transcript_words(text)
    chars = len(text)
    word_count = len(words)
    passed = chars >= min_chars and word_count >= min_words
    return {
        "pass": passed,
        "chars": chars,
        "words": word_count,
        "text": text,
        "transcript": text,
    }


def run_stt(
    wav_path: str | Path | None,
    backend: str | None = None,
    model: str | None = None,
    *,
    min_chars: int = DEFAULT_MIN_CHARS,
    min_words: int = DEFAULT_MIN_WORDS,
    timeout: float | None = None,
    timeout_s: float | None = None,
    repo: Path | None = None,
) -> dict:
    """Transcribe a WAV and score against pipeline STT thresholds.

    Returns a dict with at least: ok, pass, chars, words, text, transcript, error.
    """
    resolved_backend = (backend or default_stt_backend()).strip() or DEFAULT_BACKEND
    resolved_timeout = float(
        DEFAULT_TIMEOUT_S if timeout is None and timeout_s is None else (timeout if timeout is not None else timeout_s)
    )
    root = (repo or default_repo()).resolve()
    result = {
        "enabled": True,
        "ok": False,
        "pass": False,
        "backend": resolved_backend,
        "model": model,
        "python": None,
        "wav_path": str(wav_path) if wav_path else None,
        "transcript": "",
        "text": "",
        "chars": 0,
        "words": 0,
        "returncode": None,
        "error": None,
    }
    if not wav_path:
        result["error"] = "no_wav_path"
        return result
    path = Path(wav_path)
    if not path.is_absolute():
        path = (root / path).resolve()
    result["wav_path"] = str(path)
    if not path.is_file():
        result["error"] = "wav_missing"
        return result
    if path.stat().st_size <= 44:
        result["error"] = "wav_empty"
        return result

    script = root / "scripts" / "stt_transcribe_wav.py"
    if not script.is_file():
        result["error"] = f"stt_script_missing:{script}"
        return result

    env = os.environ.copy()
    if model:
        env["SDR_TOWN_WHISPER_MODEL"] = str(model)
    stt_python = default_stt_python(root)
    result["python"] = str(stt_python)

    try:
        proc = subprocess.run(
            [str(stt_python), str(script), "--backend", resolved_backend, str(path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=max(5.0, resolved_timeout),
            check=False,
            env=env,
        )
    except subprocess.TimeoutExpired:
        result["error"] = "stt_timeout"
        return result

    scored = classify_stt_transcript(
        proc.stdout or "",
        min_chars=min_chars,
        min_words=min_words,
    )
    result.update(
        {
            "ok": proc.returncode == 0,
            "pass": proc.returncode == 0 and bool(scored["pass"]),
            "transcript": scored["transcript"],
            "text": scored["text"],
            "chars": scored["chars"],
            "words": scored["words"],
            "returncode": proc.returncode,
            "stderr": (proc.stderr or "").strip()[-4000:],
        }
    )
    return result
