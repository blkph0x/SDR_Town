"""Actual CLI DCS tests using the independent ETSI fixture generator."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from test_dcs_reference import VECTORS, air_bits, verify, write_fixtures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    args = parser.parse_args()

    def run(action, path):
        result = subprocess.run([str(args.exe.resolve()), "--cli", "--no-control-server",
                                 "--cmd", f'tones {action} "{path}"'],
                                text=True, capture_output=True, check=True, timeout=30)
        return result.stdout

    def decoded(action, path):
        output = run(action, path)
        records = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
        records = [record for record in records if record.get("decoder") == "dcs"]
        assert len(records) == 1, output
        return records[0]

    verify()
    with tempfile.TemporaryDirectory(prefix="dcs qa ") as directory:
        root = Path(directory)
        write_fixtures(root)
        for code, word in VECTORS.items():
            for inverted in (False, True):
                label = f"{code:03o}{'I' if inverted else 'N'}"
                audio = root / f"dcs_{code:03o}_{'inverted' if inverted else 'normal'}.wav"
                record = decoded("dcs", audio)
                assert label in record["aliases"], record
                assert record["samples"] == 40000, record
                bits = root / "stream.bits"
                bits.write_text("".join(str(int(bit)) for bit in air_bits(word, inverted)) * 12, encoding="ascii")
                assert decoded("dcs-bits", bits)["aliases"] == record["aliases"]
        invalid = root / "invalid.bits"
        invalid.write_text("010201", encoding="ascii")
        assert "DCS bits require ASCII" in run("dcs-bits", invalid)
        invalid.write_text("0" * 16129, encoding="ascii")
        assert "exceeds 120 seconds" in run("dcs-bits", invalid)
        assert "Cannot open DCS bit recording" in run("dcs-bits", root / "missing.bits")
        assert "Cannot open DCS recording" in run("dcs", root / "missing.wav")
    print("PASS actual CLI: independent DCS waveform/bitstream aliases, polarity and input errors")


if __name__ == "__main__":
    main()
