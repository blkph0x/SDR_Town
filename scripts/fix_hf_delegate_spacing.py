#!/usr/bin/env python3
from pathlib import Path

path = Path("src/Demod.cpp")
text = path.read_text(encoding="utf-8")
old = "    // HF_RECEIVE_DELEGATE_END\n\n    rmsOut = -100;"
new = "    // HF_RECEIVE_DELEGATE_END\n    rmsOut = -100;"
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one HF delegate spacing boundary, found {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
