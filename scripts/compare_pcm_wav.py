"""Compare mono/stereo PCM16 or float32 RIFF WAVs without scientific packages."""
import argparse
import array
import json
import math
import struct
import sys
from pathlib import Path


def read_wav(path):
    with Path(path).open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
            raise ValueError("Expected little-endian RIFF WAVE")
        end = struct.unpack_from("<I", header, 4)[0] + 8
        fmt = None
        data = bytearray()
        while stream.tell() < end:
            chunk = stream.read(8)
            if len(chunk) != 8:
                raise ValueError("Truncated chunk header")
            tag, size = struct.unpack("<4sI", chunk)
            if stream.tell() + size + (size & 1) > end:
                raise ValueError("Chunk exceeds RIFF boundary")
            payload = stream.read(size)
            if len(payload) != size:
                raise ValueError("Truncated chunk")
            if tag == b"fmt ":
                if len(payload) < 16:
                    raise ValueError("Truncated format")
                fmt = struct.unpack_from("<HHIIHH", payload)
            elif tag == b"data":
                data.extend(payload)
            if size & 1:
                if len(stream.read(1)) != 1:
                    raise ValueError("Missing chunk padding")
    if fmt is None:
        raise ValueError("Missing format")
    codec, channels, rate, byte_rate, align, bits = fmt
    if (codec, bits) not in ((1, 16), (3, 32)):
        raise ValueError(f"Unsupported codec={codec} bits={bits}")
    if not channels or not rate or align != channels * bits // 8 or byte_rate != rate * align:
        raise ValueError("Inconsistent format")
    if len(data) % align:
        raise ValueError("Partial PCM frame")
    samples = array.array("h" if codec == 1 else "f")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    scale = 32768.0 if codec == 1 else 1.0
    values = [v / scale for v in samples]
    if not all(math.isfinite(v) for v in values):
        raise ValueError("Non-finite PCM")
    return rate, channels, values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--tolerance", type=float, default=0.0)
    args = parser.parse_args()
    if not math.isfinite(args.tolerance) or args.tolerance < 0:
        parser.error("tolerance must be finite and nonnegative")
    ar, ac, a = read_wav(args.reference)
    br, bc, b = read_wav(args.candidate)
    same_shape = (ar, ac, len(a)) == (br, bc, len(b))
    differences = [x - y for x, y in zip(a, b)] if same_shape else []
    peak = max(map(abs, differences), default=0.0) if same_shape else None
    ok = same_shape and peak <= args.tolerance
    print(json.dumps({"ok": ok, "reference_samples": len(a), "candidate_samples": len(b),
                      "reference_rate": ar, "candidate_rate": br, "same_shape": same_shape,
                      "max_abs_error": peak,
                      "rms_error": math.sqrt(math.fsum(d*d for d in differences) / len(differences))
                      if differences else None}, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
