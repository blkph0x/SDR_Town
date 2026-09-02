from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"


text = MAIN.read_text(encoding="utf-8", errors="replace")
start = text.index("std::shared_ptr<std::function<void(bool)>> decodeOne")
end = text.index("connect(playTimer, &QTimer::timeout", start)
replay_worker = text[start:end]

assert "state->wav.append(audio.audio)" not in replay_worker, (
    "GUI IQ replay WAV/STT must not record decoded block PCM before it is pushed "
    "to the speaker path"
)
assert "std::vector<float> pushedRealAudio;" in replay_worker
assert "const std::vector<float>* speakerPcm" in replay_worker
assert "state->wav.append(*speakerPcm)" in replay_worker
assert "p25TranscriptTapSpeakerPcm(speakerPcm->data(), speakerPcm->size()" in replay_worker
assert "guiIqReplayAudioOutputSamples.fetch_add(static_cast<long long>(speakerPcmSamples)" in replay_worker
assert "state->drainingTail.load(std::memory_order_acquire)" in replay_worker

drain_start = text.index("std::shared_ptr<std::function<void()>> drainReplaySpeakerTail")
drain_end = text.index("std::shared_ptr<std::function<void(bool)>> decodeOne", drain_start)
drain_worker = text[drain_start:drain_end]
for required in (
    "pushP25LiveStreamingAudio(audioEngine",
    "state->pendingSpeaker",
    "state->wav.append(pushedRealAudio)",
    "p25TranscriptTapSpeakerPcm(pushedRealAudio.data(), pushedRealAudio.size()",
    "guiIqReplayAudioOutputSamples.fetch_add(static_cast<long long>(pushedRealAudio.size())",
    "speaker pending drain complete",
):
    assert required in drain_worker, f"missing replay pending drain behavior: {required}"

write_result_start = text.index("auto writeReplayResult =")
write_result_end = text.index("QTimer* playTimer", write_result_start)
write_result = text[write_result_start:write_result_end]
for required in (
    'record["iqReplay"]["speakerPushedSamples"] = state->speakerSamples',
    'record["iqReplay"]["speakerDrainEvents"] = state->speakerDrainEvents',
    'record["iqReplay"]["speakerDrainSamples"] = state->speakerDrainSamples',
    'record["iqReplay"]["speakerDroppedTailSamples"] = state->speakerDroppedTailSamples',
    'record["iqReplay"]["fedFrames"] = state->fedFrames',
    'record["iqReplay"]["emittedPcmFrames"] = state->emittedPcmFrames',
    'record["iqReplay"]["targetVoiceCodewords"] = state->targetVoiceCodewords',
    'record["iqReplay"]["oppositeVoiceCodewords"] = state->oppositeVoiceCodewords',
    'record["iqReplay"]["inputQualityRejectedVoiceCodewords"] =',
):
    assert required in write_result, f"missing replay diagnostic counter: {required}"

selftest_start = text.index("void writeGuiRuntimeSelfTestResult")
selftest_end = text.index("QString guiRuntimeCaptureLabel", selftest_start)
selftest = text[selftest_start:selftest_end]
assert "GUI IQ replay result preserved" in selftest, (
    "runtime self-test writer must not overwrite the detailed IQ replay result"
)
assert "!guiRuntimeConfig.iqReplayResultPath.empty() && guiRuntimeConfig.iqReplay" in selftest

print("verify_p25_gui_replay_speaker_pcm_accounting: PASS")
