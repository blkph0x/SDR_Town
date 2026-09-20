#!/usr/bin/env python3
"""Decode the PySSTV fixture with the independent `sstv` decoder (a control).

PySSTV is encode-only, so this uses colaclanth's `sstv` decoder on the same
fixture our Rust decoder reads, to help tell whether a timing offset originates
in PySSTV's signal or in our decoder.

Generate the fixture first with tests/scripts/encode_with_pysstv.py.

Requires:  pip install --no-deps -r tests/scripts/requirements.txt
Usage:     python3 tests/scripts/decode_with_sstv.py [output.png]
"""

import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURE = os.path.join(REPO_ROOT, "tests", "assets", "patch-robot36-pysstv.wav")
OUTPUT = (
    sys.argv[1]
    if len(sys.argv) > 1
    else os.path.join(REPO_ROOT, "tests", "assets", "pysstv_decoded_by_sstv.png")
)


def main() -> None:
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)

    # sstv logs progress via os.get_terminal_size(), which raises without a TTY.
    decode_code = (
        "import os;"
        "os.get_terminal_size = lambda *a: os.terminal_size((80, 24));"
        "from sstv.__main__ import main; main()"
    )
    subprocess.run(
        [sys.executable, "-c", decode_code, "-d", FIXTURE, "-o", OUTPUT],
        check=True,
    )

    print(f"wrote {OUTPUT}")


if __name__ == "__main__":
    main()
