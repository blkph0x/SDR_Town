#!/usr/bin/env python3
from pathlib import Path

path = Path("src/HfDemod.cpp")
text = path.read_text(encoding="utf-8")

old_state = """    float squelchGain = 0.0f;
    int squelchHang = 0;
    float clickFade = 0.0f;

    uint64_t decoderEpoch = 0;
"""
new_state = """    float squelchGain = 0.0f;
    int squelchHang = 0;
    float clickFade = 0.0f;
    int startupMuteSamples = 0;

    uint64_t decoderEpoch = 0;
"""
if old_state in text:
    text = text.replace(old_state, new_state, 1)
elif new_state not in text:
    raise SystemExit("HF State insertion point no longer matches")

old_reset = """    state.squelchGain = 0.0f;
    state.squelchHang = 0;
    state.clickFade = 0.0f;
    state.decoderContinuous = false;
"""
new_reset = """    state.squelchGain = 0.0f;
    state.squelchHang = 0;
    state.clickFade = 0.0f;
    // Hold output through the FIR group delay and the first few milliseconds of
    // detector/carrier settling. Without this, the all-zero delay line creates
    // a one-block AM step that can hit the final limiter and masks the actual
    // audio low-pass response used by decoder workflows.
    state.startupMuteSamples = static_cast<int>(0.012 * kWorkRateHz) +
        static_cast<int>(state.channelTaps.size() / 2);
    state.decoderContinuous = false;
"""
if old_reset in text:
    text = text.replace(old_reset, new_reset, 1)
elif new_reset not in text:
    raise SystemExit("HF reset insertion point no longer matches")

old_output = """        state->clickFade +=
            static_cast<float>(
                1.0 - std::exp(-1.0 / (0.008 * workRateHz))) *
            (1.0f - state->clickFade);
        sample *= static_cast<float>(audioGain) *
                  state->squelchGain * state->clickFade;
        sample = std::clamp(sample, -0.98f, 0.98f);
"""
new_output = """        if (state->startupMuteSamples > 0) {
            --state->startupMuteSamples;
            sample = 0.0f;
            continue;
        }

        state->clickFade +=
            static_cast<float>(
                1.0 - std::exp(-1.0 / (0.008 * workRateHz))) *
            (1.0f - state->clickFade);
        sample *= static_cast<float>(audioGain) *
                  state->squelchGain * state->clickFade;
        sample = std::clamp(sample, -0.98f, 0.98f);
"""
if old_output in text:
    text = text.replace(old_output, new_output, 1)
elif new_output not in text:
    raise SystemExit("HF output insertion point no longer matches")

path.write_text(text, encoding="utf-8")
