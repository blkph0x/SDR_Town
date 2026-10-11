"""Paced NFM WAV -> virtual Soapy device -> production live SSTV session.

Requires numpy, scipy and soundfile in a test environment. Never transmits RF.
The test selects only its registered virtual device, not physical SDR streams.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--tests", type=Path, default=Path("build/bin/Release/sdr_town_tests.exe"))
    parser.add_argument("--rf-mode", choices=["auto", "NFM"], default="auto")
    parser.add_argument("--skip-seconds", type=float, default=0)
    parser.add_argument("--expected-images", type=int, default=1)
    parser.add_argument("--allow-partial", action="store_true")
    parser.add_argument("--dedicated", action="store_true", help="Open the radio through the SSTV workflow and verify pre-VIS monitoring")
    parser.add_argument("--gui", action="store_true", help="Start and observe the real SSTV window")
    parser.add_argument("--satellite", action="store_true", help="Use the production Satcom pass engine and synthetic orbital Doppler")
    parser.add_argument("--gap-at", type=float, default=-1, help="Simulate a 10 ms SDR overflow at this wall-clock second")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    output = args.out.resolve()
    audio, rate = sf.read(args.wav, dtype="float64")
    if audio.ndim == 2:
        audio = audio.mean(axis=1)
    if not 0 <= args.skip_seconds < len(audio) / rate:
        raise ValueError("Skip must be within the recording")
    audio = audio[int(args.skip_seconds * rate):]
    duration=len(audio)/rate
    if not 0 < duration <= 540 or not np.isfinite(audio).all():
        raise ValueError("Fixture must be finite and 0..540 seconds long")
    if args.gap_at!=-1 and not 1 <= args.gap_at < duration:
        raise ValueError("Gap must fall within the active transmission")
    divisor = np.gcd(rate, 96000)
    audio = resample_poly(audio, 96000 // divisor, rate // divisor)
    pcm = output / "input-96k-s16le.pcm"
    np.rint(np.clip(audio, -1, 32767 / 32768) * 32768).astype("<i2").tofile(pcm)
    env = dict(os.environ, SDR_TOWN_SSTV_VIRTUAL_PCM=str(pcm),
               SDR_TOWN_SSTV_VIRTUAL_OUTPUT=str(output / "decoded"),
               SDR_TOWN_SSTV_VIRTUAL_MODE=args.rf_mode,
               SDR_TOWN_SSTV_VIRTUAL_IMAGES=str(args.expected_images),
               SDR_TOWN_SSTV_VIRTUAL_DEDICATED="1" if args.dedicated else "0",
               SDR_TOWN_SSTV_VIRTUAL_GUI="1" if args.gui else "0",
               SDR_TOWN_SSTV_VIRTUAL_SATELLITE="1" if args.satellite else "0",
               SDR_TOWN_SSTV_VIRTUAL_GAP_AT=str(args.gap_at),
               SDR_TOWN_SSTV_VIRTUAL_COMPLETE="0" if args.allow_partial else "1")
    with (output / "simulation.log").open("w", encoding="utf-8") as log:
        result = subprocess.run([str(args.tests.resolve()), "[.sstv-virtual-sdr]", "--reporter", "compact"],
                                env=env, stdout=log, stderr=subprocess.STDOUT, timeout=duration+60)
    if result.returncode:
        raise RuntimeError(f"Simulation failed; see {output / 'simulation.log'}")
    report = json.loads((output / "decoded/sstv-report.json").read_text())
    print(json.dumps({"rfMode": report.get("rfModeSelected", "satellite profile"), "images": report["images"],
                      "inputSamples": report["inputSamples"], "output": str(output)}, indent=2))


if __name__ == "__main__":
    main()
