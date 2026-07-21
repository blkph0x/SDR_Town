#!/usr/bin/env python3
from pathlib import Path

text = Path("src/main.cpp").read_text(errors="ignore")

may = text.split("static bool p25Phase2MayAppendPlcBlock", 1)[1].split(
    "static bool p25Phase2AudioTailGraceActive", 1)[0]
gate = text.split("static std::string p25VoiceBlockSpeakerGateReason", 1)[1].split(
    "static bool p25VoiceBlockMayEmitAudio", 1)[0]
opp = text.split("static void p25Phase2AppendOppositeSlotSustainPlc", 1)[1].split(
    "static bool p25Phase2AmbeProbeScoreLooksFinite", 1)[0]
gap = text.split("static void p25Phase2FillFeedGapWithPlc", 1)[1].split(
    "static void p25RecordPhase2AmbeFeedCadence", 1)[0]

checks = {
    "bounded speaker tail grace": "static constexpr qint64 kP25Phase2SpeakerAudioTailGraceMs = 2500;" in text,
    "trusted clear helper": "static bool p25Phase2BlockHasTrustedClearContext" in text,
    "plc helper hard-denies invent audio": "return false;" in may and "never synthesizes" in may,
    "opposite-slot PLC disabled": "Intentionally disabled" in opp and "return;" not in opp.split("{", 1)[1][:80] or "(void)rx;" in opp,
    "feed-gap PLC disabled": "Disabled: SDRTrunk does not invent" in gap,
    "speaker rejects fed=0 invent PCM": 'return "phase2-no-fresh-feed";' in gate and "phase2FedToMbelib == 0" in gate,
    "concealment dominant muted": 'return "phase2-concealment-dominant";' in text,
    "concealment without target muted": 'return "phase2-concealment-without-target";' in text,
    "fragmented tail muted": 'return "phase2-fragmented-tail-muted";' in text,
    "fragmented tail no longer mutes on feed gap alone": "out.phase2FeedGaps > 0 ||" not in text,
    "no rf-gap plc injection": "p25RecordPhase2AmbeFeedCadence(P25VoiceAudioBlock& out" in text and
                               "p25Phase2AppendPlcBlock(rx, outputRateHz, out);" not in
                               text.split("static void p25RecordPhase2AmbeFeedCadence", 1)[1].split("static json p25Phase2EssJson", 1)[0],
    "phase2 empty-audio buffer preserve requires trusted clear context": "hasTrustedPhase2ClearContext" in text and "rx.p25VoiceMaskParamsKnown ||" not in text,
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 inter-speech garbage gate regression: FAIL: " + ", ".join(missing))
print("P25 Phase 2 inter-speech garbage gate regression: PASS")
