#!/usr/bin/env python3
from p25_voicetest_classify import classify_voicetest_output, is_pass_audio_status


plc_only = (
    "P25 voicetest result=PASS_PARTIAL_AUDIO voiceWindows=1 emitWindows=1 "
    "decodedFrames=0 audioSamples=15360 speakerSamples=15360 audioSeconds=0.32 "
    "p2bursts=12 p2vcw=46 targetVcw=46 fed=42 emitPcm=42 plc=42 "
    "iqReject=42 ambe=0/42 essKnown=yes essEncrypted=no\n"
)
assert classify_voicetest_output(plc_only) == "FAIL_PLC_ONLY_INPUT_QUALITY_REJECTED"
assert not is_pass_audio_status(classify_voicetest_output(plc_only))

concealment_only = (
    "P25 followtest result=PASS_CLEAR_AUDIO voiceWindows=1 speakerWindows=1 "
    "decodedFrames=0 audioSamples=15360 speakerSamples=15360 p2bursts=12 p2vcw=20 "
    "targetVcw=20 fed=20 emitPcm=20 plc=20 iqReject=0 ambe=0/0 essKnown=yes essEncrypted=no\n"
)
assert classify_voicetest_output(concealment_only) == "FAIL_CONCEALMENT_ONLY_AUDIO"

real_partial = (
    "P25 voicetest result=PASS_PARTIAL_AUDIO voiceWindows=3 emitWindows=2 "
    "decodedFrames=79 audioSamples=86401 speakerSamples=86401 audioSeconds=1.8 "
    "p2bursts=40 p2vcw=170 targetVcw=170 fed=88 emitPcm=88 plc=6 "
    "iqReject=6 ambe=79/88 essKnown=yes essEncrypted=no\n"
)
assert classify_voicetest_output(real_partial) == "PASS_PARTIAL_AUDIO"
assert is_pass_audio_status(classify_voicetest_output(real_partial))

print("P25 voicetest classifier regression: PASS")
