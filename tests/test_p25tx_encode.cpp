#include <catch2/catch_test_macros.hpp>

#include "IP25AmbeEncoder.h"
#include "P25Phase2TxFramer.h"

#include <cmath>
#include <vector>

TEST_CASE("AMBE silence encoder produces valid 20ms frames", "[p25][tx][encode]")
{
    auto enc = p25CreateAmbeEncoder("silence");
    REQUIRE(enc);
    REQUIRE_FALSE(enc->producesRealSpeech());

    std::vector<float> pcm(160, 0.0f);
    P25AmbeEncodedFrame fr;
    REQUIRE(enc->encodeFrame(pcm.data(), fr));
    REQUIRE(fr.valid);
    REQUIRE(fr.pcmRms == 0.0f);
    for (auto b : fr.bits49) REQUIRE(b == 0);
}

TEST_CASE("AMBE energy placeholder tracks PCM level", "[p25][tx][encode]")
{
    auto enc = p25CreateAmbeEncoder("energy");
    REQUIRE(enc);

    std::vector<float> quiet(160, 0.0f);
    std::vector<float> loud(160, 0.4f);
    P25AmbeEncodedFrame a, b;
    REQUIRE(enc->encodeFrame(quiet.data(), a));
    REQUIRE(enc->encodeFrame(loud.data(), b));
    REQUIRE(b.pcmPeak > a.pcmPeak);
    REQUIRE(b.pcmRms > a.pcmRms);
    // Packed frames should differ when energy differs.
    REQUIRE(a.packed7 != b.packed7);
}

TEST_CASE("Voice packetizer emits one frame per 160 samples", "[p25][tx][encode]")
{
    P25TxVoicePacketizer pkt(p25CreateAmbeEncoder("silence"));
    std::vector<float> pcm(400, 0.1f);
    std::vector<P25AmbeEncodedFrame> out;
    const size_t n = pkt.pushPcm8k(pcm.data(), pcm.size(), out);
    REQUIRE(n == 2);
    REQUIRE(out.size() == 2);
    REQUIRE(pkt.pendingSamples() == 80);
    REQUIRE(pkt.framesEncoded() == 2);
}

TEST_CASE("Phase2 TX framer builds 2160-symbol superframe skeleton", "[p25][tx][encode]")
{
    P25Phase2TxFramer framer;
    P25Phase2TxFramerConfig cfg;
    cfg.nac = 0x2df;
    cfg.wacn = 0xbee00;
    cfg.systemId = 0x2d1;
    cfg.talkgroupId = 30302;
    cfg.unitId = 0x123456;
    cfg.slot = 0;
    framer.setConfig(cfg);

    std::vector<P25AmbeEncodedFrame> frames;
    auto enc = p25CreateAmbeEncoder("energy");
    std::vector<float> pcm(160, 0.2f);
    for (int i = 0; i < 6; ++i) {
        P25AmbeEncodedFrame fr;
        REQUIRE(enc->encodeFrame(pcm.data(), fr));
        frames.push_back(fr);
    }

    auto sf = framer.buildSuperframe(frames);
    REQUIRE(sf.valid);
    REQUIRE(sf.dibits.size() == 2160);
    REQUIRE(sf.voiceFramesUsed == 6);
    REQUIRE(framer.maskSeed() != 0);
}
