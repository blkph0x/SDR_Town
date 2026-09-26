"""DEC-0151 offline WFM filter oracle. Requires NumPy; never changes the app."""
import argparse
import json
import math
import hashlib
import subprocess
from pathlib import Path
import numpy as np


def taps(rate, bandwidth, count=None):
    fc = bandwidth / (2 * rate)
    legacy = count is None
    if legacy:
        count = 2 * min(math.ceil(3.3 / (fc * .5) + 1), 160) + 1
    if count < 3 or count % 2 == 0:
        raise ValueError("Odd FIR length >=3 required")
    half = (count - 1) // 2
    m = np.arange(count) - half
    window = np.kaiser(count, .1102 * ((60 if legacy else 80) - 8.7))
    if legacy:
        window[0] = window[-1] = 0  # Demod.cpp uses abs(arg)<1.
    h = (window * 2 * fc * np.sinc(2 * fc * m)).astype(np.float32)
    # C++ stores each coefficient as float, accumulates its sum in double.
    h /= np.float32(h.sum(dtype=np.float64))
    return h


def causal_filter(x, h):
    size = 1 << (len(x) + len(h) - 2).bit_length()
    return np.fft.ifft(np.fft.fft(x, size) * np.fft.fft(h, size))[:len(x)]


def db(amplitude):
    return float(20 * np.log10(max(float(amplitude), 1e-15)))


def rms(x):
    return float(np.sqrt(np.mean(np.abs(x) ** 2)))


def discriminator(x, rate):
    return np.angle(x[1:] * np.conj(x[:-1])) * rate / (2 * np.pi * 75000)


def measure(rate, bandwidth, deviation, side, count):
    factor = max(1, math.floor(rate / max(192000, bandwidth * 1.1) + .5))
    actual = rate / factor
    offset = side * (actual - 30000)
    time = np.arange(round(.2 * rate)) / rate
    wanted = np.exp(1j * np.cumsum(2 * np.pi * deviation * np.sin(2 * np.pi * 900 * time) / rate))
    blocker = np.exp(1j * np.cumsum(2 * np.pi * (offset + deviation * np.sin(2 * np.pi * 1700 * time)) / rate))
    h = taps(rate, bandwidth, count)
    a = causal_filter(wanted, h)
    b = causal_filter(blocker, h)
    start = round(.1 * rate)
    leakage = db(100 * rms(b[start:]) / rms(a[start:]))
    clean = discriminator(a[::factor], actual)
    mixed = discriminator((a + 100 * b)[::factor], actual)
    delay = (len(h) - 1) // 2
    positions = np.arange(0, len(wanted), factor)
    valid = positions >= start
    positions = positions[valid]
    ideal = discriminator(wanted[positions - delay], actual)
    # Discriminator index k describes retained samples k and k+1.
    at = int(np.flatnonzero(valid)[0])
    n = len(ideal)
    clean = clean[at:at+n]
    mixed = mixed[at:at+n]
    return dict(sampleRate=rate, bandwidthHz=bandwidth, deviationHz=deviation,
                offsetHz=offset, taps=len(h), actualRate=actual, delayUs=delay/rate*1e6,
                blockerResidualRelativeDb=leakage,
                mixedDiscriminatorErrorDb=db(rms(mixed-clean)/rms(clean)),
                cleanDiscriminatorErrorDb=db(rms(clean-ideal)/rms(ideal)),
                fullRateMacsPerSecond=rate*len(h),
                retainedMacsPerSecond=actual*len(h))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    rows = []
    for rate in (2400000, 10000000):
        for deviation in (50000, 75000):
            for side in (-1, 1):
                for count in (None, 2049, 4097):
                    row = measure(rate, 180000, deviation, side, count)
                    if not all(math.isfinite(v) for v in row.values()):
                        raise ValueError('Nonfinite measurement')
                    rows.append(row)
                    print(json.dumps(row, allow_nan=False), flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parent.parent
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=repo, text=True).strip())
    args.output.write_text(json.dumps(dict(schema='wfm-filter-oracle-v1',
        scope='Offline double-precision convolution; not app PCM or CPU timing',
        sourceHead=source, sourceDirty=dirty,
        scriptSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        numpyVersion=np.__version__, rows=rows), indent=2, allow_nan=False)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
