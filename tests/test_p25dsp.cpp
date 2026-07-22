#include <catch2/catch_test_macros.hpp>

#include "P25LiveDecoder.h"
#include "dsp/P25CqpskStagedScorer.h"
#include "dsp/P25DemodStateMachine.h"
#include "dsp/P25FilterCache.h"
#include "dsp/P25Phase2Framer.h"

TEST_CASE("OP25-aligned framer emits 180-dibit burst after sync", "[p25][dsp][framer]")
{
    p25dsp::P25Phase2Framer framer;
    std::vector<int> dibits;
    dibits.reserve(p25dsp::kPhase2BurstDibits);

    // Build one valid sync (20 dibits) followed by payload dibits.
    const auto syncDibits = P25LiveDecoder::phase2FrameSyncDibits();
    for (int dib : syncDibits) {
        dibits.push_back(dib & 0x03);
    }
    while (dibits.size() < p25dsp::kPhase2BurstDibits) {
        dibits.push_back(static_cast<int>(dibits.size() & 0x03));
    }

    framer.consumeDibits(dibits);
    const auto bursts = framer.takeBursts();
    REQUIRE(bursts.size() >= 1);
    REQUIRE(bursts.front().dibits.size() == p25dsp::kPhase2BurstDibits);
}

TEST_CASE("SDRTrunk-aligned sync thresholds gate staged scorer", "[p25][dsp][scorer]")
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
