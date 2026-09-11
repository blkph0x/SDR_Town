#!/usr/bin/env python3
"""Source+runtime regressions for framer origin / ACCH / block-resample P0 fixes."""

from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore").replace("\r\n", "\n")
header = (root / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="ignore")
from p25_orchestration_sources import orchestration_source_text
main_cpp = orchestration_source_text()

checks = {
    "framer origin latch helper": "latchPhase2FramerOriginIfNeeded" in decoder,
    "framer feed wraps latch": "feedPhase2FramerDibits(" in decoder
        and "m_phase2Framer.consumeDibits(best.dibits)" not in decoder,
    "origin latched flag": "m_phase2FramerOriginLatched" in header,
    "framer ACCH rescue budget": "framerAcchRescueBudget" in decoder,
    "block resample full sinc support": (
        "pos + static_cast<double>(radius) >= static_cast<double>(x.size())" in decoder
    ),
    "voice config keeps streaming DDC off": (
        "cfg.enableStreamingChannelDdc = false" in main_cpp
    ),
}

idx = decoder.find("burst.sessionAudioRelease")
snippet = decoder[idx : idx + 450] if idx >= 0 else ""
# Clear MAC_ACTIVE (trafficClearRelease) or ESS/PTT may open continuous feed;
# encrypted remains closed via !burst.encrypted && xorMaskApplied.
checks["sessionAudioRelease clear path uses traffic or ESS"] = (
    "sessionAudioRelease" in snippet
    and ("trafficClearRelease" in snippet or "essOrPttClearRelease" in snippet)
    and "xorMaskApplied" in snippet
)

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("P0 source guards failed: " + ", ".join(failed))

exe = root / "build" / "bin" / "Release" / "sdr_town_tests.exe"
if not exe.is_file():
    exe = root / "build" / "bin" / "Debug" / "sdr_town_tests.exe"
if not exe.is_file():
    raise SystemExit("SKIP: sdr_town_tests not built")

for tag in ("[p25][framer][epoch]", "[p25][dsp][ddc]", "[p25][dsp][framer]"):
    proc = subprocess.run([str(exe), tag], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"Regression filter failed: {tag}")

proc = subprocess.run(
    [str(exe), "*MAC_ACTIVE group user*"],
    capture_output=True,
    text=True,
)
if proc.returncode != 0:
    sys.stderr.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    raise SystemExit("MAC_ACTIVE clear/encrypted release regression failed")

print("verify_p25_phase2_framer_origin_and_acch: PASS")
