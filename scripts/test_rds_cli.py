"""Exercise the actual CLI without opening an SDR or audio device."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    args = parser.parse_args()

    def run(path, kind="bits", env=None):
        result = subprocess.run([str(args.exe.resolve()), "--cli", "--no-control-server",
                                 "--cmd", f'rds {kind} "{path.resolve()}"'],
                                capture_output=True, text=True, timeout=30, check=True, env=env)
        return result.stdout

    output = run(Path("tests/fixtures/rds-reference.bits"))
    records = [json.loads(line) for line in output.splitlines() if line.startswith('{"bits"')]
    assert len(records) == 1, output
    record = records[0]
    assert record["identified"] and record["pi"] == 0x22E1, record
    assert record["completeGroups"] >= 3 and record["correctedBlocks"] == 0, record
    output = run(Path("tests/fixtures/rds-mpx-yksi.flac"), "mpx")
    records = [json.loads(line) for line in output.splitlines() if line.startswith('{"bits"')]
    assert len(records) == 1, output
    record = records[0]
    assert record["decoder"] == "redsea-mpx", record
    assert record["samples"] == 134400 and record["sampleRate"] == 192000, record
    assert record["completeGroups"] == 2 and not record["identified"], record
    with tempfile.TemporaryDirectory(prefix="rds-cli-") as root:
        parity = Path(root) / "parity.jsonl"
        env = os.environ.copy()
        env["SDR_TOWN_RDS_PARITY_LOG"] = str(parity)
        run(Path("tests/fixtures/rds-mpx-yksi.flac"), "mpx", env)
        reports = [json.loads(line) for line in parity.read_text(encoding="utf-8").splitlines()]
        assert len(reports) == 1, reports
        r = reports[0]
        assert r["samples"] == 134400 and r["blocks"] > 0, r
        assert r["adapterGroups"] == r["nativeGroups"] == 2, r
        assert r["adapterBits"] == r["nativeBits"] > 0, r
        assert r["mismatches"] == r["resetMismatches"] == r["adapterFailures"] == 0, r
        invalid = Path(root) / "invalid bits.txt"
        invalid.write_text("001101 invalid", encoding="ascii")
        assert "RDS error: invalid bit file" in run(invalid)
        missing = Path(root) / "missing.bits"
        assert "RDS error: cannot open file" in run(missing)
        assert "RDS error: Cannot open MPX recording" in run(missing, "mpx")
        assert "RDS error: Cannot open MPX recording" in run(invalid, "mpx")
    print("PASS: reference PI, recorded MPX, parity diagnostic, confirmation gate, malformed and missing files")


if __name__ == "__main__":
    main()
