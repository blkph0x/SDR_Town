"""Verify independent IQ diagnostic units and malformed-input rejection."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import numpy as np
import soundfile as sf

with tempfile.TemporaryDirectory(prefix="rds-iq-check-") as root:
    root = Path(root)
    meta = root / "tone.sigmf-meta"
    data = meta.with_suffix(".sigmf-data")
    out = root / "mpx.wav"
    rate = 256000
    t = np.arange(rate // 5) / rate
    # 750 Hz peak deviation at 1 kHz: 0.01 in 75 kHz-normalized MPX.
    iq = np.exp(1j * .75 * np.sin(2 * np.pi * 1000 * t)).astype("<c8")
    iq.tofile(data)
    meta.write_text(json.dumps({"global": {"core:sample_rate": rate, "core:datatype": "cf32_le"}}))
    command = [sys.executable, "scripts/diagnose_rds_iq.py", str(meta), "--output", str(out)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30, check=True)
    report = json.loads(result.stdout)
    samples, actual_rate = sf.read(out)
    expected = np.diff(.75 * np.sin(2 * np.pi * 1000 * t)) * rate / (2 * np.pi * 75000)
    assert actual_rate == rate and len(samples) == len(iq) - 1, report
    assert np.max(np.abs(samples - expected)) < 1e-6, report
    # Compare both declared formats on exactly the same quantized IQ values.
    raw = np.stack((iq.real, iq.imag), axis=1)
    raw = np.clip(np.rint(raw * 100 + 127.5), 0, 255).astype('u1')
    raw.tofile(root / 'raw.cu8')
    q = ((raw[:, 0].astype(float) - 127.5) + 1j * (raw[:, 1].astype(float) - 127.5)) / 128
    q.astype('<c8').tofile(data)
    subprocess.run(command, capture_output=True, text=True, timeout=30, check=True)
    reference, _ = sf.read(out)
    meta.write_text(json.dumps({'global': {'core:sample_rate': rate, 'core:datatype': 'cu8'}}))
    raw_command = command + ['--data', str(root / 'raw.cu8')]
    subprocess.run(raw_command, capture_output=True, text=True, timeout=30, check=True)
    quantized, _ = sf.read(out)
    assert np.array_equal(reference, quantized)
    (root / 'raw.cu8').write_bytes(b'123')
    result = subprocess.run(raw_command, capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and 'incomplete complex' in result.stderr
    meta.write_text(json.dumps({'global': {'core:sample_rate': rate, 'core:datatype': 'cf32_le'}}))
    iq[0] = complex(float("nan"), 0)
    iq.tofile(data)
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and "Non-finite IQ" in result.stderr, result
    iq[:100].tofile(data)
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and "Capture too short" in result.stderr, result
    print("PASS: independent MPX units, sample count, sample rate, invalid IQ rejection")
