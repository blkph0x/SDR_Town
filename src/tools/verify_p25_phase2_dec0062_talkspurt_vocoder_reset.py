#!/usr/bin/env python3
"""DEC-0062: mid-grant MAC_PTT / post-END talkspurt resets mbelib (keep abs-dedupe)."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
voice = (root / "src" / "P25VoiceDecode.cpp").read_text(encoding="utf-8", errors="ignore")
hdr = (root / "include" / "Receiver.h").read_text(encoding="utf-8", errors="ignore")
api = (root / "include" / "P25VoiceDecode.h").read_text(encoding="utf-8", errors="ignore")

required = {
    "receiver talkspurt flags": (
        "p25Phase2LastTalkspurtVocoderResetMs" in hdr
        and "p25Phase2TalkspurtEndedPendingVocoderReset" in hdr
    ),
    "reset helper keeps abs-dedupe comment": (
        "p25Phase2ResetVocoderForNewTalkspurt" in voice
        and "Keep abs-dibit de-dupe" in voice
        and "p25AmbeVoiceDecoder = P25AmbeVoiceDecoder()" in voice
    ),
    "observe MAC_PTT / END": (
        "p25Phase2ObserveTargetTalkspurtMac" in voice
        and "mac-ptt-talkspurt" in voice
        and "mac-ptt-after-end" in voice
        and "post-end-voice" in voice
    ),
    "feed path observes before VCW loop": (
        voice.find("DEC-0062: MAC_PTT / END_PTT often ride FACCH")
        < voice.find("for (const auto& burst : orderedBurstsForFeed)")
    ),
    "API declared": (
        "p25Phase2ResetVocoderForNewTalkspurt" in api
        and "p25Phase2ObserveTargetTalkspurtMac" in api
    ),
    "DEC-0062 cited": "DEC-0062" in voice and "DEC-0062" in hdr,
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit("DEC-0062 regression failed: " + ", ".join(failed))
print("P25 Phase 2 DEC-0062 talkspurt vocoder reset: PASS")
