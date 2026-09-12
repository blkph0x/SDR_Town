#!/usr/bin/env python3
"""PCM listen classifier: CLEAR / GARBLED / SILENT (DEC-0050).

Scores *decoded speaker PCM*, not RF SNR or CADENCE duty. Duty can pass while
speech is still blocky/garbled; this fills that gap for live vs file automation.

Frames are 20 ms (P25 Phase 2 AMBE frame). Features per active frame:
  RMS, zero-crossing rate, spectral flatness (Wiener entropy on |FFT|^2).
Labels:
  SILENT  — almost no energy / active frames
  GARBLED — energy present but noise-like (high flatness + high ZCR, weak envelope)
  CLEAR   — speech-like envelope modulation + moderate flatness on active frames

Orthogonal to drop A–E / voicetest duty / STT chars-words.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import wave
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Sequence


FRAME_MS = 20.0
DEFAULT_SR = 48000


@dataclass
class ListenMetrics:
    sample_rate: int
    samples: int
    seconds: float
    frames: int
    active_frames: int
    active_ratio: float
    peak: float
    rms: float
    median_flatness: float
    median_zcr: float
    envelope_cv: float
    longest_active_run_s: float


@dataclass
class ListenResult:
    label: str  # CLEAR | GARBLED | SILENT
    metrics: ListenMetrics
    reasons: list[str]


def _median(xs: Sequence[float]) -> float:
    if not xs:
        return 0.0
    ys = sorted(xs)
    mid = len(ys) // 2
    if len(ys) % 2:
        return ys[mid]
    return 0.5 * (ys[mid - 1] + ys[mid])


def _rms(frame: Sequence[float]) -> float:
    if not frame:
        return 0.0
    s = 0.0
    for x in frame:
        s += x * x
    return math.sqrt(s / len(frame))


def _zcr(frame: Sequence[float]) -> float:
    if len(frame) < 2:
        return 0.0
    crosses = 0
    prev = frame[0]
    for x in frame[1:]:
        if (prev >= 0.0) != (x >= 0.0) and (prev != 0.0 or x != 0.0):
            crosses += 1
        prev = x
    return crosses / (len(frame) - 1)


def _spectral_flatness(frame: Sequence[float]) -> float:
    """Wiener entropy on positive FFT bins; 1 ~= white, lower ~= tonal/speech."""
    n = len(frame)
    if n < 8:
        return 1.0
    # Real DFT magnitude via naive DFT on downsampled frame for speed/determinism.
    # 20 ms @ 48 kHz = 960; DFT all bins is heavy — use every 4th sample (12 kHz).
    step = 4 if n >= 64 else 1
    xs = [frame[i] for i in range(0, n, step)]
    m = len(xs)
    # Hann window
    if m > 1:
        xs = [xs[i] * (0.5 - 0.5 * math.cos(2.0 * math.pi * i / (m - 1))) for i in range(m)]
    half = m // 2
    powers: list[float] = []
    for k in range(1, max(2, half)):
        re = 0.0
        im = 0.0
        ang0 = 2.0 * math.pi * k / m
        for i, x in enumerate(xs):
            a = ang0 * i
            re += x * math.cos(a)
            im -= x * math.sin(a)
        p = re * re + im * im
        if p > 1e-20:
            powers.append(p)
    if len(powers) < 2:
        return 1.0
    log_sum = sum(math.log(p) for p in powers)
    geo = math.exp(log_sum / len(powers))
    arith = sum(powers) / len(powers)
    if arith <= 0.0:
        return 1.0
    return max(0.0, min(1.0, geo / arith))


def _parse_riff_wav(path: Path) -> tuple[list[float], int]:
    """Minimal RIFF reader: PCM16/24/32 + IEEE float32 (voicetest wav=)."""
    data = path.read_bytes()
    if len(data) < 44 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"not a RIFF/WAVE: {path}")
    pos = 12
    audio_format = None
    nch = 1
    sr = DEFAULT_SR
    bits = 16
    payload = b""
    while pos + 8 <= len(data):
        chunk_id = data[pos : pos + 4]
        chunk_size = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8
        chunk = data[pos : pos + chunk_size]
        pos += chunk_size + (chunk_size & 1)
        if chunk_id == b"fmt " and len(chunk) >= 16:
            audio_format, nch, sr, _br, _ba, bits = struct.unpack_from("<HHIIHH", chunk, 0)
        elif chunk_id == b"data":
            payload = chunk
    if audio_format is None or not payload:
        raise ValueError(f"missing fmt/data in {path}")
    samples: list[float]
    if audio_format == 1 and bits == 16:
        n = len(payload) // 2
        ints = struct.unpack("<" + "h" * n, payload[: n * 2])
        samples = [v / 32768.0 for v in ints]
    elif audio_format == 1 and bits == 32:
        n = len(payload) // 4
        ints = struct.unpack("<" + "i" * n, payload[: n * 4])
        samples = [v / 2147483648.0 for v in ints]
    elif audio_format == 1 and bits == 24:
        samples = []
        for i in range(0, len(payload) - 2, 3):
            v = int.from_bytes(payload[i : i + 3], "little", signed=True)
            samples.append(v / 8388608.0)
    elif audio_format == 3 and bits == 32:
        n = len(payload) // 4
        samples = list(struct.unpack("<" + "f" * n, payload[: n * 4]))
    else:
        raise ValueError(f"unsupported wav format={audio_format} bits={bits} in {path}")
    if nch > 1:
        mono = []
        for i in range(0, len(samples), nch):
            chunk = samples[i : i + nch]
            if chunk:
                mono.append(sum(chunk) / len(chunk))
        samples = mono
    return samples, int(sr)


def load_wav_mono_float(path: Path) -> tuple[list[float], int]:
    """Load mono WAV as float in [-1,1]. Supports PCM16 and float32."""
    path = Path(path)
    try:
        return _parse_riff_wav(path)
    except ValueError:
        # Fallback for odd PCM-only files
        with wave.open(str(path), "rb") as wf:
            nch = wf.getnchannels()
            sw = wf.getsampwidth()
            sr = wf.getframerate()
            raw = wf.readframes(wf.getnframes())
        if sw != 2:
            raise
        ints = struct.unpack("<" + "h" * (len(raw) // 2), raw)
        samples = [v / 32768.0 for v in ints]
        if nch > 1:
            samples = [
                sum(samples[i : i + nch]) / nch for i in range(0, len(samples), nch)
            ]
        return samples, sr

def classify_pcm(
    samples: Sequence[float],
    sample_rate: int = DEFAULT_SR,
    *,
    silence_peak: float = 0.002,
    silence_active_ratio: float = 0.06,
    active_rms: float = 0.008,
    garbled_flatness: float = 0.45,
    garbled_zcr: float = 0.28,
    clear_flatness_max: float = 0.38,
    clear_envelope_cv_min: float = 0.22,
    clear_active_ratio_min: float = 0.12,
    clear_run_s_min: float = 0.20,
) -> ListenResult:
    sr = int(sample_rate) if sample_rate and sample_rate > 0 else DEFAULT_SR
    n = len(samples)
    seconds = n / float(sr) if sr else 0.0
    frame_n = max(1, int(round(sr * FRAME_MS / 1000.0)))
    reasons: list[str] = []

    if n == 0:
        metrics = ListenMetrics(sr, 0, 0.0, 0, 0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0)
        return ListenResult("SILENT", metrics, ["empty_pcm"])

    peak = max(abs(x) for x in samples)
    global_rms = _rms(samples)

    flatnesses: list[float] = []
    zcrs: list[float] = []
    active_rms_list: list[float] = []
    active_flags: list[bool] = []

    for i in range(0, n - frame_n + 1, frame_n):
        frame = samples[i : i + frame_n]
        r = _rms(frame)
        active = r >= active_rms
        active_flags.append(active)
        if active:
            active_rms_list.append(r)
            flatnesses.append(_spectral_flatness(frame))
            zcrs.append(_zcr(frame))

    frames = len(active_flags)
    active_frames = sum(1 for a in active_flags if a)
    active_ratio = (active_frames / frames) if frames else 0.0
    med_flat = _median(flatnesses)
    med_zcr = _median(zcrs)

    # Envelope CV over active-frame RMS (speech has syllable modulation).
    if len(active_rms_list) >= 3:
        mean_r = sum(active_rms_list) / len(active_rms_list)
        var = sum((r - mean_r) ** 2 for r in active_rms_list) / len(active_rms_list)
        envelope_cv = math.sqrt(var) / mean_r if mean_r > 1e-12 else 0.0
    else:
        envelope_cv = 0.0

    longest = 0
    run = 0
    for a in active_flags:
        if a:
            run += 1
            longest = max(longest, run)
        else:
            run = 0
    longest_s = longest * FRAME_MS / 1000.0

    metrics = ListenMetrics(
        sample_rate=sr,
        samples=n,
        seconds=seconds,
        frames=frames,
        active_frames=active_frames,
        active_ratio=active_ratio,
        peak=peak,
        rms=global_rms,
        median_flatness=med_flat,
        median_zcr=med_zcr,
        envelope_cv=envelope_cv,
        longest_active_run_s=longest_s,
    )

    if peak < silence_peak or active_ratio < silence_active_ratio or active_frames < 2:
        reasons.append(f"peak={peak:.5f}")
        reasons.append(f"active_ratio={active_ratio:.3f}")
        return ListenResult("SILENT", metrics, reasons)

    noise_like = med_flat >= garbled_flatness and med_zcr >= garbled_zcr
    speech_like = (
        med_flat <= clear_flatness_max
        and envelope_cv >= clear_envelope_cv_min
        and active_ratio >= clear_active_ratio_min
        and longest_s >= clear_run_s_min
    )

    if speech_like and not noise_like:
        reasons.append(f"flatness={med_flat:.3f}")
        reasons.append(f"envelope_cv={envelope_cv:.3f}")
        reasons.append(f"active_run={longest_s:.2f}s")
        return ListenResult("CLEAR", metrics, reasons)

    reasons.append(f"flatness={med_flat:.3f}")
    reasons.append(f"zcr={med_zcr:.3f}")
    reasons.append(f"envelope_cv={envelope_cv:.3f}")
    if noise_like or not speech_like:
        return ListenResult("GARBLED", metrics, reasons)
    return ListenResult("GARBLED", metrics, reasons)


def classify_wav(path: Path | str) -> ListenResult:
    samples, sr = load_wav_mono_float(Path(path))
    return classify_pcm(samples, sr)


def result_to_dict(result: ListenResult) -> dict:
    d = asdict(result.metrics)
    return {"label": result.label, "reasons": result.reasons, "metrics": d}


def synthesize_clear(seconds: float = 1.2, sr: int = DEFAULT_SR) -> list[float]:
    """AM-modulated multi-tone (speech-like envelope + low flatness)."""
    n = int(sr * seconds)
    out = [0.0] * n
    for i in range(n):
        t = i / sr
        # Stronger syllable AM + periodic near-gaps (speech-like envelope CV).
        env = 0.15 + 0.85 * (0.5 + 0.5 * math.sin(2.0 * math.pi * 4.0 * t))
        if (t * 4.0) % 1.0 < 0.18:
            env *= 0.05
        s = (
            0.55 * math.sin(2.0 * math.pi * 220.0 * t)
            + 0.30 * math.sin(2.0 * math.pi * 440.0 * t)
            + 0.15 * math.sin(2.0 * math.pi * 880.0 * t)
        )
        out[i] = 0.28 * env * s
    return out


def synthesize_garbled(seconds: float = 1.2, sr: int = DEFAULT_SR) -> list[float]:
    """Dense noise (high flatness / ZCR) at speech-like level."""
    n = int(sr * seconds)
    # Deterministic LCG noise
    x = 0xC0FFEE
    out = [0.0] * n
    for i in range(n):
        x = (1103515245 * x + 12345) & 0x7FFFFFFF
        out[i] = ((x / 0x7FFFFFFF) * 2.0 - 1.0) * 0.12
    return out


def synthesize_silent(seconds: float = 1.0, sr: int = DEFAULT_SR) -> list[float]:
    n = int(sr * seconds)
    x = 1
    out = [0.0] * n
    for i in range(n):
        x = (1103515245 * x + 12345) & 0x7FFFFFFF
        out[i] = ((x / 0x7FFFFFFF) * 2.0 - 1.0) * 0.0002
    return out


def write_wav_pcm16(path: Path, samples: Sequence[float], sr: int = DEFAULT_SR) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        frames = b"".join(
            struct.pack("<h", max(-32767, min(32767, int(round(max(-1.0, min(1.0, s)) * 32767.0)))))
            for s in samples
        )
        wf.writeframes(frames)


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("wav", type=Path, nargs="?", help="WAV to classify")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--expect", choices=("CLEAR", "GARBLED", "SILENT"), default=None)
    ap.add_argument("--write-synth", type=Path, default=None, help="Write synth CLEAR/GARBLED/SILENT WAVs under dir")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.write_synth:
        root = args.write_synth
        write_wav_pcm16(root / "synth_clear.wav", synthesize_clear())
        write_wav_pcm16(root / "synth_garbled.wav", synthesize_garbled())
        write_wav_pcm16(root / "synth_silent.wav", synthesize_silent())
        print("wrote synth wavs under", root)
        if not args.wav:
            return 0

    if not args.wav:
        ap.error("wav path required (or use --write-synth alone)")

    result = classify_wav(args.wav)
    if args.json:
        print(json.dumps(result_to_dict(result), indent=2))
    else:
        m = result.metrics
        print(
            f"P25 listenclassify label={result.label} "
            f"seconds={m.seconds:.3f} active_ratio={m.active_ratio:.3f} "
            f"flatness={m.median_flatness:.3f} zcr={m.median_zcr:.3f} "
            f"envelope_cv={m.envelope_cv:.3f} peak={m.peak:.4f} "
            f"reasons={','.join(result.reasons)}"
        )
    if args.expect and result.label != args.expect:
        print(f"EXPECT FAIL: got {result.label} want {args.expect}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
