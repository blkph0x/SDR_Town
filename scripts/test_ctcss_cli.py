"""Actual CLI integration using independently generated mono tone fixtures."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    args = parser.parse_args()

    def run(path):
        result = subprocess.run([str(args.exe.resolve()), "--cli", "--no-control-server",
                                 "--cmd", f'tones file "{path}"'],
                                text=True, capture_output=True, timeout=30, check=True)
        return result.stdout

    with tempfile.TemporaryDirectory(prefix="ctcss-qa-") as directory:
        root = Path(directory)
        for frequency in (100, 123, 1000, 0):
            path = root / f"tone {frequency}.wav"
            with wave.open(str(path), "wb") as output:
                output.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
                output.writeframes(b"".join(struct.pack("<h", round(3000 * math.sin(2 * math.pi * frequency * n / 8000)))
                                           for n in range(24000)))
            output = run(path)
            records = [json.loads(line) for line in output.splitlines() if line.startswith('{"confirmedWindows"')]
            assert len(records) == 1, output
            assert records[0]["frequencyHz"] == (frequency if frequency in (100, 123) else 0), records
        assert "Tone error: Cannot open tone recording" in run(root / "missing.wav")
        invalid = root / "invalid.wav"
        invalid.write_text("not a waveform", encoding="ascii")
        assert "Tone error: Cannot open tone recording" in run(invalid)
    print("PASS: actual CLI tones, silence, speech-band rejection, malformed and missing files")


if __name__ == "__main__":
    main()
