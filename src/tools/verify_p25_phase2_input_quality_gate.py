#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
session = (root.parent / "include" / "P25ReceiverSession.h").read_text(errors="ignore")

assert "phase2InputQualityRejectedVoiceCodewords" in main, (
    "Phase-2 diagnostics must count AMBE frames concealed due to poor CQPSK soft quality"
)
assert "p25Phase2AmbeInputQualityBlockReason" in main, (
    "Phase-2 AMBE path must have an explicit input-quality gate"
)
assert "p25Phase2AmbeInputQualityForCodeword" in main, (
    "Phase-2 AMBE quality gating must prefer per-codeword soft reliability, not whole-window softQ"
)
codeword_gate = main[main.index("P25Phase2AmbeInputQuality p25Phase2AmbeInputQualityForCodeword"):
                     main.index("P25Phase2AmbeInputQuality p25Phase2AmbeInputQualityFromPending")]
assert "codeword.inputSoftDecisionSymbols" in codeword_gate and "burst.syncErrors" in codeword_gate, (
    "Per-codeword quality must carry exact 36-dibit reliability and burst-local sync errors"
)
gate = main[main.index("std::string p25Phase2AmbeInputQualityBlockReason"):
            main.index("void p25Phase2AppendPlcBlock",
                       main.index("std::string p25Phase2AmbeInputQualityBlockReason"))]
assert "softDecisionQuality < 0.42" in gate, (
    "Collapsed softQ windows must be blocked before mbelib"
)
assert "softDecisionQuality < 0.50" in gate and "bestPhase2SyncErrors > 1" in gate, (
    "Marginal softQ windows must also require bad sync/eye evidence before muting"
)
assert "marginalSynchronizedEye" in gate and "cqpskPhaseErrorRmsRad <= 0.34" in gate, (
    "Synchronized marginal CQPSK-eye windows must be allowed through mbelib ECC"
)
assert "softLowConfidenceRatio > 0.20" in gate, (
    "Low-confidence symbol ratio must participate in AMBE input gating"
)
decode = main[main.index("bool p25DecodePhase2AmbeFrameToAudio"):
              main.index("bool p25ProbePhase2AmbeFrameForDiagnostics")]
assert "p25Phase2AmbeInputQualityBlockReason(inputQuality)" in decode, (
    "Live speaker decode must evaluate input quality before mbelib"
)
assert "p25Phase2AmbeInputQualityBypassAllowed" in decode, (
    "Known-clear selected-slot traffic must be able to bypass the coarse softQ pre-mute"
)
assert "bypassed-\" + inputQualityBlockReason" in decode, (
    "Validation JSON must identify when an established clear call bypassed the softQ pre-mute"
)
assert "rx.p25AmbeVoiceDecoder.decodeAmbe3600x2450Frame" in decode, (
    "AMBE decoder call must remain in the persistent vocoder path"
)
assert decode.index("p25Phase2AmbeInputQualityBlockReason(inputQuality)") < decode.index(
    "rx.p25AmbeVoiceDecoder.decodeAmbe3600x2450Frame"
), "Bad RF AMBE must be concealed before it mutates the persistent vocoder"
assert "frame.timelineEmitted = true" in decode, (
    "Concealed AMBE slots must still advance the 20 ms audio timeline"
)
assert "concealSelectedSlotTimeline(frame.inputQualityBlockReason, true);" in decode and (
    "concealSelectedSlotTimeline(inputQualityBlockReason, true);" in decode
), "Input-quality rejects must emit one cadence-preserving concealment block"
assert main.count("if (ok || speechFrame.timelineEmitted)") >= 2, (
    "Sequenced selected-slot PLC frames must keep their speech ordinals through speaker filtering"
)
bypass = gate[gate.index("bool p25Phase2AmbeInputQualityBypassAllowed"):
              gate.index("// Input-quality concealment")]
assert "targetClearEstablished" in bypass and "selectedSlotStructured" in bypass, (
    "SoftQ bypass must require established clear target-slot structure, not just a grant"
)
assert "catastrophicallyWeak" in bypass and "softDecisionQuality < 0.20" in bypass, (
    "SoftQ bypass must still block catastrophic RF collapse before mbelib"
)
assert "out.phase2TargetEssEncrypted" in bypass and "out.phase2WrongSlot" in bypass, (
    "SoftQ bypass must stay fail-closed for encrypted or wrong-slot traffic"
)
plc = main[main.index("void p25Phase2AppendPlcBlock"):
           main.index("void p25Phase2AppendOppositeSlotSustainPlc")]
assert "p25Phase2LastGoodPcm" not in plc, (
    "Input-quality rejects must soft-mute, not repeat stale speech into the speaker"
)
assert "out.audio.insert(out.audio.end(), samplesPerFrame, 0.0f)" in plc, (
    "Input-quality rejects must advance the timeline with deterministic silence"
)
assert "++out.phase2EmittedPcmFrames;" in plc, (
    "Input-quality PLC must count as one emitted Phase-2 timeline frame"
)
assert "inputQualityKnown" in session and "inputSoftDecisionQuality" in session, (
    "Queued/reordered Phase-2 AMBE frames must carry their source-window quality"
)
live_decoder = (root / "P25LiveDecoder.cpp").read_text(errors="ignore")
live_header = (root.parent / "include" / "P25LiveDecoder.h").read_text(errors="ignore")
assert "inputSoftDecisionSymbols = valid" in live_decoder and "stampPhase2CodewordSoftQuality" in live_decoder, (
    "Phase-2 decoder must stamp each AMBE codeword with its own soft reliability"
)
assert "m_phase2SoftDibitTail" in live_header and "m_phase2SoftDibitTail" in live_decoder, (
    "Rolling Phase-2 overlap must retain soft reliability alongside hard dibits"
)
assert "inputQualityBlockReason" in main and "inputSoftLowConfidenceRatio" in main, (
    "Validation JSON must expose per-frame quality-gate evidence"
)
assert "bool p25Phase2TrustedConcealmentOnlyWindow" in main, (
    "Quality-gated selected-slot frames need a single trusted concealment predicate"
)
assert "bool p25VoiceBlockHasSpeakerTimelineAudio" in main, (
    "CLI/GUI speaker gates must share a timeline-audio predicate"
)
assert main.count("p25VoiceBlockHasSpeakerTimelineAudio(audio)") >= 3, (
    "followtest, voicetest, and GUI replay must accept trusted concealment timeline audio"
)
assert main.count("p25VoiceBlockHasSpeakerTimelineAudio(p25Audio)") >= 4, (
    "live GUI/CLI P25 speaker paths must not hard-require decodedFrames for quality-gated PLC"
)
assert main.count("p25VoiceBlockHasSpeakerTimelineAudio(result.audio)") >= 3, (
    "GUI worker result publishing must carry the same speaker-timeline decision"
)
print("P25 Phase 2 AMBE input-quality gate regression: PASS")
