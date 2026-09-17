"""Bounded offline RF/MPX inspection; not a replacement live demodulator.

Uses a whole-record, zero-phase rectangular FFT channel filter for an independent
diagnostic. Edge transients and ideal stopband differ from the application's FIR.
Output is raw 75 kHz-normalized MPX, never speaker audio or an RF quality claim.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import soundfile as sf


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("meta", type=Path)
    p.add_argument("--data", type=Path, help="Explicit sample file; defaults to matching .sigmf-data")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--bandwidth", type=float, default=180000)
    p.add_argument("--seconds", type=float, default=5)
    args = p.parse_args()
    meta = json.loads(args.meta.read_text(encoding="utf-8"))
    rate = float(meta["global"]["core:sample_rate"])
    datatype = meta["global"]["core:datatype"]
    if datatype not in ("cf32_le", "cu8"):
        p.error("Only cf32_le and cu8 are supported")
    if not 128000 <= rate <= 3000000 or not 0 < args.seconds <= 10:
        p.error("Require 128 kHz..3 MHz and 0 < seconds <= 10")
    factor = max(1, int(rate / 256000))
    out_rate = rate / factor
    if out_rate != round(out_rate) or not 0 < args.bandwidth < out_rate:
        p.error("Require integer MPX rate and bandwidth below its sample rate")
    path = args.data or args.meta.with_suffix(".sigmf-data")
    bytes_per_sample = 8 if datatype == "cf32_le" else 2
    if not path.stat().st_size or path.stat().st_size % bytes_per_sample:
        p.error("Empty or incomplete complex sample file")
    iq = np.memmap(path, dtype="<c8" if datatype == "cf32_le" else "u1", mode="r")
    if datatype == "cu8":
        raw = iq[:int(rate * args.seconds) * 2].reshape(-1, 2).astype(np.float32)
        iq = ((raw[:, 0] - 127.5) + 1j * (raw[:, 1] - 127.5)) / 128
    count = min(len(iq), int(rate * args.seconds))
    if (count + factor - 1) // factor - 1 < 16384:
        p.error("Capture too short for one MPX analysis window")
    x = np.asarray(iq[:count])
    if not np.isfinite(x).all():
        p.error("Non-finite IQ")
    fft = np.fft.fft(x)
    freq = np.fft.fftfreq(count, 1 / rate)
    filtered = np.fft.ifft(fft * (np.abs(freq) <= args.bandwidth / 2))[::factor]
    mpx = np.angle(filtered[1:] * np.conj(filtered[:-1])) * out_rate / (2 * np.pi * 75000)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(args.output), mpx, round(out_rate), subtype="FLOAT")
    # Relative spectral levels, not calibrated RF SNR or proof of RDS presence.
    n = 16384
    pieces = mpx[:len(mpx) // n * n].reshape(-1, n)
    power = np.mean(np.abs(np.fft.rfft(pieces * np.hanning(n), axis=1)) ** 2, axis=0)
    hz = np.fft.rfftfreq(n, 1 / out_rate)
    def level(lo, hi):
        return float(10 * np.log10(max(1e-30, np.mean(power[(hz >= lo) & (hz < hi)]))))
    result = {"inputSamples": count, "datatype": datatype, "rateHz": rate, "mpxRateHz": out_rate,
              "bandwidthHz": args.bandwidth, "iqRms": float(np.sqrt(np.mean(np.abs(x) ** 2))),
              "mpxRms": float(np.sqrt(np.mean(mpx ** 2))),
              "pilotBandDb": level(18900, 19100), "pilotAdjacentDb": level(20000, 22000),
              "rdsBandDb": level(54600, 59400), "rdsAdjacentDb": level(62000, 66000)}
    args.output.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
