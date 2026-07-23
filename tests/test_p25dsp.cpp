#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "P25LiveDecoder.h"
#include "dsp/P25CqpskStagedScorer.h"
#include "dsp/P25DemodStateMachine.h"
#include "dsp/P25FilterCache.h"
#include "dsp/P25Phase2Framer.h"
#include "dsp/P25StreamingChannelDdc.h"

#include <cmath>
#include <numeric>

namespace {

void applyFirReference(std::vector<std::complex<float>>& x, const std::vector<double>& taps)
{
    if (x.empty() || taps.empty()) return;
    const std::vector<std::complex<float>> input = x;
    const int half = static_cast<int>(taps.size() / 2);
    for (size_t n = 0; n < x.size(); ++n) {
        double accI = 0.0;
        double accQ = 0.0;
        for (size_t k = 0; k < taps.size(); ++k) {
            const int idx = static_cast<int>(n) + static_cast<int>(k) - half;
            if (idx < 0 || idx >= static_cast<int>(input.size())) continue;
            const double w = taps[k];
            accI += static_cast<double>(input[static_cast<size_t>(idx)].real()) * w;
            accQ += static_cast<double>(input[static_cast<size_t>(idx)].imag()) * w;
        }
        x[n] = {static_cast<float>(accI), static_cast<float>(accQ)};
    }
}

std::vector<int> makeBurstAfterSync(size_t payloadDibits)
{
    std::vector<int> dibits;
    dibits.reserve(p25dsp::kPhase2FrameSyncDibits + payloadDibits);
    for (int dib : P25LiveDecoder::phase2FrameSyncDibits()) {
        dibits.push_back(dib & 0x03);
    }
    while (dibits.size() < p25dsp::kPhase2FrameSyncDibits + payloadDibits) {
        dibits.push_back(static_cast<int>(dibits.size() & 0x03));
    }
    return dibits;
}

std::vector<int> makeFullBurst()
{
    return makeBurstAfterSync(p25dsp::kPhase2BurstDibits - p25dsp::kPhase2FrameSyncDibits);
}

} // namespace

TEST_CASE("OP25-aligned framer emits 180-dibit burst after sync", "[p25][dsp][framer]")
{
    p25dsp::P25Phase2Framer framer;
    framer.consumeDibits(makeFullBurst());
    const auto bursts = framer.takeBursts();
    REQUIRE(bursts.size() >= 1);
    REQUIRE(bursts.front().dibits.size() == p25dsp::kPhase2BurstDibits);
}

TEST_CASE("Phase-2 framer sync allowance expires after ten complete bursts", "[p25][dsp][framer]")
{
    p25dsp::P25Phase2Framer framer;
    framer.consumeDibits(makeFullBurst());
    framer.takeBursts();
    REQUIRE(framer.synchronized());
    REQUIRE(framer.inSyncAllowance() == 9);

    std::vector<int> payloadBurst(p25dsp::kPhase2BurstDibits);
    for (size_t i = 0; i < payloadBurst.size(); ++i) {
        payloadBurst[i] = static_cast<int>((i + 3) & 0x03);
    }

    for (int burst = 0; burst < 9; ++burst) {
        framer.consumeDibits(payloadBurst);
        framer.takeBursts();
    }

    REQUIRE(framer.inSyncAllowance() == 0);
    REQUIRE_FALSE(framer.synchronized());

    framer.consumeDibits(payloadBurst);
    REQUIRE(framer.takeBursts().empty());
}

TEST_CASE("Phase-2 framer superframe ring continues beyond 1440 dibits", "[p25][dsp][framer]")
{
    p25dsp::P25Phase2Framer framer;
    std::vector<int> stream;
    stream.reserve(1500);
    for (size_t i = 0; i < 1500; ++i) {
        stream.push_back(static_cast<int>(i & 0x03));
    }
    framer.consumeDibits(stream);
    REQUIRE(framer.absoluteDibitCursor() == 1500);
}

TEST_CASE("Phase-2 framer rejects single-good superframe sync when synchronized", "[p25][dsp][framer]")
{
    p25dsp::P25Phase2Framer framer;
    std::vector<int> stream;
    stream.reserve(p25dsp::kPhase2SuperframeDibits + 40);

    for (int dib : P25LiveDecoder::phase2FrameSyncDibits()) {
        stream.push_back(dib & 0x03);
    }
    while (stream.size() < p25dsp::kPhase2SuperframeDibits) {
        stream.push_back(0);
    }
    for (int dib : P25LiveDecoder::phase2FrameSyncDibits()) {
        stream.push_back((~dib) & 0x03);
    }

    framer.consumeDibits(stream);
    const auto frames = framer.takeSuperframes();
    REQUIRE(frames.empty());
}

TEST_CASE("Phase-2 framer accepts dual-sync superframe when both markers valid", "[p25][dsp][framer]")
{
    p25dsp::P25Phase2Framer framer;
    std::vector<int> stream;
    stream.reserve(p25dsp::kPhase2SuperframeDibits + 40);
    const auto sync = P25LiveDecoder::phase2FrameSyncDibits();

    for (size_t block = 0; block < 4; ++block) {
        if (block == 2 || block == 3) {
            for (int dib : sync) stream.push_back(dib & 0x03);
        }
        while (stream.size() < (block + 1) * 180) {
            stream.push_back(static_cast<int>((stream.size() + 1) & 0x03));
        }
    }
    while (stream.size() < p25dsp::kPhase2SuperframeDibits + 20) {
        stream.push_back(static_cast<int>(stream.size() & 0x03));
    }

    framer.consumeDibits(stream);
    REQUIRE(framer.takeSuperframes().size() >= 1);
}

TEST_CASE("Staged CQPSK gate rejects noise and accepts sync preview", "[p25][dsp][gate]")
{
    std::vector<int> noise(240, 0);
    const auto preview = p25dsp::scorePhase2SyncPreview(noise, false);
    REQUIRE_FALSE(preview.plausible);
    REQUIRE_FALSE(p25dsp::passesStagedCqpskGate(
        preview, p25dsp::P25DemodState::TrackingSoft, false));

    std::vector<int> syncStream;
    syncStream.reserve(240);
    const auto syncDibits = P25LiveDecoder::phase2FrameSyncDibits();
    for (int dib : syncDibits) {
        syncStream.push_back(dib & 0x03);
    }
    const auto good = p25dsp::scorePhase2SyncPreview(syncStream, false);
    REQUIRE(good.plausible);
    REQUIRE(good.bestSyncErrors <= p25dsp::kSyncThresholdUnsynchronized);
}

TEST_CASE("Filter cache avoids repeated tap design", "[p25][dsp][filter]")
{
    p25dsp::P25FilterCache cache;
    const auto& a = cache.lowpassTaps(48000.0, 6500.0, 700.0, 161);
    const auto& b = cache.lowpassTaps(48000.0, 6500.0, 700.0, 161);
    REQUIRE(a.data() == b.data());
    REQUIRE(cache.designCalls() == 1);
}

TEST_CASE("Demod state machine tightens candidate budget while tracking", "[p25][dsp][demod]")
{
    p25dsp::P25DemodStateMachine machine;
    machine.noteCarrierStable(true);
    machine.noteTimingStable(true);
    for (int i = 0; i < 6; ++i) {
        machine.noteCarrierStable(true);
        machine.noteTimingStable(true);
    }
    for (int i = 0; i < 8; ++i) {
        machine.noteSyncHit(2, p25dsp::kSyncThresholdSynchronized);
    }
    machine.noteProtocolEvidence(true, true, true);
    for (int i = 0; i < 3; ++i) {
        machine.noteProtocolEvidence(true, true, true);
    }
    REQUIRE(machine.state() == p25dsp::P25DemodState::TrackingHard);
    REQUIRE(machine.maxTimingPhases() == 1);
    REQUIRE(machine.maxMappingCandidates() == 1);
    REQUIRE_FALSE(machine.allowFullGridSearch());
}

TEST_CASE("Streaming DDC uses persistent NCO without per-block tap design", "[p25][dsp][ddc]")
{
    p25dsp::P25FilterCache cache;
    p25dsp::P25StreamingChannelDdc ddc;
    P25LiveDecoderConfig config;
    config.workSampleRate = 48000.0;
    config.symbolRate = 6000.0;
    config.channelBandwidthHz = 12500.0;

    std::vector<std::complex<float>> iq(4096);
    for (size_t i = 0; i < iq.size(); ++i) {
        const float phase = static_cast<float>(i) * 0.01f;
        iq[i] = {std::cos(phase), std::sin(phase)};
    }

    const auto first = ddc.process(iq, 2400000.0, 850000000.0, 850006250.0, config, cache);
    const auto second = ddc.process(iq, 2400000.0, 850000000.0, 850006250.0, config, cache);
    REQUIRE(first.samples.size() >= 2);
    REQUIRE(second.samples.size() >= 2);
    REQUIRE(cache.designCalls() >= 1);
    REQUIRE(cache.designCalls() <= 4);
}

TEST_CASE("Streaming FIR state is partition invariant", "[p25][dsp][ddc]")
{
    p25dsp::P25FilterCache cache;
    const auto& taps = cache.lowpassTaps(96000.0, 5000.0, 1000.0, 161);

    std::vector<std::complex<float>> iq(8192);
    for (size_t i = 0; i < iq.size(); ++i) {
        const float phase = static_cast<float>(i) * 0.017f;
        iq[i] = {std::cos(phase), std::sin(phase)};
    }

    p25dsp::P25StreamingFirState wholeFir;
    std::vector<std::complex<float>> whole = iq;
    wholeFir.processInPlace(whole, taps);

    p25dsp::P25StreamingFirState splitFir;
    std::vector<std::complex<float>> first(iq.begin(), iq.begin() + 4096);
    std::vector<std::complex<float>> second(iq.begin() + 4096, iq.end());
    splitFir.processInPlace(first, taps);
    splitFir.processInPlace(second, taps);
    std::vector<std::complex<float>> split;
    split.insert(split.end(), first.begin(), first.end());
    split.insert(split.end(), second.begin(), second.end());

    REQUIRE(whole.size() == split.size());
    const size_t splitSkip = taps.size();
    double maxAbsDiff = 0.0;
    for (size_t i = splitSkip; i < whole.size(); ++i) {
        maxAbsDiff = std::max(maxAbsDiff, static_cast<double>(std::abs(whole[i] - split[i])));
    }
    REQUIRE(maxAbsDiff < 1e-4);
}

TEST_CASE("Streaming DDC output is partition invariant across block boundaries", "[p25][dsp][ddc]")
{
    p25dsp::P25FilterCache cacheA;
    p25dsp::P25FilterCache cacheB;
    p25dsp::P25StreamingChannelDdc wholeDdc;
    p25dsp::P25StreamingChannelDdc splitDdc;

    P25LiveDecoderConfig config;
    config.workSampleRate = 96000.0;
    config.symbolRate = 6000.0;
    config.channelBandwidthHz = 12500.0;

    constexpr double kSampleRate = 96000.0;
    std::vector<std::complex<float>> iq(8192);
    for (size_t i = 0; i < iq.size(); ++i) {
        const float phase = static_cast<float>(i) * 0.013f;
        iq[i] = {std::cos(phase), std::sin(phase)};
    }

    const auto whole = wholeDdc.process(iq, kSampleRate, 850000000.0, 850006250.0, config, cacheA);

    const size_t splitAt = 4096;
    std::vector<std::complex<float>> first(iq.begin(), iq.begin() + static_cast<std::ptrdiff_t>(splitAt));
    std::vector<std::complex<float>> second(iq.begin() + static_cast<std::ptrdiff_t>(splitAt), iq.end());
    const auto part1 = splitDdc.process(first, kSampleRate, 850000000.0, 850006250.0, config, cacheB);
    const auto part2 = splitDdc.process(second, kSampleRate, 850000000.0, 850006250.0, config, cacheB);

    std::vector<std::complex<float>> split;
    split.insert(split.end(), part1.samples.begin(), part1.samples.end());
    split.insert(split.end(), part2.samples.begin(), part2.samples.end());

    REQUIRE(whole.samples.size() >= 32);
    REQUIRE(split.size() >= 32);
    REQUIRE(std::abs(static_cast<long>(whole.samples.size()) - static_cast<long>(split.size())) <= 2);

    const size_t compareStart = 80;
    const size_t compareCount = std::min(whole.samples.size(), split.size()) - compareStart;
    REQUIRE(compareCount >= 16);

    double maxAbsDiff = 0.0;
    for (size_t i = 0; i < compareCount; ++i) {
        const auto& a = whole.samples[compareStart + i];
        const auto& b = split[compareStart + i];
        maxAbsDiff = std::max(maxAbsDiff, static_cast<double>(std::abs(a - b)));
    }
    REQUIRE(maxAbsDiff < 0.02);
}
