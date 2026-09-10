#!/usr/bin/env python3
"""Guard: companion TDMA slot has second AMBE module + multi-record + promote-by-swap."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
receiver_h = (root / "include" / "Receiver.h").read_text(encoding="utf-8", errors="replace")
session_h = (root / "include" / "P25ReceiverSession.h").read_text(
    encoding="utf-8", errors="replace"
)
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

observe_parts = main.split(
    "void p25Phase2ObserveOppositeSlotAmbe", 1
)
observe_fn = ""
if len(observe_parts) > 1:
    observe_fn = observe_parts[1].split(
        "P25VoiceAudioBlock decodeP25Phase2VoiceBlock", 1
    )[0]

feed = main.split("orderedBurstsForFeed", 1)
feed_region = feed[1][:8000] if len(feed) > 1 else ""
reject = feed_region.split("effectiveBurstSlot != followedGrantSlot", 1)
reject_site = reject[1][:900] if len(reject) > 1 else ""

promote_fn = ""
body_marker = "bool p25Phase2PromoteCompanionModules(Receiver& rx, const char* why) noexcept\n{"
if body_marker not in main:
    body_marker = (
        "bool p25Phase2PromoteCompanionModules(Receiver& rx, const char* why) noexcept\r\n{"
    )
if body_marker in main:
    promote_fn = main.split(body_marker, 1)[1][:1200]

checks = {
    "receiver has opposite AMBE decoder": "p25AmbeVoiceDecoderOpposite" in receiver_h,
    "session has opposite pending queue": "pendingAudioOpposite" in session_h,
    "session has opposite resampler": "resamplerOpposite" in session_h,
    "observe helper exists": "p25Phase2ObserveOppositeSlotAmbe" in main,
    "observe never writes speaker PCM": (
        "out.audio.insert" not in observe_fn
        and "pushP25SpeakerAudio" not in observe_fn
        and "p25DecodePhase2AmbeFrameToAudio" not in observe_fn
    ),
    "observe records companion PCM": "phase2OppositeRecordPcm" in observe_fn,
    "opposite reject site calls observe": "p25Phase2ObserveOppositeSlotAmbe" in reject_site,
    "wrongSlot companion accounting preserved": (
        "phase2WrongSlotVoiceCodewords" in reject_site
    ),
    "promote swaps modules not mix": (
        "std::move(rx.p25AmbeVoiceDecoderOpposite)" in promote_fn
        and "std::swap(rx.p25SessionState.pendingAudio" in promote_fn
        and "out.audio" not in promote_fn
    ),
    "metadata same-rf promote": 'p25Phase2PromoteCompanionModules(rx, "metadata-same-rf")' in main,
    "call-boundary same-rf promote": 'p25Phase2PromoteCompanionModules(rx, "call-boundary-same-rf")' in main,
    "slot-probe promote": 'p25Phase2PromoteCompanionModules(rx, "slot-probe")' in main,
    "oppwav cli flag": "oppositeWavOutPath" in main and "oppwav" in main,
    "cli companion wav capture": "startCliP25OppositeWavCapture" in main,
    "pending clear clears opposite queue": "pendingAudioOpposite = {}" in main
    or "rx.p25SessionState.pendingAudioOpposite = {}" in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 dual AMBE / multi-record / promote regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 dual AMBE / multi-record / promote regression: PASS")
