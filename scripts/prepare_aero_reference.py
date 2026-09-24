#!/usr/bin/env python3
"""Offline reference preparation, not application DSP. Requires PyAV and NumPy.

Decode the pinned JAERO reference to 48 kHz real IF and analytic cf32_le IQ.
An upstream truncated Ogg tail is reported and never counted as decoded input.
"""
import argparse
import hashlib
import json
from pathlib import Path
import av
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--if-hz",type=float,default=8000,help="Known source audio IF center")
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
blocks = []
error = None
resampler = av.AudioResampler(format="s16", layout="mono", rate=48000)
try:
    with av.open(str(a.source)) as container:
        for frame in container.decode(audio=0):
            blocks.extend(f.to_ndarray().tobytes() for f in resampler.resample(frame))
except av.error.FFmpegError as e:
    error = str(e)
blocks.extend(f.to_ndarray().tobytes() for f in resampler.resample(None))
data = b"".join(blocks)
if not data:
    raise RuntimeError(f"No valid reference audio decoded: {error}")
(a.output / "if-s16le-48k.pcm").write_bytes(data)
x = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768
# Analytic-signal conversion only for the independent IF recording. Live complex
# IQ goes directly to the native channelizer. Preserve USB spectral orientation.
spectrum = np.fft.fft(x)
h = np.zeros(len(x))
h[0] = 1
h[1:(len(x)+1)//2] = 2
if len(x) % 2 == 0:
    h[len(x)//2] = 1
iq = np.fft.ifft(spectrum*h) * np.exp(-2j*np.pi*a.if_hz*np.arange(len(x))/48000)
(a.output / "reference.sigmf-data").write_bytes(iq.astype("<c8").tobytes())
# 1545 MHz is explicit synthetic capture metadata, NOT a claim about the original
# recording's RF center. The upstream file only contains real intermediate audio.
meta = {"global": {"core:datatype": "cf32_le", "core:sample_rate": 48000,
                   "core:description": "JAERO reference real IF converted to analytic IQ; RF center synthetic"},
        "captures": [{"core:sample_start": 0, "core:frequency": 1545000000}], "annotations": []}
(a.output / "reference.sigmf-meta").write_text(json.dumps(meta, indent=2))
report = {"sourceSha256": hashlib.sha256(a.source.read_bytes()).hexdigest(),
          "decodedSamples": len(x), "seconds": len(x)/48000, "containerError": error,"sourceIfHz":a.if_hz}
(a.output / "preparation.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
