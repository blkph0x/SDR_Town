from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"
SESSION = ROOT / "include" / "P25ReceiverSession.h"
RECEIVER = ROOT / "src" / "Receiver.cpp"

main = MAIN.read_text(encoding="utf-8")
session = SESSION.read_text(encoding="utf-8")
receiver = RECEIVER.read_text(encoding="utf-8")

required = {
    "session remembers ptt-start clear": "lastPttStartPendingClearCallSessionId" in session,
    "session clearAll resets ptt-start clear latch": "lastPttStartPendingClearCallSessionId = 0;" in session,
    "new ptt resets ptt-start clear latch": "rx.p25SessionState.lastPttStartPendingClearCallSessionId = 0;" in receiver,
}

helper = main.split("static void p25Phase2HandlePttStartForPendingQueue", 1)[1].split(
    "static size_t p25Phase2PendingAmbeFrameCount", 1
)[0]
required.update({
    "helper requires valid call key": "if (!audioKey.valid()) return;" in helper,
    "helper no-ops without pending voice": "if (!hasAnyPending) return;" in helper,
    "helper is call-session latch guarded": "lastPttStartPendingClearCallSessionId == audioKey.callSessionId" in helper,
    "helper records call-session latch before clear":
        "lastPttStartPendingClearCallSessionId = audioKey.callSessionId;" in helper,
})

release = main.split("auto releasePendingRawVoiceFromEss = [&]() {", 1)[1].split(
    "auto releasePendingRawVoiceFromTrustedTrafficState", 1
)[0]
required.update({
    "ESS release uses guarded ptt-start helper":
        "p25Phase2HandlePttStartForPendingQueue(rx, audioKey);" in release,
    "ESS release does not directly ptt-start clear":
        "P25PendingClearReason::PttStartReset" not in release,
})

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("verify_p25_phase2_ptt_start_pending_clear_once failed: " + ", ".join(missing))

print("verify_p25_phase2_ptt_start_pending_clear_once: PASS")
