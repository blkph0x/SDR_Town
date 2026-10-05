"""Paired real-IQ CLI proof that passive validation does not change decoded PCM.

User-supplied RF stays local. Pass a voicetest command without its wav= option.
This proves instrumentation non-interference, not intelligible RF or live cadence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--command", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.command.startswith("p25 voicetest ") or "wav=" in args.command:
        parser.error("Use p25 voicetest with all RF metadata but without wav=")
    args.output.mkdir(parents=True, exist_ok=True)
    runs = []
    for validation, deep in ((False, False), (True, False), (False, True)):
        name = "deep" if deep else "validation" if validation else "normal"
        wav = (args.output / (name + ".wav")).resolve()
        env = dict(os.environ, SDR_TOWN_P25_VALIDATION_LOG=str(int(validation)),
                   SDR_TOWN_P25_VALIDATION_REDACT="1", SDR_TOWN_P25_VALIDATION_ALL="1",
                   SDR_TOWN_P25_DEEP_TRACE=str(int(deep)))
        with (args.output / (name + ".log")).open("wb") as log:
            result = subprocess.run([str(args.exe.resolve()), "--cli", "--allow-multiple",
                "--no-control-server", "--no-remote-diagnostics", "--cmd",
                args.command + ' wav="' + wav.as_posix() + '"'],
                cwd=args.exe.resolve().parent, env=env, stdout=log,
                stderr=subprocess.STDOUT, timeout=240)
        if result.returncode or not wav.exists() or wav.stat().st_size <= 44:
            raise RuntimeError(f"{name} produced no successful WAV; inspect local log")
        runs.append({"mode": name, "bytes": wav.stat().st_size,
                     "sha256": hashlib.sha256(wav.read_bytes()).hexdigest()})
    passed = len({r["sha256"] for r in runs}) == 1
    report = {"passed": passed, "runs": runs}
    (args.output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not passed:
        raise SystemExit("Passive logging changed PCM")


if __name__ == "__main__":
    main()
