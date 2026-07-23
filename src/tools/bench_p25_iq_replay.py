#!/usr/bin/env python3
"""IQ replay / DSP benchmark guard."""

from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
build = root / "build" / "bin" / "Release" / "sdr_town_tests.exe"
if not build.is_file():
    build = root / "build" / "bin" / "Debug" / "sdr_town_tests.exe"

required_sources = [
    root / "src" / "P25LiveDecoder.cpp",
    root / "src" / "dsp" / "P25StreamingChannelDdc.cpp",
    root / "src" / "dsp" / "P25Phase2Framer.cpp",
    root / "src" / "UpdateManifestSignature.cpp",
    root / "tests" / "test_p25dsp.cpp",
]

missing = [str(p.relative_to(root)) for p in required_sources if not p.is_file()]
if missing:
    raise SystemExit("Benchmark prerequisites missing: " + ", ".join(missing))

if not build.is_file():
    raise SystemExit(
        "SKIP: sdr_town_tests executable not built (build Release target sdr_town_tests first)"
    )

filters = [
    "[p25][dsp]",
    "[p25][framer]",
]
for tag in filters:
    proc = subprocess.run([str(build), tag], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"P25 DSP benchmark filter failed: {tag}")

decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
checks = {
    "anchor-gated framer decode": "m_phase2SuperframeAnchorKnown" in decoder and
        "m_phase2FramerOriginStreamDibit" in decoder,
    "resampler lookahead guard": "kResamplerRadius" in (
        root / "src" / "dsp" / "P25StreamingChannelDdc.cpp"
    ).read_text(encoding="utf-8"),
    "ed25519 manifest verify": "crypto_sign_ed25519_verify_detached" in (
        root / "src" / "UpdateManifestSignature.cpp"
    ).read_text(encoding="utf-8"),
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("Benchmark integration checks failed: " + ", ".join(failed))

print("P25 IQ replay / DSP benchmark guard: PASS")
