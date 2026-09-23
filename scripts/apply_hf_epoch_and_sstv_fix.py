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
    "src/Demod.cpp",
    """    // HF_RECEIVE_RESET_BEGIN
    if (HfDemod::supports(lastResetMode)) HfDemod::reset(this);
    // HF_RECEIVE_RESET_END
""",
    """    // HF_RECEIVE_RESET_BEGIN
    // reset() is a no-op when this Demodulator has never entered an HF mode.
    // Do not rely on the legacy lastResetMode field: the isolated HF delegate
    // returns before the legacy narrowband state machine updates that field.
    HfDemod::reset(this);
    // HF_RECEIVE_RESET_END
""",
)

replace_exact(
    "scripts/verify_no_p25_changes.py",
    """HF_RESET_BLOCK = '    // HF_RECEIVE_RESET_BEGIN\\n    if (HfDemod::supports(lastResetMode)) HfDemod::reset(this);\\n    // HF_RECEIVE_RESET_END\\n'
""",
    """HF_RESET_BLOCK = '    // HF_RECEIVE_RESET_BEGIN\\n    // reset() is a no-op when this Demodulator has never entered an HF mode.\\n    // Do not rely on the legacy lastResetMode field: the isolated HF delegate\\n    // returns before the legacy narrowband state machine updates that field.\\n    HfDemod::reset(this);\\n    // HF_RECEIVE_RESET_END\\n'
""",
)

replace_exact(
    "scripts/verify_hf_integration.py",
    """    require("HfDemod::supports(lastResetMode)" in demod,
            "HF reset is not limited to the previous HF mode")
""",
    """    require("HfDemod::reset(this);" in demod and
            "HfDemod::supports(lastResetMode)" not in demod,
            "HF stream-epoch reset is conditional on stale legacy mode state")
""",
)

satcom_path = Path("src/SatcomScannerEngine.cpp")
satcom = satcom_path.read_text(encoding="utf-8")
replace_pairs = [
    (
        """    FmMultiplexBlock* multiplexOutput = nfmSstv ? &multiplex : nullptr;
""",
        """    // Request the demodulator's clean pre-squelch decoder block for
    // both FM and sideband SSTV. Reconstructing USB/LSB input from speaker
    // audio loses provenance and can include output gain/gating artifacts.
    FmMultiplexBlock* multiplexOutput =
        (nfmSstv || ssbSstv) ? &multiplex : nullptr;
""",
    ),
    (
        """    if (nfmSstv && !multiplex.samples.empty()) {
        sstvFeed_->publish(multiplex,
                           sstvSourceEpoch_.load(std::memory_order_acquire),
                           DemodMode::NFM);
    }
""",
        """    if ((nfmSstv || ssbSstv) && !multiplex.samples.empty()) {
        sstvFeed_->publish(multiplex,
                           sstvSourceEpoch_.load(std::memory_order_acquire),
                           demodMode);
    }
""",
    ),
    (
        """    if (ssbSstv) {
        size_t offset = 0;
        while (offset < audio.size()) {
            const size_t count = std::min(
                SstvInputEvent::maxSamples, audio.size() - offset);
            FmMultiplexBlock block;
            block.samples.assign(audio.begin() + static_cast<std::ptrdiff_t>(offset),
                                 audio.begin() + static_cast<std::ptrdiff_t>(offset + count));
            block.sampleRate = 48000.0;
            block.targetHz = lockFrequencyHz;
            block.epoch = sstvSourceEpoch_.load(std::memory_order_acquire);
            block.firstSample = sstvAudioFirstSample_.fetch_add(
                count, std::memory_order_acq_rel);
            block.discontinuity = false;
            sstvFeed_->publish(block, block.epoch, demodMode);
            offset += count;
        }
    }

""",
        "",
    ),
]
for old, new in replace_pairs:
    count = satcom.count(old)
    if count != 1:
        raise SystemExit(
            f"src/SatcomScannerEngine.cpp: expected one guarded block, found {count}"
        )
    satcom = satcom.replace(old, new, 1)
satcom_path.write_text(satcom, encoding="utf-8")

verify_path = Path("scripts/verify_hf_integration.py")
verify = verify_path.read_text(encoding="utf-8")
replace_exact_anchor = """    docs = text("docs/HF_RECEIVE.md")
"""
if replace_exact_anchor not in verify:
    raise SystemExit("HF verifier source-list anchor missing")
verify = verify.replace(
    replace_exact_anchor,
    replace_exact_anchor + "    satcom = text(\"src/SatcomScannerEngine.cpp\")\n",
    1,
)
verify_anchor = """    require("startupMuteSamples" in source and
            "detector/carrier settling" in source,
            "HF detector startup transient guard missing")
"""
verify_addition = verify_anchor + """    require("(nfmSstv || ssbSstv) ? &multiplex : nullptr" in satcom,
            "Satcom sideband SSTV does not request the clean HF decoder block")
    require("if ((nfmSstv || ssbSstv) && !multiplex.samples.empty())" in satcom,
            "Satcom does not publish USB/LSB decoder provenance")
    require("block.samples.assign(audio.begin()" not in satcom,
            "Satcom still reconstructs SSTV input from speaker audio")
"""
if verify.count(verify_anchor) != 1:
    raise SystemExit("HF verifier assertion anchor missing")
verify = verify.replace(verify_anchor, verify_addition, 1)
verify_path.write_text(verify, encoding="utf-8")

test_path = Path("tests/test_hf_demod.cpp")
tests = test_path.read_text(encoding="utf-8")
test_anchor = """TEST_CASE("HF dispatch is limited to AM USB LSB and CW", "[hf][guard]") {
"""
new_test = """TEST_CASE("HF explicit reset starts a new decoder epoch", "[hf][sstv][reset]") {
    constexpr double inputRate = 192000.0;
    const auto iq = analyticTone(inputRate, 0.25, 1900.0, 0.15f);

    Demodulator demod;
    FmMultiplexBlock firstTap;
    FmMultiplexBlock continuousTap;
    FmMultiplexBlock resetTap;
    processHf(demod, iq, inputRate, DemodMode::USB, 6000.0, 3000.0,
              &firstTap);
    processHf(demod, iq, inputRate, DemodMode::USB, 6000.0, 3000.0,
              &continuousTap);

    REQUIRE_FALSE(firstTap.samples.empty());
    REQUIRE_FALSE(continuousTap.discontinuity);
    demod.resetState();
    processHf(demod, iq, inputRate, DemodMode::USB, 6000.0, 3000.0,
              &resetTap);

    REQUIRE_FALSE(resetTap.samples.empty());
    REQUIRE(resetTap.discontinuity);
    REQUIRE(resetTap.epoch > continuousTap.epoch);
    REQUIRE(resetTap.firstSample == 0);
}

"""
if tests.count(test_anchor) != 1:
    raise SystemExit("HF test insertion anchor missing")
tests = tests.replace(test_anchor, new_test + test_anchor, 1)
test_path.write_text(tests, encoding="utf-8")

replace_exact(
    "docs/HF_RECEIVE.md",
    """- SSB decoder audio is published with sample provenance and discontinuity
  information for live HF SSTV.
""",
    """- The Satcom/HF receive session publishes clean pre-squelch SSB decoder
  audio with sample provenance and discontinuity information for live HF SSTV.
""",
)
