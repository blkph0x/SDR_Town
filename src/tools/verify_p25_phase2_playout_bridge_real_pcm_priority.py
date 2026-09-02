#!/usr/bin/env python3
"""Guard that Phase 2 playout never puts bridge silence ahead of real PCM."""

from pathlib import Path


root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="replace")
session_h = (root / "include" / "P25ReceiverSession.h").read_text(encoding="utf-8", errors="replace")
engine_h = (root / "include" / "AudioEngine.h").read_text(encoding="utf-8", errors="replace")
engine_cpp = (root / "src" / "AudioEngine.cpp").read_text(encoding="utf-8", errors="replace")
topup = main.split("static size_t p25TopUpSpeakerPlaybackRing", 1)[1].split(
    "static size_t p25Phase2EffectiveMinFreshSamples", 1
)[0]
push = main.split("static size_t pushP25LiveStreamingAudio", 1)[1].split(
    "static size_t pushP25SpeakerAudio", 1
)[0]

checks = {
    "warm pending real-audio argument": "warmPendingRealAudio" in push,
    "warm pending uses hot prime": (
        "warmPlaybackContext" in push
        and "(midCallRingRestart || warmPlaybackContext) ? hotPrimeSamples : coldPrimeSamples"
        in push
    ),
    "warm pending can push to real-audio ceiling": (
        "const bool realAudioReady = hasFreshAudio || warmPendingRealAudio" in push
        and "const size_t queueLimit = realAudioReady ? pushCeilingSamples : targetQueuedSamples"
        in push
    ),
    "pushed real PCM can be reported": "std::vector<float>* pushedRealAudio" in push
    and "pushedRealAudio->insert" in push,
    "topup pushes pending as warm real PCM": (
        "pushP25LiveStreamingAudio(engine" in topup
        and "true," in topup
        and "&pushedRealAudio" in topup
    ),
    "bridge waits until no playable pending PCM": (
        "if (it->second.samples.size() < phase2FrameSamples)" in topup
        and "pushP25Phase2PlayoutBridge" in topup.split(
            "if (it->second.samples.size() < phase2FrameSamples)", 1
        )[1]
    ),
    "topup refreshes real output tail": (
        "p25Phase2NoteQueuedSpeakerPcmPushed(*rx, pushedRealAudio, nowMs)" in topup
        and "gP25AudioLastSpeakerOutputMs.store(nowMs" not in topup
    ),
    "bridge tail is bound to call identity": (
        "P25P2CallAudioKey lastSpeakerKey" in session_h
        and "bool haveLastSpeakerKey" in session_h
        and "bool playoutBridgeEligible" in session_h
        and "tail.lastSpeakerKey = key;" in main
        and "tail.haveLastSpeakerKey = true;" in main
        and "tail.playoutBridgeEligible =" in main
        and "armPlayoutBridge ? true" in main
        and "!(tail.lastSpeakerKey == currentKey)" in main
    ),
    "bridge uses current scheduler key": (
        "p25CurrentPhase2AudioKey(rx, p25Phase2VoiceSchedulerNominalHz(rx))" in main
        and "p25Phase2RememberLastEmittedSample(rx, key, pcm, false)" in main
    ),
    "bridge anchors only on clean selected-slot windows": (
        "static bool p25Phase2CleanPlayoutBridgeAnchorWindow" in main
        and "p25Phase2CompanionSlotAccounted(out)" in main.split(
            "static bool p25Phase2CleanPlayoutBridgeAnchorWindow", 1
        )[1][:1200]
        and "out.phase2WrongSlotVoiceCodewords == 0" in main.split(
            "static bool p25Phase2CleanPlayoutBridgeAnchorWindow", 1
        )[1][:1200]
        and "p25Phase2CleanPlayoutBridgeAnchorWindow(result.audio)" in main
    ),
    "ambiguous windows disarm bridge": (
        "static bool p25Phase2WindowDisablesPlayoutBridge" in main
        and "tail.playoutBridgeEligible = false;" in main
        and "out.phase2WrongSlotVoiceCodewords > 0" in main.split(
            "static bool p25Phase2WindowDisablesPlayoutBridge", 1
        )[1][:900]
        and "out.phase2WrongSlot" in main.split(
            "static bool p25Phase2WindowDisablesPlayoutBridge", 1
        )[1][:900]
    ),
    "topup splits real and bridge diagnostics": (
        "size_t* realAudioPushed" in topup
        and "size_t* bridgeAudioPushed" in topup
        and "if (realAudioPushed) *realAudioPushed += pushed;" in topup
        and "if (bridgeAudioPushed) *bridgeAudioPushed += bridgePushed;" in topup
    ),
    "gui topup does not count bridge as speech": (
        "guiP25AudioOutputSamples.fetch_add(static_cast<long long>(realTopUpPushed)" in main
        and "guiP25AudioOutputSamples.fetch_add(static_cast<long long>(topUpPushed)" not in main
    ),
    "audio ring tags bridge samples": (
        "std::vector<uint8_t> sampleKind" in engine_h
        and "kRingSampleBridge" in engine_h
        and "rb.sampleKind[w] = sampleKind;" in engine_cpp
    ),
    "bridge push uses bridge tag": (
        "pushBridgeAudioToActiveOutputs" in engine_h
        and "pushAudioToActiveOutputLocked(*m_active[activeIndex], samples, count, kRingSampleBridge)" in engine_cpp
        and "engine->pushBridgeAudioToActiveOutputs" in main
    ),
    "real pcm can preempt queued bridge": (
        "dropQueuedBridgeAudio" in engine_h
        and "compare_exchange_weak" in engine_cpp
        and "dropQueuedBridgeAudio(bridgeHeadroomDeficit, activeOutputIndices)" in main
    ),
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 playout bridge real-PCM priority regression FAILED: "
        + ", ".join(missing)
    )

print("P25 Phase 2 playout bridge real-PCM priority regression: PASS")
