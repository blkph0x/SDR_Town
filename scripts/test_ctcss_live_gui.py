"""Bounded NFM GUI pipeline check; an expected tone must come from the operator."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    parser.add_argument("--frequency-mhz", type=float, required=True)
    parser.add_argument("--expect-tone", type=float)
    parser.add_argument("--duration-ms", type=int, default=45000)
    parser.add_argument("--output", type=Path, default=Path("build/ctcss_live_qa"))
    args = parser.parse_args()
    if not 5000 <= args.duration_ms <= 120000 or not 0 < args.frequency_mhz < 6000:
        parser.error("Use a valid receive frequency and duration of 5000..120000 ms")
    args.output.mkdir(parents=True, exist_ok=True)
    report = args.output.resolve() / "result.json"
    started = time.time()
    with (args.output / "run.log").open("w", encoding="utf-8") as log:
        subprocess.run([str(args.exe.resolve()), "--no-control-server", "--gui-freq", str(args.frequency_mhz),
                        "--gui-workspace", "listening", "--gui-self-test", str(report),
                        "--gui-exit-after-ms", str(args.duration_ms)], stdout=log, stderr=subprocess.STDOUT,
                       timeout=args.duration_ms / 1000 + 45, check=True)
    assert report.exists() and report.stat().st_mtime >= started, "Missing fresh GUI report"
    result = json.loads(report.read_text(encoding="utf-8"))
    assert result["ok"] and not result["errors"] and result["device"]["streaming"], result
    tone = result["ctcss"]
    assert tone["samples"] > 0 and tone["windows"] >= 2, tone
    assert abs(tone["targetHz"] - args.frequency_mhz * 1e6) < 1, tone
    dcs = result["dcs"]
    assert dcs["samples"] == tone["samples"] and dcs["resets"] == tone["resets"], dcs
    assert dcs["targetHz"] == tone["targetHz"], dcs
    if args.expect_tone is not None:
        assert abs(tone["frequencyHz"] - args.expect_tone) < .01, tone
    print("PASS live NFM data path" + (" and expected tone" if args.expect_tone is not None else " (tone not independently verified)"))
    print(json.dumps(tone))
    print("DCS data-path check only, no independently known live code:", json.dumps(dcs))


if __name__ == "__main__":
    main()
