#include <catch2/catch_all.hpp>
#include "P25Control.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

void writeBitsMsb(std::vector<uint8_t>& bytes, int startBit, int count, uint64_t value)
{
    for (int i = 0; i < count; ++i) {
        const int bit = startBit + count - 1 - i;
        const int byteIndex = bit / 8;
        const int bitInByte = 7 - (bit % 8);
        const uint8_t mask = static_cast<uint8_t>(1u << bitInByte);
        if (value & (1ull << i)) bytes[static_cast<size_t>(byteIndex)] |= mask;
        else bytes[static_cast<size_t>(byteIndex)] &= static_cast<uint8_t>(~mask);
    }
}

std::vector<uint8_t> makeTsbk(uint8_t opcode, uint8_t mfid = 0)
{
    std::vector<uint8_t> b(12, 0);
    writeBitsMsb(b, 0, 1, 0);       // last-block flag, unused here
    writeBitsMsb(b, 1, 1, 0);       // protected flag, unused here
    writeBitsMsb(b, 2, 6, opcode);
    writeBitsMsb(b, 8, 8, mfid);
    return b;
}

} // namespace

TEST_CASE("P25 control analyzer maps identifier updates to channel frequencies")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x3d);

    const uint8_t id = 3;
    const uint32_t baseUnits = static_cast<uint32_t>(450000000.0 / 5.0);
    const uint32_t spacingUnits = static_cast<uint32_t>(12500.0 / 125.0);

    writeBitsMsb(iden, 16, 4, id);
    writeBitsMsb(iden, 20, 9, 0);
    writeBitsMsb(iden, 29, 9, 0x100u); // positive zero offset
    writeBitsMsb(iden, 38, 10, spacingUnits);
    writeBitsMsb(iden, 48, 32, baseUnits);

    auto events = analyzer.ingestTsbk(iden);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().type == P25ControlEventType::IdentifierUpdate);

    const uint16_t channel = static_cast<uint16_t>((id << 12) | 10);
    auto freq = analyzer.channelToFrequencyHz(channel);
    REQUIRE(freq.has_value());
    REQUIRE(*freq == Catch::Approx(450125000.0).margin(1.0));
}

TEST_CASE("P25 control analyzer extracts talkgroups from group voice grants")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x3d);
    writeBitsMsb(iden, 16, 4, 3);
    writeBitsMsb(iden, 29, 9, 0x100u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(450000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto grant = makeTsbk(0x00);
    writeBitsMsb(grant, 16, 8, 0x00);       // service options
    writeBitsMsb(grant, 24, 16, 0x300a);    // table 3, channel 10
    writeBitsMsb(grant, 40, 16, 1234);      // TGID
    writeBitsMsb(grant, 56, 24, 0x112233);  // source

    auto events = analyzer.ingestTsbk(grant);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(ev.talkgroupId == 1234);
    REQUIRE(ev.sourceId == 0x112233);
    REQUIRE(ev.channel == 0x300a);
    REQUIRE(ev.encryptionKnown);
    REQUIRE_FALSE(ev.encrypted);
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase1FDMA);
    REQUIRE_FALSE(ev.phase2Candidate);
    REQUIRE_FALSE(ev.tdmaSlotKnown);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(450125000.0).margin(1.0));
}

TEST_CASE("P25 control analyzer extracts both entries from grant update blocks")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x3d);
    writeBitsMsb(iden, 16, 4, 1);
    writeBitsMsb(iden, 29, 9, 0x100u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(851000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto update = makeTsbk(0x02);
    writeBitsMsb(update, 16, 16, 0x1001);
    writeBitsMsb(update, 32, 16, 2001);
    writeBitsMsb(update, 48, 16, 0x1002);
    writeBitsMsb(update, 64, 16, 2002);

    auto events = analyzer.ingestTsbk(update);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].talkgroupId == 2001);
    REQUIRE_FALSE(events[0].encryptionKnown);
    REQUIRE(events[0].voiceProtocol == P25VoiceProtocol::Phase1FDMA);
    REQUIRE(events[0].voiceFrequencyHz == Catch::Approx(851012500.0).margin(1.0));
    REQUIRE(events[1].talkgroupId == 2002);
    REQUIRE_FALSE(events[1].encryptionKnown);
    REQUIRE(events[1].voiceProtocol == P25VoiceProtocol::Phase1FDMA);
    REQUIRE(events[1].voiceFrequencyHz == Catch::Approx(851025000.0).margin(1.0));
}

TEST_CASE("P25 hex parser accepts spaced and separated bytes")
{
    auto bytes = p25ParseHexBytes("00 01 02:03-04");
    REQUIRE(bytes.size() == 5);
    REQUIRE(bytes[0] == 0x00);
    REQUIRE(bytes[1] == 0x01);
    REQUIRE(bytes[2] == 0x02);
    REQUIRE(bytes[3] == 0x03);
    REQUIRE(bytes[4] == 0x04);
}
