#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(errors="ignore")
session = (root / "include" / "P25ReceiverSession.h").read_text(errors="ignore")

drain = main.split("auto drainPendingRawVoice = [&]() {", 1)[1].split("auto releasePendingRawVoiceFromEss", 1)[0]
feed = main.split("for (const auto& burst : orderedBurstsForFeed) {", 1)[1].split("if (lateEntryStrongTargetReleaseDecoded", 1)[0]

checks = {
    "pending AMBE carries slot provenance": (
        "bool grantSlotKnown = false;" in session and
        "uint8_t grantSlot = 0xffu;" in session
    ),
    "validation defaults unknown slot to fail-closed": "uint8_t grantSlot = 0xffu;" in main,
    "pending drain records pending slot": (
        "frame.grantSlotKnown = pending.grantSlotKnown;" in drain and
        "frame.grantSlot = pending.grantSlot;" in drain
    ),
    "pending drain rejects unknown slot before mbelib": (
        "if (!pending.grantSlotKnown)" in drain and
        drain.find("if (!pending.grantSlotKnown)") < drain.find("p25DecodePhase2AmbeFrameToAudio")
    ),
    "pending drain rejects non-followed slot before mbelib": (
        "pending.grantSlot & 0x01u" in drain and
        "rx.p25VoiceTdmaSlot & 0x01u" in drain and
        drain.find("pending.grantSlot & 0x01u") < drain.find("p25DecodePhase2AmbeFrameToAudio")
    ),
    "live feed rejects unlabelled TDMA voice": (
        "if (!burst.grantSlotKnown)" in feed and
        feed.find("if (!burst.grantSlotKnown)") < feed.find("P25P2PendingAmbeFrame pending;")
    ),
    "queued AMBE stores normalized selected slot": (
        "pending.grantSlotKnown = true;" in feed and
        "pending.grantSlot = effectiveBurstSlot;" in feed
    ),
    "validation stores normalized selected slot": (
        "frame.grantSlotKnown = true;" in feed and
        "frame.grantSlot = effectiveBurstSlot;" in feed
    ),
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 pending slot provenance regression FAILED: " + ", ".join(missing))
print("P25 Phase 2 pending slot provenance regression: PASS")
