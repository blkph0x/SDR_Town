#!/usr/bin/env python3
"""Regression guard for v0.2.43 overlap-boundary and burst-closure fixes."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="ignore")
decoder_h = (root / ".." / "include" / "P25LiveDecoder.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)
decoder_cpp = (root / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
session_h = (root / ".." / "include" / "P25ReceiverSession.h").resolve().read_text(
    encoding="utf-8", errors="ignore"
)

copy_ctor = decoder_cpp.split("P25LiveDecoder::P25LiveDecoder(const P25LiveDecoder& other)", 1)[1].split(
    "P25LiveDecoder& P25LiveDecoder::operator=(const P25LiveDecoder& other)", 1
)[0]
copy_assign = decoder_cpp.split("P25LiveDecoder& P25LiveDecoder::operator=(const P25LiveDecoder& other)", 1)[1].split(
    "P25LiveDecoder::P25LiveDecoder(P25LiveDecoder&& other)", 1
)[0]
move_ctor = decoder_cpp.split("P25LiveDecoder::P25LiveDecoder(P25LiveDecoder&& other)", 1)[1].split(
    "P25LiveDecoder& P25LiveDecoder::operator=(P25LiveDecoder&& other)", 1
)[0]
move_assign = decoder_cpp.split("P25LiveDecoder& P25LiveDecoder::operator=(P25LiveDecoder&& other)", 1)[1].split(
    "uint64_t p25EncodeNidBch", 1
)[0]
reset_block = decoder_cpp.split("void P25LiveDecoder::reset()", 1)[1].split(
    "P25LiveDecoder P25LiveDecoder::createIndependentProbeCopy", 1
)[0]
cursor_clear = decoder_cpp.split("auto clearTrafficContinuity = [&]()", 1)[1].split(
    "// This method is called before processHardDibits", 1
)[0]
normalize = decoder_cpp.split("normalizePhase2BurstOffsets", 1)[1][:1200]
same_burst_main = main.split("p25Phase2SameVoiceBurst", 1)[1][:900]
close_burst = main.split("p25Phase2CloseActiveVoiceBurst", 1)[1][:2600]
session_same = session_h.split("p25Phase2VoiceFrameKeysSameBurst", 1)[1][:700]

required = {
    "no prefix-position duplicate erase": "cw.dibitOffset < phase2PrefixDibits" not in normalize,
    "recent burst reuse table": "RecentPhase2Burst" in decoder_h and "m_phase2RecentBursts" in decoder_h,
    "recent burst table copied": "m_phase2RecentBursts(other.m_phase2RecentBursts)" in copy_ctor,
    "recent burst table assigned": "m_phase2RecentBursts = other.m_phase2RecentBursts" in copy_assign,
    "recent burst table moved": "m_phase2RecentBursts(std::move(other.m_phase2RecentBursts))" in move_ctor,
    "recent burst table move-assigned": "m_phase2RecentBursts = std::move(other.m_phase2RecentBursts)" in move_assign,
    "recent burst table reset": "m_phase2RecentBursts.clear()" in reset_block,
    "recent burst table cleared on discontinuity": "m_phase2RecentBursts.clear()" in cursor_clear,
    "deep ACCH repair budget reset": "m_phase2ExtraDeepAcchBudget = 0" in reset_block,
    "burst id matched by stream start": "streamBurstStart > seen.streamBurstStartDibit" in decoder_cpp,
    "same burst prefers stream start dibit": (
        same_burst_main.find("streamBurstStartDibitKnown") < same_burst_main.find("sessionBurstIdKnown")
    ),
    "frame keys prefer stream start dibit": (
        session_same.find("streamBurstStartDibitKnown") < session_same.find("sessionBurstIdKnown")
    ),
    "close consumes held before erasure": (
        "heldFutureFrames[seq.expectedVoiceIndex]" in close_burst and
        "p25Phase2SequencerExpireHeldFrames" not in main
    ),
    "close passes decode queue on burst switch": (
        "p25Phase2BeginVoiceBurst(rx, seq, key, &decodeQueue" in main
    ),
    "unknown burst length not default four": "key.burstVoiceCount > 0 ? key.burstVoiceCount : 4" not in main,
    "sequencer accepts any stable protocol identity": (
        "p25Phase2VoiceFrameKeyHasProtocolIdentity(key)" in main.split(
            "p25Phase2SequencerProcessSpeechFrame", 1
        )[1][:1200]
        and "!key.streamDibitKnown || !key.streamBurstStartDibitKnown" not in main.split(
            "p25Phase2SequencerProcessSpeechFrame", 1
        )[1][:1200]
    ),
    "unorderable does not fake hold count": (
        "FrameOrderResult::Unorderable" in main and
        main.split("FrameOrderResult::Unorderable", 1)[1].split("return decodeQueue", 1)[0].count("reorderHeld") == 0
    ),
    "condensed live streaming target fill": "outRate * 0.180" in main.split("pushP25LiveStreamingAudio", 1)[1][:3400],
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 forensic P5 regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 forensic P5 regression: PASS")
