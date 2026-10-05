#include <catch2/catch_all.hpp>

#include "P25ReceiverSession.h"
#include "P25TrafficChannelProcessor.h"
#include "Receiver.h"

#include <chrono>
#include <thread>

TEST_CASE("P25 Phase 2 audio call key binds selected allocation identity", "[p25][traffic][session]")
{
    P25P2CallAudioKey first;
    first.nac = 0x2df;
    first.wacn = 0xbee00;
    first.systemId = 0x2d1;
    first.talkgroupId = 30302;
    first.sourceId = 0x1ed289;
    first.callSessionId = 0x30302abcdULL;
    first.grantEpochMs = 1'000'000;
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

    second = first;
    second.nac = static_cast<uint16_t>(first.nac + 1);
    second.wacn = first.wacn + 1;
    second.systemId = static_cast<uint16_t>(first.systemId + 1);
    REQUIRE(first == second);

    second = first;
    second.sourceId = 0;
    REQUIRE(first == second);

    second = first;
    second.sourceId = 0x445566;
    REQUIRE(first == second);

    second = first;
    second.grantEpochMs = first.grantEpochMs + 500;
    REQUIRE_FALSE(first == second);
}

TEST_CASE("P25 grant refresh preserves PTT generation and call session", "[p25][traffic][session]")
{
    Receiver rx;
    rx.p25VoiceTalkgroupId = 30302;
    p25Phase2BeginNewPtt(rx, 1'000'000);
    const uint64_t firstSession = rx.p25CurrentCallSessionId;
    const uint64_t firstPtt = rx.p25PttGeneration;
    REQUIRE(firstSession != 0);
    REQUIRE(firstPtt == 1);

    p25Phase2RefreshGrantEpoch(rx, 1'000'500);
    REQUIRE(rx.p25CurrentCallSessionId == firstSession);
    REQUIRE(rx.p25PttGeneration == firstPtt);
    REQUIRE(rx.p25VoiceGrantEpochMs == 1'000'000);

    p25Phase2BeginNewPtt(rx, 1'001'000);
    REQUIRE(rx.p25PttGeneration == firstPtt + 1);
    REQUIRE(rx.p25CurrentCallSessionId != firstSession);
}

TEST_CASE("P25 new PTT clears Phase 2 call-bound audio and security state", "[p25][traffic][session]")
{
    Receiver rx;
    rx.p25VoiceTalkgroupId = 30003;
    p25Phase2BeginNewPtt(rx, 1'000'000);

    rx.p25SessionState.callSecurityLatch = P25CallSecurityLatch::Clear;
    rx.p25SessionState.pendingAudio.armed = true;
    rx.p25SessionState.pendingAudio.ambeFrames.push_back(P25P2PendingAmbeFrame{});
    rx.p25SessionState.ambeDedupe.talkgroupId = 30003;
    rx.p25SessionState.frameSequencer.armed = true;
    rx.p25SessionState.audioTail.consecutivePlayoutBridgeFrames = 7;
    rx.p25SessionState.sustain.hadSuccessfulEmit = true;

    p25Phase2BeginNewPtt(rx, 1'002'000);

    REQUIRE(rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Unknown);
    REQUIRE_FALSE(rx.p25SessionState.pendingAudio.armed);
    REQUIRE(rx.p25SessionState.pendingAudio.ambeFrames.empty());
    REQUIRE(rx.p25SessionState.ambeDedupe.talkgroupId == 0);
    REQUIRE_FALSE(rx.p25SessionState.frameSequencer.armed);
    REQUIRE(rx.p25SessionState.audioTail.consecutivePlayoutBridgeFrames == 0);
    REQUIRE_FALSE(rx.p25SessionState.sustain.hadSuccessfulEmit);
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

TEST_CASE("P25 traffic observer respects end and next PTT order", "[p25][traffic][response-order]")
{
    // DEC-0192: a batch can span the end of one caller and the next caller's
    // PTT. The final selected-slot event, not any earlier END, owns its state.
    P25TrafficChannelProcessor processor(99, 30003, 416550000, 0);
    P25Phase2Burst end;
    end.valid = true;
    end.grantSlotKnown = true;
    end.grantSlot = 0;
    end.macCrcValid = true;
    end.macEndPttSeen = true;
    P25Phase2Burst start = end;
    start.macEndPttSeen = false;
    start.macPttSeen = true;
    start.essObservedThisBurst = true;
    start.essKnown = true;
    start.sessionAudioRelease = true;
    P25Phase2Burst voice = start;
    voice.macPttSeen = false;
    voice.essObservedThisBurst = false;
    voice.xorMaskApplied = true;
    voice.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    P25LiveDecodeResult result;
    SECTION("a new selected-slot PTT supersedes the old end") {
        result.phase2Bursts = {end, start, voice};
        processor.observeDecodeResult(result, 2000);
        REQUIRE_FALSE(processor.getDiag().callEnded);
        REQUIRE(processor.mayEmitSustainedAudio());
    }
    SECTION("a later end still closes the selected slot") {
        result.phase2Bursts = {start, voice, end};
        processor.observeDecodeResult(result, 2000);
        REQUIRE(processor.getDiag().callEnded);
        REQUIRE_FALSE(processor.mayEmitSustainedAudio());
    }
    SECTION("another slot cannot restart the ended selected slot") {
        start.grantSlot = 1;
        voice.grantSlot = 1;
        result.phase2Bursts = {end, start, voice};
        processor.observeDecodeResult(result, 2000);
        REQUIRE(processor.getDiag().callEnded);
        REQUIRE_FALSE(processor.mayEmitSustainedAudio());
    }
}

TEST_CASE("P25 traffic repeated idle does not extend an ended call", "[p25][traffic][response-order]")
{
    P25TrafficChannelProcessor processor(100, 30003, 416550000, 0);
    P25Phase2Burst end;
    end.valid = true;
    end.grantSlotKnown = true;
    end.grantSlot = 0;
    end.macCrcValid = true;
    end.macEndPttSeen = true;
    P25LiveDecodeResult result;
    result.phase2Bursts = {end};
    processor.observeDecodeResult(result, 1000);
    const auto firstEnd = processor.getDiag().endedMs;
    // Cross one monotonic-clock tick; this tests timestamp stability, not a
    // decoder/hold timing threshold. No production timeout is changed.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    end.macEndPttSeen = false;
    end.macIdleSeen = true;
    result.phase2Bursts = {end};
    processor.observeDecodeResult(result, 2000);
    REQUIRE(processor.getDiag().callEnded);
    REQUIRE(processor.getDiag().endedMs == firstEnd);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());
}

TEST_CASE("P25 traffic observer does not replay overlapping call boundaries", "[p25][traffic][response-order]")
{
    P25TrafficChannelProcessor processor(101, 30003, 416550000, 0);
    P25Phase2Burst start;
    start.valid = true;
    start.grantSlotKnown = true;
    start.grantSlot = 0;
    start.macCrcValid = true;
    start.macPttSeen = true;
    start.streamBurstStartDibitKnown = true;
    start.streamBurstStartDibit = 1000;
    start.essObservedThisBurst = true;
    start.essKnown = true;
    P25Phase2Burst end = start;
    end.macPttSeen = false;
    end.macEndPttSeen = true;
    end.essObservedThisBurst = false;
    end.streamBurstStartDibit = 2000;
    P25LiveDecodeResult result;
    // Deliberately not vector order: absolute capture position wins.
    result.phase2Bursts = {end, start};
    processor.observeDecodeResult(result, 3000);
    REQUIRE(processor.getDiag().callEnded);
    const auto firstEnd = processor.getDiag().endedMs;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    processor.observeDecodeResult(result, 4000);
    REQUIRE(processor.getDiag().callEnded);
    REQUIRE(processor.getDiag().endedMs == firstEnd);

    start.streamBurstStartDibit = 5000;
    start.essObservedThisBurst = false;
    start.essKnown = false;
    result.phase2Bursts = {start, end};
    processor.observeDecodeResult(result, 6000);
    REQUIRE_FALSE(processor.getDiag().callEnded);
    REQUIRE_FALSE(processor.getDiag().audioOpen);
    // Old context from the first caller cannot end the reply or renew its clock.
    result.phase2Bursts = {end};
    const auto lastActive = processor.getDiag().lastActiveMs;
    processor.observeDecodeResult(result, 7000);
    REQUIRE_FALSE(processor.getDiag().callEnded);
    REQUIRE(processor.getDiag().lastActiveMs == lastActive);

    end.streamBurstStartDibit = 8000;
    result.phase2Bursts = {end};
    processor.observeDecodeResult(result, 9000);
    REQUIRE(processor.getDiag().callEnded);
    REQUIRE(processor.getDiag().endedMs > firstEnd);
}

TEST_CASE("P25 traffic next PTT cannot inherit previous caller security", "[p25][traffic][response-order]")
{
    P25TrafficChannelProcessor processor(102, 30003, 416550000, 0);
    P25Phase2Burst first;
    first.valid = true;
    first.grantSlotKnown = true;
    first.grantSlot = 0;
    first.essObservedThisBurst = true;
    first.essKnown = true;
    first.sessionAudioRelease = true;
    first.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    P25Phase2Burst next;
    next.valid = true;
    next.grantSlotKnown = true;
    next.grantSlot = 0;
    next.macCrcValid = true;
    next.macPttSeen = true;
    P25LiveDecodeResult result;
    result.phase2Bursts = {first, next};
    processor.observeDecodeResult(result, 3000);
    REQUIRE_FALSE(processor.getDiag().callEnded);
    REQUIRE_FALSE(processor.getDiag().essTrusted);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());
}

TEST_CASE("P25 traffic activity excludes companion and stale voice", "[p25][traffic][response-order]")
{
    // Capture003120 at00:32:35: selected TG10120/s0 is in hangtime, while
    // companion TG12068/s1 still has VCWs. It must not count as selected voice.
    P25TrafficChannelProcessor processor(103, 10120, 420100000, 0);
    P25Phase2Burst selected;
    selected.valid = true;
    selected.grantSlotKnown = true;
    selected.grantSlot = 0;
    selected.trafficTalkgroupKnown = true;
    selected.trafficTalkgroupId = 10120;
    selected.xorMaskApplied = true;
    selected.streamBurstStartDibitKnown = true;
    selected.streamBurstStartDibit = 1000;
    selected.voiceCodewords.resize(4);
    auto companion = selected;
    companion.grantSlot = 1;
    companion.trafficTalkgroupId = 12068;
    companion.streamBurstStartDibit = 1180;
    companion.voiceCodewords.resize(12);
    P25LiveDecodeResult result;
    result.phase2Bursts = {selected, companion};
    result.stats.phase2VoiceCodewords = 16;
    processor.observeDecodeResult(result, 2000);
    REQUIRE(processor.getDiag().p2vcw == 4);
    REQUIRE(processor.getDiag().p2AllSlotVcw == 16);
    processor.observeDecodeResult(result, 3000);
    REQUIRE(processor.getDiag().p2vcw == 0);
    selected.voiceCodewords.clear();
    selected.macHangtimeSeen = true;
    selected.streamBurstStartDibit = 4000;
    companion.streamBurstStartDibit = 4180;
    result.phase2Bursts = {selected, companion};
    processor.observeDecodeResult(result, 5000);
    REQUIRE(processor.getDiag().p2vcw == 0);
    REQUIRE(processor.getDiag().callEnded);
    REQUIRE_FALSE(processor.mayEmitSustainedAudio());
    selected.macHangtimeSeen = false;
    selected.voiceCodewords.resize(4);
    selected.xorMaskApplied = false;
    selected.streamBurstStartDibit = 6000;
    result.phase2Bursts = {selected};
    processor.observeDecodeResult(result, 7000);
    REQUIRE(processor.getDiag().p2vcw == 0);
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
    REQUIRE(diag.p2vcw == 0); // no selected burst proof in aggregate stats
    REQUIRE(diag.p2AllSlotVcw == 8);
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
    burst.xorMaskApplied = true;
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
    voiceBurst.xorMaskApplied = true;
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

TEST_CASE("P25 traffic processor ignores sticky non-observed encrypted ESS paint", "[p25][traffic]")
{
    P25TrafficChannelProcessor processor(48, 30302, 421225000, 1);

    P25LiveDecodeResult result;
    result.stats.phase2VoiceCodewords = 4;
    P25Phase2Burst burst;
    burst.valid = true;
    burst.grantSlotKnown = true;
    burst.grantSlot = 1;
    burst.essKnown = true;
    burst.essEncrypted = true;
    burst.encrypted = true;
    burst.essObservedThisBurst = false; // sticky session paint only
    burst.voiceCodewords.push_back(P25Phase2VoiceCodeword{});
    result.phase2Bursts.push_back(burst);

    processor.observeDecodeResult(result, 9500);
    const auto diag = processor.getDiag();
    REQUIRE_FALSE(diag.encrypted);
    REQUIRE_FALSE(diag.essTrusted);
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
    burst.essEncrypted = true;
    burst.encrypted = true;
    burst.essObservedThisBurst = true;
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
