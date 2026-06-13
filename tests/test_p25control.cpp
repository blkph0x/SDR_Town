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

TEST_CASE("P25 control analyzer does not treat Motorola regroup add as voice grant")
{
    P25ControlChannelAnalyzer analyzer;
    const auto bytes = p25ParseHexBytes("00 90 27 88 27 88 27 88 27 88 31 4A");

    const auto events = analyzer.ingestTsbk(bytes);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::VendorCommand);
    REQUIRE(ev.opcode == 0x00);
    REQUIRE(ev.mfid == 0x90);
    REQUIRE(ev.talkgroupId == 0);
    REQUIRE(ev.channel == 0);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(0.0));
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Unknown);
    REQUIRE_FALSE(ev.tdmaSlotKnown);
}

TEST_CASE("P25 control analyzer maps TDMA identifiers to slot-aware channel frequencies")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);

    const uint8_t id = 4;
    const uint8_t channelType = 3; // common two-slot Phase 2 traffic profile
    const uint32_t baseUnits = static_cast<uint32_t>(450000000.0 / 5.0);
    const uint32_t spacingUnits = static_cast<uint32_t>(12500.0 / 125.0);

    writeBitsMsb(iden, 16, 4, id);
    writeBitsMsb(iden, 20, 4, channelType);
    writeBitsMsb(iden, 24, 14, 0x2000u); // positive zero offset
    writeBitsMsb(iden, 38, 10, spacingUnits);
    writeBitsMsb(iden, 48, 32, baseUnits);

    auto events = analyzer.ingestTsbk(iden);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::IdentifierUpdate);
    REQUIRE(ev.identifierKnown);
    REQUIRE(ev.identifier == id);
    REQUIRE(ev.channelType == channelType);
    REQUIRE(ev.slotsPerCarrier == 2);
    REQUIRE(ev.phase2Candidate);
    REQUIRE(ev.baseFrequencyHz == Catch::Approx(450000000.0).margin(1.0));
    REQUIRE(ev.channelSpacingHz == Catch::Approx(12500.0).margin(1.0));

    auto slot0Freq = analyzer.channelToFrequencyHz(static_cast<uint16_t>((id << 12) | 0));
    auto slot1Freq = analyzer.channelToFrequencyHz(static_cast<uint16_t>((id << 12) | 1));
    auto nextSlot0Freq = analyzer.channelToFrequencyHz(static_cast<uint16_t>((id << 12) | 2));
    REQUIRE(slot0Freq.has_value());
    REQUIRE(slot1Freq.has_value());
    REQUIRE(nextSlot0Freq.has_value());
    REQUIRE(*slot0Freq == Catch::Approx(450000000.0).margin(1.0));
    REQUIRE(*slot1Freq == Catch::Approx(450000000.0).margin(1.0));
    REQUIRE(*nextSlot0Freq == Catch::Approx(450012500.0).margin(1.0));
}

TEST_CASE("P25 control analyzer marks TDMA voice grants with protocol and slot")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);
    writeBitsMsb(iden, 16, 4, 4);
    writeBitsMsb(iden, 20, 4, 3);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(450000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto grant = makeTsbk(0x00);
    writeBitsMsb(grant, 16, 8, 0x00);
    writeBitsMsb(grant, 24, 16, 0x4003);
    writeBitsMsb(grant, 40, 16, 77);
    writeBitsMsb(grant, 56, 24, 0x010203);

    auto events = analyzer.ingestTsbk(grant);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(ev.talkgroupId == 77);
    REQUIRE(ev.identifierKnown);
    REQUIRE(ev.identifier == 4);
    REQUIRE(ev.channelType == 3);
    REQUIRE(ev.slotsPerCarrier == 2);
    REQUIRE(ev.baseFrequencyHz == Catch::Approx(450000000.0).margin(1.0));
    REQUIRE(ev.channelSpacingHz == Catch::Approx(12500.0).margin(1.0));
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA);
    REQUIRE(ev.phase2Candidate);
    REQUIRE(ev.tdmaSlotKnown);
    REQUIRE(ev.tdmaSlot == 1);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(450012500.0).margin(1.0));
}

TEST_CASE("P25 control analyzer labels unknown TDMA channel types")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);
    writeBitsMsb(iden, 16, 4, 7);
    writeBitsMsb(iden, 20, 4, 9);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(420000000.0 / 5.0));

    auto events = analyzer.ingestTsbk(iden);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::IdentifierUpdate);
    REQUIRE(ev.identifier == 7);
    REQUIRE(ev.channelType == 9);
    REQUIRE(ev.slotsPerCarrier == 2);
    REQUIRE(ev.label.find("unknown channel type") != std::string::npos);
}

TEST_CASE("P25 control analyzer parses Motorola regroup channel grant as slot-aware voice")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);
    writeBitsMsb(iden, 16, 4, 6);
    writeBitsMsb(iden, 20, 4, 3);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(412475000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto grant = makeTsbk(0x02, 0x90);
    writeBitsMsb(grant, 24, 16, 0x637d);
    writeBitsMsb(grant, 40, 16, 11178);
    writeBitsMsb(grant, 56, 24, 0x102030);

    const auto events = analyzer.ingestTsbk(grant);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceUpdate);
    REQUIRE(ev.mfid == 0x90);
    REQUIRE(ev.talkgroupId == 11178);
    REQUIRE(ev.sourceId == 0x102030);
    REQUIRE(ev.channel == 0x637d);
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA);
    REQUIRE(ev.tdmaSlotKnown);
    REQUIRE(ev.tdmaSlot == 1);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(418050000.0).margin(1.0));
}

TEST_CASE("P25 control analyzer propagates NAC WACN and SYSID to later grants")
{
    P25ControlChannelAnalyzer analyzer;
    analyzer.setNac(0x2d2);

    auto iden = makeTsbk(0x33);
    writeBitsMsb(iden, 16, 4, 6);
    writeBitsMsb(iden, 20, 4, 3);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(412475000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto net = makeTsbk(0x3b);
    writeBitsMsb(net, 16, 8, 0x00);
    writeBitsMsb(net, 24, 20, 0xbee00);
    writeBitsMsb(net, 44, 12, 0x2d1);
    writeBitsMsb(net, 56, 16, 0x5038);
    analyzer.ingestTsbk(net);

    auto grant = makeTsbk(0x02);
    writeBitsMsb(grant, 16, 16, 0x637d);
    writeBitsMsb(grant, 32, 16, 11178);

    const auto events = analyzer.ingestTsbk(grant);
    REQUIRE(events.size() >= 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceUpdate);
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA);
    REQUIRE(ev.nacKnown);
    REQUIRE(ev.nac == 0x2d2);
    REQUIRE(ev.networkStatusKnown);
    REQUIRE(ev.wacn == 0xbee00);
    REQUIRE(ev.systemId == 0x2d1);
}

TEST_CASE("P25 control analyzer extracts network status mask metadata")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);
    writeBitsMsb(iden, 16, 4, 4);
    writeBitsMsb(iden, 20, 4, 3);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(419000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto net = makeTsbk(0x3b);
    writeBitsMsb(net, 16, 8, 0x2a);
    writeBitsMsb(net, 24, 20, 0xabcde);
    writeBitsMsb(net, 44, 12, 0x321);
    writeBitsMsb(net, 56, 16, 0x4002);

    auto events = analyzer.ingestTsbk(net);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::NetworkStatus);
    REQUIRE(ev.networkStatusKnown);
    REQUIRE(ev.lra == 0x2a);
    REQUIRE(ev.wacn == 0xabcde);
    REQUIRE(ev.systemId == 0x321);
    REQUIRE(ev.controlChannel == 0x4002);
    REQUIRE(ev.controlChannelFrequencyHz == Catch::Approx(419012500.0).margin(1.0));
}

TEST_CASE("P25 control analyzer extracts RFSS status site metadata")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x3d);
    writeBitsMsb(iden, 16, 4, 3);
    writeBitsMsb(iden, 29, 9, 0x100u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(416000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto rfss = makeTsbk(0x3a);
    writeBitsMsb(rfss, 16, 8, 0x14);
    writeBitsMsb(rfss, 27, 1, 1);
    writeBitsMsb(rfss, 28, 12, 0x654);
    writeBitsMsb(rfss, 40, 8, 7);
    writeBitsMsb(rfss, 48, 8, 12);
    writeBitsMsb(rfss, 56, 16, 0x3004);

    auto events = analyzer.ingestTsbk(rfss);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::RfssStatus);
    REQUIRE(ev.rfssStatusKnown);
    REQUIRE(ev.lra == 0x14);
    REQUIRE(ev.rfssNetworkActiveKnown);
    REQUIRE(ev.rfssNetworkActive);
    REQUIRE(ev.systemId == 0x654);
    REQUIRE(ev.rfssId == 7);
    REQUIRE(ev.siteId == 12);
    REQUIRE(ev.controlChannel == 0x3004);
    REQUIRE(ev.controlChannelFrequencyHz == Catch::Approx(416050000.0).margin(1.0));
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
