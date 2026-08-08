#!/usr/bin/env python3
"""Guard: dual-slot MAC-dead windows never reach speaker as trusted-clear."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="replace")

helper = main.split("p25Phase2DualSlotUntrustedGarbleWindow", 1)[1].split(
    "p25Phase2DualSlotPendingDrainUnsafeWindow", 1
)[0]
pending = main.split("p25Phase2DualSlotPendingDrainUnsafeWindow", 1)[1].split(
    "p25Phase2UnsafeMixedSlotAudioWindow", 1
)[0]
trusted_clear = main.split("const bool trustedClear =", 1)[1][:800]
dual_now = main.split("const bool dualSlotUntrustedNow =", 1)[1][:400]
drop_path = main.split("dual-slot-untrusted-garble-drop", 1)[0][-400:]

checks = {
    "helper defined": "p25Phase2DualSlotUntrustedGarbleWindow" in main,
    "pending drain helper defined": "p25Phase2DualSlotPendingDrainUnsafeWindow" in main,
    "helper requires this-window MAC (macCrcValid)": (
        "phase2MacCrcValid > 0" in helper and "thisWindowMacOk" in helper
    ),
    "helper requires target MAC OR window MAC": "phase2TargetMacCrcValid" in helper,
    "helper fails closed without MAC": (
        "if (!thisWindowMacOk)" in helper and "return true;" in helper
    ),
    "sticky ESS must not defeat helper": (
        "phase2TargetEssKnown" not in helper.split("thisWindowMacOk", 1)[0]
        or "targetSlotClear" not in helper
    ),
    "trustedClear fail-closes dual-slot first": (
        "!dualSlotUntrustedGate" in trusted_clear[:200]
    ),
    "dualSlotUntrustedNow ignores sticky ESS": (
        "phase2TargetEssKnown" not in dual_now
        and "phase2TargetSessionAudioRelease" not in dual_now
    ),
    "dualSlotUntrustedNow requires MAC": (
        "phase2MacCrcValid == 0" in dual_now or "phase2MacCrcValid" in dual_now
    ),
    "window drop path for dual-slot": "dual-slot-untrusted-garble-drop" in main,
    "drop path clears audio without unknown thrash": (
        "out.audio.clear()" in drop_path and "WaitingForClearGrant" not in drop_path
    ),
    "speaker gate names garble": "phase2-dual-slot-untrusted-garble" in main,
    "unsafe mixed includes garble": (
        "p25Phase2DualSlotUntrustedGarbleWindow(out)"
        in main.split("p25Phase2UnsafeMixedSlotAudioWindow", 1)[1][:600]
    ),
    "pending drain uses strict helper": (
        "p25Phase2DualSlotPendingDrainUnsafeWindow(out)"
        in main.split("auto canDrainPendingRawVoiceThisWindow", 1)[1].split(
            "auto drainPendingRawVoice", 1
        )[0]
    ),
    "034136 capture note present": "20260808_034136" in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 dual-slot untrusted garble gate regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 dual-slot untrusted garble gate regression: PASS")
for name in checks:
    print(f"  OK  {name}")
