#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# The first generator intentionally uses exact source anchors. It correctly
# patches the C++ and regression sources, then the historical v1 generator
# stops at its stale verifier anchor. Preserve those source edits and replace
# only that known generator failure with the correct verifier/document edits.
result = subprocess.run(
    [sys.executable, "scripts/apply_hf_antialias_hardening.py"],
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
)
if result.returncode == 0:
    raise SystemExit("v1 generator unexpectedly succeeded; refuse ambiguous patch state")
if "HF verifier anti-alias contract: expected one match, found 0" not in result.stdout:
    print(result.stdout, end="")
    raise SystemExit("v1 generator failed for an unexpected reason")

verify_path = Path("scripts/verify_hf_integration.py")
verify = verify_path.read_text(encoding="utf-8")
anchor = '''    require("decoder tap is continuous" in tests,
            "HF SSTV continuity coverage missing")
'''
replacement = '''    require("decoder tap is continuous" in tests,
            "HF SSTV continuity coverage missing")
    require("adaptiveResamplerHalf" in source and
            "kMaximumResamplerHalf = 1024" in source,
            "multi-MS/s anti-alias hardening missing")
    require("0.38 * std::min(1.0, outputRateHz / inputRateHz)" in source,
            "HF anti-alias transition band missing")
    require("multi-megasample SDR streams" in tests,
            "multi-MS/s alias regression coverage missing")
'''
verify = replace_once(verify, anchor, replacement, "HF verifier insertion")
verify_path.write_text(verify, encoding="utf-8")

doc_path = Path("docs/HF_RECEIVE.md")
doc = doc_path.read_text(encoding="utf-8")
doc = replace_once(
    doc,
    "- Very short impulsive samples are replaced before the channel filter.\n",
    "- Very short impulsive samples are replaced before the channel filter.\n"
    "- The streaming rate converter expands its anti-alias kernel for common\n"
    "  multi-megasample SDR rates, preventing signals near 48 kHz multiples\n"
    "  from folding into the selected HF audio channel.\n",
    "HF documentation anti-alias property",
)
doc = replace_once(
    doc,
    "- safe fallback from stale wideband settings\n",
    "- safe fallback from stale wideband settings\n"
    "- multi-MS/s rate conversion and alias rejection at 2.4 and 10 MS/s\n",
    "HF validation list",
)
doc_path.write_text(doc, encoding="utf-8")

print("high-rate HF anti-alias patch applied with corrected verifier contract")
