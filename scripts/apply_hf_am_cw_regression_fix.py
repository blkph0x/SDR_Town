#!/usr/bin/env python3
from pathlib import Path


def replace_exact(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded block, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_exact(
    "src/HfDemod.cpp",
    """    // Hold output through the FIR group delay and the first few milliseconds of
    // detector/carrier settling. Without this, the all-zero delay line creates
    // a one-block AM step that can hit the final limiter and masks the actual
    // audio low-pass response used by decoder workflows.
    state.startupMuteSamples = static_cast<int>(0.012 * kWorkRateHz) +
        static_cast<int>(state.channelTaps.size() / 2);
""",
    """    // AM envelope normalization needs a short acquisition interval while the
    // channel FIR fills. SSB and CW must not prepend silence: doing so changes
    // whole-block pitch/timing measurements and delays weak-signal decoder data.
    state.startupMuteSamples = state.mode == DemodMode::AM
        ? static_cast<int>(0.012 * kWorkRateHz) +
              static_cast<int>(state.channelTaps.size() / 2)
        : 0;
""",
)

replace_exact(
    "src/HfDemod.cpp",
    """    std::vector<float> audio(channel.size());
    if (mode == DemodMode::AM) {
        const float carrierAlpha = static_cast<float>(
            1.0 - std::exp(-2.0 * kPi * 4.0 / workRateHz));
        for (size_t index = 0; index < channel.size(); ++index) {
            const float envelope = std::abs(channel[index]);
            if (!state->carrierValid) {
                state->carrier = std::max(envelope, 1.0e-5f);
                state->carrierValid = true;
            }
            state->carrier +=
                carrierAlpha * (envelope - state->carrier);
            audio[index] =
                (envelope - state->carrier) /
                std::max(state->carrier, 1.0e-5f);
        }
""",
    """    std::vector<float> audio(channel.size());
    if (mode == DemodMode::AM) {
        // Prime the carrier estimator from settled samples in this block. Using
        // the first all-zero FIR output as the carrier reference produces a huge
        // normalized step as the delay line fills, which can drive both filtered
        // and decoder-bypass paths into the limiter and erase their difference.
        if (!state->carrierValid) {
            const size_t settledAt = std::min(
                channel.size(), state->channelTaps.size() / 2);
            double carrierSum = 0.0;
            size_t carrierCount = 0;
            for (size_t index = settledAt; index < channel.size(); ++index) {
                carrierSum += std::abs(channel[index]);
                ++carrierCount;
            }
            if (carrierCount == 0) {
                for (const auto& sample : channel) carrierSum += std::abs(sample);
                carrierCount = channel.size();
            }
            state->carrier = std::max(
                static_cast<float>(carrierSum /
                    static_cast<double>(std::max<size_t>(1, carrierCount))),
                1.0e-5f);
            state->carrierValid = true;
        }

        const float carrierAlpha = static_cast<float>(
            1.0 - std::exp(-2.0 * kPi * 4.0 / workRateHz));
        for (size_t index = 0; index < channel.size(); ++index) {
            const float envelope = std::abs(channel[index]);
            state->carrier +=
                carrierAlpha * (envelope - state->carrier);
            audio[index] =
                (envelope - state->carrier) /
                std::max(state->carrier, 1.0e-5f);
        }
""",
)

verify_path = Path("scripts/verify_hf_integration.py")
verify = verify_path.read_text(encoding="utf-8")
anchor = """    require("startupMuteSamples" in source and
            "detector/carrier settling" in source,
            "HF detector startup transient guard missing")
"""
replacement = """    require("startupMuteSamples" in source and
            "state.mode == DemodMode::AM" in source,
            "HF startup settling is not limited to AM")
    require("Prime the carrier estimator from settled samples" in source and
            "carrierSum" in source,
            "AM carrier acquisition can still normalize against FIR startup zeros")
"""
if verify.count(anchor) != 1:
    raise SystemExit("HF verifier startup assertion anchor missing")
verify_path.write_text(verify.replace(anchor, replacement, 1), encoding="utf-8")
