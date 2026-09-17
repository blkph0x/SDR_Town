"""Launch the actual GUI with no RX, validate startup and save layout screenshots."""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    parser.add_argument("--output", type=Path, default=Path("build/workspace_qa"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [("listening", 960, 720, "AU"), ("trunking", 1280, 900, "US"),
             ("hf", 800, 700, "GB"), ("analysis", 1600, 900, "AU")]
    for preset, width, height, profile in cases:
        stem = args.output.resolve() / preset
        image = stem.with_suffix(".png")
        report = stem.with_suffix(".json")
        command = [str(args.exe.resolve()), "--allow-multiple", "--no-control-server",
                   "--gui-dry-run", "--gui-workspace", preset,
                   "--gui-bandplan", profile,
                   "--gui-window-size", f"{width}x{height}",
                   "--gui-screenshot", str(image), "--gui-self-test", str(report),
                   "--gui-exit-after-ms", "2200"]
        with stem.with_suffix(".log").open("w", encoding="utf-8") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=45, check=True)
        result = json.loads(report.read_text(encoding="utf-8"))
        assert result["ok"] and not result["errors"] and not result["warnings"], result
        assert not result["device"]["streaming"], "Layout QA must not start hardware RX"
        assert result["bandPlan"]["id"] == profile, result["bandPlan"]
        header = image.read_bytes()[:24]
        assert header[:8] == b"\x89PNG\r\n\x1a\n", image
        actual = struct.unpack(">II", header[16:24])
        # Widget grabs use physical pixels on high-DPI screens. Both axes must
        # have the same scale; a forced minimum width otherwise fails this test.
        assert abs(actual[0] / width - actual[1] / height) < 0.01, (preset, actual)
        print(f"PASS {preset}: {actual[0]}x{actual[1]}, no RX, no startup errors")


if __name__ == "__main__":
    main()
