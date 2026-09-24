"""Black-box IQ transport/diagnostic checks; never claims Aero voice acceptance."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--remote", action="store_true", help="Explicitly send synthetic diagnostics via existing config")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    args.out = args.out.resolve()
    args.exe = args.exe.resolve()
    base = args.out / "synthetic"
    # Deterministic IQ, not an Aero recording and not expected to emit any audio.
    data = b"".join(struct.pack("<ff", .3 if (i // 40) % 2 else -.3, 0.) for i in range(48000))
    base.with_suffix(".sigmf-data").write_bytes(data)
    base.with_suffix(".sigmf-meta").write_text(json.dumps({
        "global": {"core:datatype": "cf32_le", "core:sample_rate": 48000, "core:version": "1.2.6"},
        "captures": [{"core:sample_start": 0, "core:frequency": 1542935000}], "annotations": []}), encoding="utf-8")
    reports = []
    for gui, fast in [(False, False), (False, True), (True, False), (True, True)]:
        name = ("gui" if gui else "cli") + ("-fast" if fast else "-paced")
        result = args.out / (name + ".json")
        command = [str(args.exe), "--allow-multiple", "--inmarsat-iq", str(base.with_suffix(".sigmf-meta")),
                   "--inmarsat-mode", "egc", "--inmarsat-log-dir", str(args.out),
                   "--inmarsat-result", str(result), "--inmarsat-exit-complete"]
        if not gui:
            command += ["--cli"]
        if fast:
            command += ["--inmarsat-fast"]
        if not args.remote:
            command += ["--no-remote-diagnostics"]
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        (args.out / (name + ".log")).write_bytes(process.stdout)
        if process.returncode:
            raise RuntimeError(f"{name} exit {process.returncode}: see {name}.log")
        report = json.loads(result.read_text(encoding="utf-8-sig"))
        assert report["state"] == "complete", report
        assert report["samples"] == report["positionSamples"] == report["totalSamples"] == 48000
        assert report["validatedFrames"] == report["pcmSamples"] == report["voiceFrames"] == 0
        assert not report["protocolDecoderAvailable"] and not report["aeroVocoderAvailable"]
        assert not report["logError"], report
        records = [json.loads(line) for line in Path(report["logPath"]).read_text(encoding="utf-8").splitlines()]
        assert records[-1]["event"] == "summary"
        assert records[-1]["session"] == report["session"]
        reports.append(report)
        print(name, "PASS", report["samples"], "samples; no claimed voice", flush=True)
    for report in reports[1:]:
        for key in ["samples", "symbols", "rawBlocks", "quality", "resets", "discontinuities"]:
            assert report[key] == reports[0][key], (key, report[key], reports[0][key])
    bad = args.out / "truncated.iq"
    bad.write_bytes(b"123")
    failed = args.out / "error.json"
    p = subprocess.run([str(args.exe), "--cli", "--allow-multiple", "--no-remote-diagnostics",
        "--inmarsat-iq", str(bad), "--inmarsat-format", "cf32_le", "--inmarsat-rate", "48000",
        "--inmarsat-center", "1542935000", "--inmarsat-result", str(failed),
        "--inmarsat-log-dir", str(args.out)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
    assert p.returncode == 2
    assert json.loads(failed.read_text())["state"] == "error"
    print("PASS GUI/CLI paced/fast parity and malformed-input rejection (transport only)")


if __name__ == "__main__":
    main()
