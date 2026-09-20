#!/usr/bin/env python3
"""Produce the reference image the encoder test checks against.

Encodes examples/patch.png with THIS crate's encoder (via the `encode` example),
then decodes the result with the independent colaclanth `sstv` decoder. If our
encoder produces valid Robot36, the decoded image should resemble the source.

Outputs are written into tests/assets/ (gitignored *.wav / *.png).

Regenerate with:  python3 tests/scripts/decode_reference.py
Requires:         pip install --no-deps -r tests/scripts/requirements.txt
"""

import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WAV = os.path.join(REPO_ROOT, "tests", "assets", "encoded.wav")
OUTPUT = os.path.join(REPO_ROOT, "tests", "assets", "encoder-robot36-sstv.png")


def main() -> None:
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)

    # 1. Encode patch.png with our crate's encoder into a WAV.
    subprocess.run(
        ["cargo", "run", "--quiet", "--features", "image,wav", "--example", "encode", "--", WAV],
        cwd=REPO_ROOT,
        check=True,
    )

    # 2. Decode that WAV with the independent sstv decoder.
    #
    # sstv logs progress via os.get_terminal_size(), which raises when stdout is
    # not a terminal (subprocess, CI). Patch it to a fixed size before importing
    # sstv so decoding works without a TTY.
    decode_code = (
        "import os;"
        "os.get_terminal_size = lambda *a: os.terminal_size((80, 24));"
        "from sstv.__main__ import main; main()"
    )
    subprocess.run(
        [sys.executable, "-c", decode_code, "-d", WAV, "-o", OUTPUT],
        check=True,
    )

    print(f"wrote {OUTPUT}")


if __name__ == "__main__":
    main()
