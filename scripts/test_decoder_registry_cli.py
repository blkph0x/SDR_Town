"""Exercise the real offline capability command, without opening radio hardware."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    args = parser.parse_args()
    result = subprocess.run([str(args.exe.resolve()), "--cli", "--no-control-server", "--cmd", "decoders"],
                            capture_output=True, text=True, timeout=30, check=True)
    assert "Devices enumerated: skipped" in result.stdout, result.stdout
    records = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
    assert len(records) == 1, result.stdout
    entries = records[0]["receiveDecoders"]
    assert {entry["id"] for entry in entries} == {"rds", "ctcss", "dcs"}, entries
    for entry in entries:
        assert entry["adapterCompiled"] and entry["contractVersion"] == 1, entry
        assert entry["backendProbe"] == "not-performed", entry
        assert entry["maximumBlockSamples"] == 262144, entry
        if entry["id"] == "rds":
            assert entry["input"] == "raw-fm-multiplex" and entry["minimumRateHz"] == 128000, entry
            assert entry["requiredModule"] == "sdrtown_rds_dsp.dll", entry
        else:
            assert entry["input"] == "raw-fm-discriminator" and entry["minimumRateHz"] == 8000, entry
            assert entry["requiredModule"] == "" and entry["experimental"], entry
    print("PASS: actual offline CLI registry, compiled capabilities and honest backend requirements")


if __name__ == "__main__":
    main()
