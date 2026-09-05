#!/usr/bin/env python3
"""Regression guard for Phase 2 rolling-window fresh/context audio gating."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")
# Prefer the AMBE feed loop that owns fresh/context speaker gating.
_gate_anchor = main.find("contextAudioLockedOut")
if _gate_anchor < 0:
    _gate_anchor = main.find("codewordIsContextOnly(codewordEndAbsDibit)")
_loop_start = main.rfind("for (const auto& codeword : burst.voiceCodewords)", 0, max(_gate_anchor, 0))
if _loop_start < 0:
    _loop_start = main.find("for (const auto& codeword : burst.voiceCodewords)")
codeword_loop = main[_loop_start:]

required = {
    "fresh-start block fields": "phase2FreshStartAbsDibitKnown" in main
    and "phase2ContextSuppressedVoiceCodewords" in main,
    "phase2 decoder receives fresh boundary": "uint64_t freshStartAbsDibit,\n                                                    bool haveFreshStartDibits" in main,
    "fresh boundary computed from context iq": "clampedContextIqSamples" in main
    and "freshStartAbsDibit" in main
    and "live.stats.symbolRate" in main,
    "bounded late-decode context grace": "kFreshContextAudioGraceDibits = 240u" in main
    and "contextAudioFloorDibit" in main,
    "speaker sustain uses near-live locked cadence": "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080" in main
    and "kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.040" in main
    and "kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.080" in main
    and "fall behind live traffic" in main,
    "context-only AMBE blocked before frame key": "if (codewordIsContextOnly(codewordAbsKnown, codewordEndAbsDibit))" in codeword_loop
    and codeword_loop.find("if (codewordIsContextOnly(codewordAbsKnown, codewordEndAbsDibit))")
    < codeword_loop.find("const Phase2VoiceFrameKey frameKey ="),
    "context-grace AMBE absolute gated": "contextAudioLockedOut" in codeword_loop
    and "codewordEndsBeforeFresh &&" in codeword_loop
    and "!p25Phase2ShouldEmitAmbeFrame" in codeword_loop
    and "hadSuccessfulEmit ||" not in codeword_loop.split("const bool contextAudioLockedOut", 1)[1].split(";", 1)[0]
    and "p25Phase2CallHadSpeakerAudio ||" not in codeword_loop.split("const bool contextAudioLockedOut", 1)[1].split(";", 1)[0]
    and "frame.duplicateSuppressedByAbsolute = true;" in codeword_loop
    and "frame.contextSuppressed = true;" in codeword_loop,
    "context AMBE never queued": codeword_loop.find("if (codewordIsContextOnly(codewordAbsKnown, codewordEndAbsDibit))")
    < codeword_loop.find("P25P2PendingAmbeFrame pending;"),
    "GUI foreground passes context": "iqStartAbsolute, iqStartAbsoluteKnown, phase2ContextIqSamples);" in main,
    "GUI worker passes context": "job.iqStartAbsoluteKnown,\n                                job.contextIqSamples)" in main,
    "CLI live passes context": "iqStartAbsolute, iqStartAbsoluteKnown, phase2ContextIqSamples);" in main,
    "voicetest stream passes context": "contextSamplesForWindow" in main
    and "voiceHz, 48000.0, absStart, true,\n                                              contextSamplesForWindow)" in main,
    "voicetest stream mirrors speaker sustain context": "voiceSpeakerSustainContextSamples" in main
    and "p25Phase2SessionSpeakerSustainActive(rx)" in main
    and "streamSpeakerSustainWindow" in main,
    "voicetest hopms zero remains auto cadence": "double hopMs = 0.0;      // 0 => stream auto cadence" in main
    and "args.hopMs = value <= 0.0 ? 0.0 : std::max(10.0, value);" in main,
    "continuous verdict rejects overfill": "audioSeconds <= spanSeconds + timelineSlackSeconds" in main
    and "duty <= 1.05" in main
    and "timelineOk && trustedTrafficProof" in main,
    "continuous verdict checks sequencer drops": "const bool sequencerOk =" in main
    and "sequencerOk && ambeQualityOk" in main,
    "logs expose context drops": "ctxVcw=%25 ctxDrop=%26" in main
    and '" contextSuppressed=" << phase2ContextSuppressedVoiceCodewords' in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 fresh/context audio gate regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 fresh/context audio gate regression: PASS")
