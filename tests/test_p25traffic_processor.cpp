#include <catch2/catch_all.hpp>

#include "P25ReceiverSession.h"
#include "P25TrafficChannelProcessor.h"

TEST_CASE("P25 Phase 2 audio call key binds session slot and frequency only", "[p25][traffic][session]")
{
    P25P2CallAudioKey first;
    first.nac = 0x2df;
    first.wacn = 0xbee00;
    first.systemId = 0x2d1;
    first.talkgroupId = 30302;
    first.callSessionId = 0x30302abcdULL;
    first.slot = 1;
    first.frequencyHz = 418875000;

    auto second = first;
    REQUIRE(first.valid());
    REQUIRE(second.valid());
    REQUIRE(first == second);

    second.callSessionId = first.callSessionId + 1;
    REQUIRE_FALSE(first == second);

    second = first;
    second.slot = static_cast<uint8_t>(first.slot ^ 0x01u);
    REQUIRE_FALSE(first == second);
}

TEST_CASE("P25 traffic processor advances dibit cursor without internal decode", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(99, 30003, 416550000, 0);

    std::vector<int> dibits(180, 0);
    processor.feedHardDibits(dibits, 5000);
    REQUIRE(processor.getDiag().lastAbsoluteDibit == 5180);

    std::vector<int16_t> raw(90, 1);
    processor.processDibits(raw.data(), raw.size(), 6000);
    REQUIRE(processor.getDiag().lastAbsoluteDibit == 6090);
}

TEST_CASE("P25 traffic processor tracks evidence without opening audio on generic MAC alone", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(42, 30302, 416550000, 0);

    P25LiveDecodeResult result;
    result.stats.phase2VoiceCodewords = 8;
    result.stats.phase2MacPdus = 3;
    result.stats.phase2MacCrcValid = 1;
    result.stats.phase2SuperframeBursts = 9;
    result.stats.phase2MaskedBursts = 9;
    result.stats.phase2MaskPhaseKnown = true;

    processor.observeDecodeResult(result, 1234);
    const auto diag = processor.getDiag();

    REQUIRE(diag.talkgroup == 30302);
    REQUIRE(diag.sessionId == 42);
    REQUIRE(diag.grantedSlot == 0);
    REQUIRE(diag.p2vcw == 8);
    REQUIRE(diag.p2mac == 1);
    REQUIRE(diag.p2macPdus == 3);
    REQUIRE(diag.p2macCrcValid == 1);
    REQUIRE(diag.sfLocked);
    REQUIRE(diag.maskLocked);
    REQUIRE(diag.macTrusted);
    REQUIRE_FALSE(diag.essTrusted);
    REQUIRE_FALSE(diag.encrypted);
    REQUIRE_FALSE(diag.audioOpen);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());
}

TEST_CASE("P25 traffic processor opens sustained audio on clear target-slot session release", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(43, 30302, 416550000, 0);

    P25LiveDecodeResult result;
    result.stats.phase2VoiceCodewords = 1;
    P25Phase2Burst burst;
    burst.valid = true;
    burst.grantSlotKnown = true;
    burst.grantSlot = 0;
    burst.sessionAudioRelease = true;
    burst.encrypted = false;
    burst.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    result.phase2Bursts.push_back(burst);

    processor.observeDecodeResult(result, 5678);
    const auto diag = processor.getDiag();

    REQUIRE(diag.p2bursts == 1);
    REQUIRE(diag.p2vcw == 1);
    REQUIRE_FALSE(diag.encrypted);
    REQUIRE(diag.audioOpen);
    REQUIRE(diag.state == "decoding_sustained");
    REQUIRE(processor.mayEmitSustainedAudio());
}

TEST_CASE("P25 traffic processor closes audio immediately on Phase 2 call-end MAC", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(45, 30302, 416550000, 0);

    P25LiveDecodeResult voice;
    P25Phase2Burst voiceBurst;
    voiceBurst.valid = true;
    voiceBurst.grantSlotKnown = true;
    voiceBurst.grantSlot = 0;
    voiceBurst.sessionAudioRelease = true;
    voiceBurst.encrypted = false;
    voiceBurst.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    voice.phase2Bursts.push_back(voiceBurst);

    processor.observeDecodeResult(voice, 1000);
    REQUIRE(processor.mayEmitSustainedAudio());

    P25LiveDecodeResult ended;
    P25Phase2Burst endBurst;
    endBurst.valid = true;
    endBurst.grantSlotKnown = true;
    endBurst.grantSlot = 0;
    endBurst.macCrcValid = true;
    endBurst.macEndPttSeen = true;
    ended.phase2Bursts.push_back(endBurst);

    processor.observeDecodeResult(ended, 1180);
    const auto diag = processor.getDiag();

    REQUIRE(diag.macEndPttSeen);
    REQUIRE(diag.callEnded);
    REQUIRE(diag.endReason == "end-ptt");
    REQUIRE(diag.state == "end-ptt");
    REQUIRE_FALSE(diag.audioOpen);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());
}

TEST_CASE("P25 traffic processor keeps encrypted calls muted and supports teardown", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(44, 12068, 421350000, 1);

    P25LiveDecodeResult result;
    result.stats.phase2VoiceCodewords = 4;
    P25Phase2Burst burst;
    burst.valid = true;
    burst.grantSlotKnown = true;
    burst.grantSlot = 1;
    burst.essKnown = true;
    burst.encrypted = true;
    burst.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    result.phase2Bursts.push_back(burst);

    processor.observeDecodeResult(result, 9000);
    auto diag = processor.getDiag();
    REQUIRE(diag.encrypted);
    REQUIRE_FALSE(diag.audioOpen);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());

    processor.requestTeardown("unit-test");
    diag = processor.getDiag();
    REQUIRE_FALSE(processor.isCallStillActive());
    REQUIRE(diag.state == "teardown");
    REQUIRE(diag.teardownReason == "unit-test");
}

TEST_CASE("P25 traffic processor does not open sustained audio from masked VCW without clear security", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(46, 30302, 416550000, 0);

    P25LiveDecodeResult result;
    result.stats.phase2VoiceCodewords = 4;
    result.stats.phase2SuperframeBursts = 6;
    result.stats.phase2MaskedBursts = 6;
    result.stats.phase2MaskPhaseKnown = true;

    P25Phase2Burst burst;
    burst.valid = true;
    burst.grantSlotKnown = true;
    burst.grantSlot = 0;
    burst.xorMaskApplied = true;
    burst.superframeLock = true;
    burst.maskPhaseLock = true;
    burst.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    result.phase2Bursts.push_back(burst);

    processor.observeDecodeResult(result, 12000);
    const auto diag = processor.getDiag();

    REQUIRE(diag.p2vcw > 0);
    REQUIRE(diag.maskLocked);
    REQUIRE_FALSE(diag.essTrusted);
    REQUIRE_FALSE(diag.audioOpen);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());
}

TEST_CASE("P25 traffic processor ignores opposite-slot clear evidence for sustained audio", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(47, 30302, 416550000, 0);

    P25LiveDecodeResult result;
    P25Phase2Burst oppositeSlot;
    oppositeSlot.valid = true;
    oppositeSlot.grantSlotKnown = true;
    oppositeSlot.grantSlot = 1;
    oppositeSlot.xorMaskApplied = true;
    oppositeSlot.superframeLock = true;
    oppositeSlot.sessionAudioRelease = true;
    oppositeSlot.essKnown = true;
    oppositeSlot.encrypted = false;
    oppositeSlot.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    result.phase2Bursts.push_back(oppositeSlot);

    processor.observeDecodeResult(result, 14000);
    const auto diag = processor.getDiag();

    REQUIRE_FALSE(diag.essTrusted);
    REQUIRE_FALSE(diag.audioOpen);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());
}
