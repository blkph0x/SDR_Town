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

TEST_CASE("P25 control analyzer can be seeded with cached channel identifiers")
{
    P25ControlChannelAnalyzer analyzer;
    P25ChannelIdentifier identifier;
    identifier.valid = true;
    identifier.id = 7;
    identifier.channelType = 3;
    identifier.baseHz = 420000000.0;
    identifier.spacingHz = 12500.0;
    identifier.bandwidthHz = 12500.0;
    identifier.slotsPerCarrier = 2;
    identifier.phase2Capable = true;
    analyzer.setChannelIdentifier(identifier);

    auto freq = analyzer.channelToFrequencyHz(0x70c5);
    REQUIRE(freq.has_value());
    REQUIRE(*freq == Catch::Approx(421225000.0).margin(1.0));
}

TEST_CASE("P25 control analyzer preserves unresolved grant updates for pending resolution")
{
    const auto grant = p25ParseHexBytes("02 00 70 C4 76 5E 70 C4 76 5E 95 CB");

    P25ControlChannelAnalyzer pendingAnalyzer;
    pendingAnalyzer.setNac(0x2dc);
    const auto pending = pendingAnalyzer.ingestTsbk(grant);
    REQUIRE(pending.size() == 1);
    REQUIRE(pending.front().type == P25ControlEventType::GroupVoiceUpdate);
    REQUIRE(p25ControlEventIsVoiceGrant(pending.front()));
    REQUIRE(pending.front().talkgroupId == 0x765e);
    REQUIRE(pending.front().channel == 0x70c4);
    REQUIRE(pending.front().voiceFrequencyHz == 0.0);
    REQUIRE(pending.front().voiceProtocol == P25VoiceProtocol::Unknown);

    P25ControlChannelAnalyzer resolvedAnalyzer;
    resolvedAnalyzer.setNac(0x2dc);
    const auto id7 = resolvedAnalyzer.ingestTsbk(
        p25ParseHexBytes("33 00 73 86 80 64 05 01 BD 00 36 D0"));
    REQUIRE(id7.size() == 1);
    REQUIRE(id7.front().type == P25ControlEventType::IdentifierUpdate);

    const auto resolved = resolvedAnalyzer.ingestTsbk(grant);
    REQUIRE(resolved.size() == 1);
    REQUIRE(p25ControlEventIsVoiceGrant(resolved.front()));
    REQUIRE(resolved.front().voiceFrequencyHz == Catch::Approx(421225000.0).margin(1.0));
    REQUIRE(resolved.front().voiceProtocol == P25VoiceProtocol::Phase2TDMA);
    REQUIRE(resolved.front().tdmaSlotKnown);
    REQUIRE(resolved.front().tdmaSlot == 0);
}

TEST_CASE("P25 control analyzer can stamp current site metadata onto deferred grants")
{
    const auto grant = p25ParseHexBytes("02 00 70 C4 76 5E 70 C4 76 5E 95 CB");

    P25ControlChannelAnalyzer analyzer;
    analyzer.setNac(0x2dc);
    const auto pending = analyzer.ingestTsbk(grant);
    REQUIRE(pending.size() == 1);

    P25ControlEvent deferred = pending.front();
    REQUIRE(p25ControlEventIsVoiceGrant(deferred));
    REQUIRE_FALSE(deferred.networkStatusKnown);
    REQUIRE_FALSE(deferred.rfssStatusKnown);

    const auto network = analyzer.ingestTsbk(
        p25ParseHexBytes("3B 00 00 BE E0 02 D1 50 4C 70 10 E8"));
    REQUIRE(network.size() == 1);
    REQUIRE(network.front().networkStatusKnown);

    const auto id7 = analyzer.ingestTsbk(
        p25ParseHexBytes("33 00 73 86 80 64 05 01 BD 00 36 D0"));
    REQUIRE(id7.size() == 1);
    REQUIRE(id7.front().type == P25ControlEventType::IdentifierUpdate);

    analyzer.annotateCurrentSystemMetadata(deferred);
    REQUIRE(deferred.nacKnown);
    REQUIRE(deferred.nac == 0x2dc);
    REQUIRE(deferred.networkStatusKnown);
    REQUIRE(deferred.wacn == 0xbee00);
    REQUIRE(deferred.systemId == 0x2d1);
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
    REQUIRE(p25ControlEventIsVoiceGrant(ev));
    REQUIRE_FALSE(ev.phase2Candidate);
    REQUIRE_FALSE(ev.tdmaSlotKnown);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(450125000.0).margin(1.0));
}

TEST_CASE("P25 control analyzer decodes service option flags from voice grants")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x3d);
    writeBitsMsb(iden, 16, 4, 3);
    writeBitsMsb(iden, 29, 9, 0x100u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(450000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto grant = makeTsbk(0x00);
    writeBitsMsb(grant, 16, 8, 0xf5);
    writeBitsMsb(grant, 24, 16, 0x300a);
    writeBitsMsb(grant, 40, 16, 4321);
    writeBitsMsb(grant, 56, 24, 0x010203);

    const auto events = analyzer.ingestTsbk(grant);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.serviceOptionsKnown);
    REQUIRE(ev.serviceEmergency);
    REQUIRE(ev.encryptionKnown);
    REQUIRE(ev.encrypted);
    REQUIRE(ev.serviceDuplexFull);
    REQUIRE(ev.servicePacketMode);
    REQUIRE(ev.servicePriority == 5);
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
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(ev));
}

TEST_CASE("P25 control analyzer maps TDMA identifiers to slot-aware channel frequencies")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);

    const uint8_t id = 11;
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
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(ev));
    REQUIRE(ev.baseFrequencyHz == Catch::Approx(450000000.0).margin(1.0));
    REQUIRE(ev.channelSpacingHz == Catch::Approx(12500.0).margin(1.0));

    auto slot0Freq = analyzer.channelToFrequencyHz(0xB000);
    auto slot1Freq = analyzer.channelToFrequencyHz(0xB001);
    auto nextSlot0Freq = analyzer.channelToFrequencyHz(0xB002);
    auto nextSlot1Freq = analyzer.channelToFrequencyHz(0xB003);
    REQUIRE(slot0Freq.has_value());
    REQUIRE(slot1Freq.has_value());
    REQUIRE(nextSlot0Freq.has_value());
    REQUIRE(nextSlot1Freq.has_value());
    REQUIRE(*slot0Freq == Catch::Approx(450000000.0).margin(1.0));
    REQUIRE(*slot1Freq == Catch::Approx(450000000.0).margin(1.0));
    REQUIRE(*nextSlot0Freq == Catch::Approx(450012500.0).margin(1.0));
    REQUIRE(*nextSlot1Freq == Catch::Approx(450012500.0).margin(1.0));
}

TEST_CASE("P25 control analyzer uses channel-type bandwidth for TDMA transmit offsets")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);

    const uint8_t id = 11;
    const uint8_t channelType = 4; // 4-slot / 25 kHz carrier profile.
    const uint32_t baseUnits = static_cast<uint32_t>(450000000.0 / 5.0);
    const uint32_t spacingUnits = static_cast<uint32_t>(12500.0 / 125.0);

    writeBitsMsb(iden, 16, 4, id);
    writeBitsMsb(iden, 20, 4, channelType);
    writeBitsMsb(iden, 24, 14, 0x2002u); // positive offset magnitude 2.
    writeBitsMsb(iden, 38, 10, spacingUnits);
    writeBitsMsb(iden, 48, 32, baseUnits);

    const auto events = analyzer.ingestTsbk(iden);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::IdentifierUpdate);
    REQUIRE(ev.channelType == channelType);
    REQUIRE(ev.slotsPerCarrier == 4);
    REQUIRE(ev.bandwidthHz == Catch::Approx(25000.0).margin(1.0));
    REQUIRE(ev.channelSpacingHz == Catch::Approx(12500.0).margin(1.0));
    REQUIRE(ev.transmitOffsetHz == Catch::Approx(50000.0).margin(1.0));
}

TEST_CASE("P25 control analyzer treats TDMA channel type 5 as a known two-slot plan")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);

    writeBitsMsb(iden, 16, 4, 8);
    writeBitsMsb(iden, 20, 4, 5);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(421000000.0 / 5.0));

    const auto events = analyzer.ingestTsbk(iden);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::IdentifierUpdate);
    REQUIRE(ev.channelType == 5);
    REQUIRE(ev.slotsPerCarrier == 2);
    REQUIRE(ev.bandwidthHz == Catch::Approx(12500.0).margin(1.0));
    REQUIRE(ev.label.find("unknown channel type") == std::string::npos);
}

TEST_CASE("P25 control analyzer maps TDMA channel numbers to carrier and slot")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);

    const uint8_t id = 11;
    const uint8_t channelType = 3;
    const uint32_t baseUnits = static_cast<uint32_t>(450000000.0 / 5.0);
    const uint32_t spacingUnits = static_cast<uint32_t>(12500.0 / 125.0);

    writeBitsMsb(iden, 16, 4, id);
    writeBitsMsb(iden, 20, 4, channelType);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, spacingUnits);
    writeBitsMsb(iden, 48, 32, baseUnits);
    analyzer.ingestTsbk(iden);

    const struct Expected {
        uint16_t channel;
        uint8_t slot;
        double freqHz;
    } expected[] = {
        {0xB000, 0, 450000000.0},
        {0xB001, 1, 450000000.0},
        {0xB002, 0, 450012500.0},
        {0xB003, 1, 450012500.0},
    };

    for (const auto& item : expected) {
        auto grant = makeTsbk(0x00);
        writeBitsMsb(grant, 16, 8, 0x00);
        writeBitsMsb(grant, 24, 16, item.channel);
        writeBitsMsb(grant, 40, 16, 2000 + item.channel);
        writeBitsMsb(grant, 56, 24, 0x010203);

        const auto events = analyzer.ingestTsbk(grant);
        REQUIRE(events.size() == 1);
        const auto& ev = events.front();
        REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA);
        REQUIRE(ev.tdmaSlotKnown);
        REQUIRE(ev.tdmaSlot == item.slot);
        REQUIRE(ev.voiceFrequencyHz == Catch::Approx(item.freqHz).margin(1.0));
    }
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
    REQUIRE(p25ControlEventIsVoiceGrant(ev));
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
    REQUIRE(p25ControlEventIsVoiceGrant(ev));
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

TEST_CASE("P25 control analyzer parses Phase 2 MAC TDMA identifiers and implicit grant updates")
{
    P25ControlChannelAnalyzer analyzer;
    analyzer.setNac(0x2d2);

    std::vector<uint8_t> iden(15, 0);
    iden[0] = static_cast<uint8_t>(4u << 5); // MAC_ACTIVE-style PDU header
    iden[1] = 0xf3;                          // Identifier Update for TDMA Extended
    writeBitsMsb(iden, (1 + 2) * 8, 4, 11);
    writeBitsMsb(iden, (1 + 2) * 8 + 4, 4, 3);
    writeBitsMsb(iden, (1 + 3) * 8, 14, 0);
    writeBitsMsb(iden, (1 + 4) * 8 + 6, 10, 100);
    writeBitsMsb(iden, (1 + 6) * 8, 32, static_cast<uint32_t>(412475000.0 / 5.0));
    writeBitsMsb(iden, (1 + 10) * 8, 20, 0xbee00);
    writeBitsMsb(iden, (1 + 12) * 8 + 4, 12, 0x2d1);

    auto idEvents = analyzer.ingestPhase2MacPdu(4, 0, iden, true);
    REQUIRE(idEvents.size() == 1);
    REQUIRE(idEvents.front().type == P25ControlEventType::IdentifierUpdate);
    REQUIRE(idEvents.front().identifier == 11);
    REQUIRE(idEvents.front().slotsPerCarrier == 2);
    REQUIRE(idEvents.front().networkStatusKnown);
    REQUIRE(idEvents.front().wacn == 0xbee00);
    REQUIRE(idEvents.front().systemId == 0x2d1);

    std::vector<uint8_t> grant(17, 0);
    grant[0] = static_cast<uint8_t>(4u << 5);
    grant[1] = 0x05; // Group Voice Channel Grant Update Multiple - implicit
    grant[2] = 0x00;
    writeBitsMsb(grant, (1 + 2) * 8, 16, 0xB003);
    writeBitsMsb(grant, (1 + 4) * 8, 16, 11178);

    auto events = analyzer.ingestPhase2MacPdu(4, 0, grant, true);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceUpdate);
    REQUIRE(ev.phase2Mac);
    REQUIRE(ev.macMessageOpcode == 0x05);
    REQUIRE(ev.talkgroupId == 11178);
    REQUIRE(ev.channel == 0xB003);
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA);
    REQUIRE(ev.tdmaSlotKnown);
    REQUIRE(ev.tdmaSlot == 1);
    REQUIRE(ev.nacKnown);
    REQUIRE(ev.nac == 0x2d2);
    REQUIRE(ev.networkStatusKnown);
    REQUIRE(ev.wacn == 0xbee00);
    REQUIRE(ev.systemId == 0x2d1);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(412487500.0).margin(1.0));
    REQUIRE(ev.encryptionKnown);
    REQUIRE_FALSE(ev.encrypted);
    REQUIRE(p25ControlEventIsVoiceGrant(ev));
}

TEST_CASE("P25 control analyzer parses Phase 2 MAC explicit multi-grants")
{
    P25ControlChannelAnalyzer analyzer;
    std::vector<uint8_t> iden(15, 0);
    iden[0] = static_cast<uint8_t>(4u << 5);
    iden[1] = 0xf3;
    writeBitsMsb(iden, (1 + 2) * 8, 4, 6);
    writeBitsMsb(iden, (1 + 2) * 8 + 4, 4, 3);
    writeBitsMsb(iden, (1 + 4) * 8 + 6, 10, 100);
    writeBitsMsb(iden, (1 + 6) * 8, 32, static_cast<uint32_t>(419000000.0 / 5.0));
    analyzer.ingestPhase2MacPdu(4, 0, iden, true);

    std::vector<uint8_t> grant(16, 0);
    grant[0] = static_cast<uint8_t>(4u << 5);
    grant[1] = 0x25; // explicit two-entry update
    grant[2] = 0x00;
    writeBitsMsb(grant, (1 + 2) * 8, 16, 0x6002);
    writeBitsMsb(grant, (1 + 4) * 8, 16, 0x6020);
    writeBitsMsb(grant, (1 + 6) * 8, 16, 1201);
    grant[9] = 0x40; // encrypted service options for second entry
    writeBitsMsb(grant, (1 + 9) * 8, 16, 0x6003);
    writeBitsMsb(grant, (1 + 11) * 8, 16, 0x6021);
    writeBitsMsb(grant, (1 + 13) * 8, 16, 1202);

    const auto events = analyzer.ingestPhase2MacPdu(4, 0, grant, true);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == P25ControlEventType::GroupVoiceGrantExplicit);
    REQUIRE(events[0].talkgroupId == 1201);
    REQUIRE(events[0].channel == 0x6002);
    REQUIRE(events[0].channelB == 0x6020);
    REQUIRE(events[0].explicitChannelKnown);
    REQUIRE(events[0].explicitChannel.valid);
    REQUIRE(events[0].explicitChannel.transmitBand == 6);
    REQUIRE(events[0].explicitChannel.transmitNumber == 2);
    REQUIRE(events[0].explicitChannel.receiveBand == 6);
    REQUIRE(events[0].explicitChannel.receiveNumber == 0x20);
    REQUIRE(events[0].explicitChannel.protocolTransmitChannel == 0x6002);
    REQUIRE(events[0].explicitChannel.protocolReceiveChannel == 0x6020);
    REQUIRE(events[0].explicitChannel.scannerDownlinkChannel == 0x6002);
    REQUIRE(events[0].explicitChannel.subscriberUplinkChannel == 0x6020);
    REQUIRE(events[0].explicitChannel.downlinkChannel == 0x6002);
    REQUIRE(events[0].explicitChannel.uplinkChannel == 0x6020);
    REQUIRE(events[0].explicitChannel.protocolTransmitHz == Catch::Approx(419012500.0).margin(1.0));
    REQUIRE(events[0].explicitChannel.protocolReceiveHz == Catch::Approx(419200000.0).margin(1.0));
    REQUIRE(events[0].explicitChannel.scannerDownlinkHz == Catch::Approx(419012500.0).margin(1.0));
    REQUIRE(events[0].explicitChannel.subscriberUplinkHz == Catch::Approx(419200000.0).margin(1.0));
    REQUIRE(events[0].explicitChannel.downlinkHz == Catch::Approx(419012500.0).margin(1.0));
    REQUIRE(events[0].explicitChannel.uplinkHz == Catch::Approx(419200000.0).margin(1.0));
    REQUIRE(events[0].voiceFrequencyHz == Catch::Approx(events[0].explicitChannel.downlinkHz).margin(1.0));
    REQUIRE(events[0].voiceFrequencyHzB == Catch::Approx(events[0].explicitChannel.uplinkHz).margin(1.0));
    REQUIRE(events[0].tdmaSlotKnown);
    REQUIRE(events[0].tdmaSlot == 0);
    REQUIRE_FALSE(events[0].encrypted);
    REQUIRE(events[1].talkgroupId == 1202);
    REQUIRE(events[1].tdmaSlotKnown);
    REQUIRE(events[1].tdmaSlot == 1);
    REQUIRE(events[1].encryptionKnown);
    REQUIRE(events[1].encrypted);
}

TEST_CASE("P25 control analyzer suppresses duplicate Phase 2 explicit multi-grant entries")
{
    P25ControlChannelAnalyzer analyzer;
    std::vector<uint8_t> iden(15, 0);
    iden[0] = static_cast<uint8_t>(4u << 5);
    iden[1] = 0xf3;
    writeBitsMsb(iden, (1 + 2) * 8, 4, 6);
    writeBitsMsb(iden, (1 + 2) * 8 + 4, 4, 3);
    writeBitsMsb(iden, (1 + 4) * 8 + 6, 10, 100);
    writeBitsMsb(iden, (1 + 6) * 8, 32, static_cast<uint32_t>(419000000.0 / 5.0));
    analyzer.ingestPhase2MacPdu(4, 0, iden, true);

    std::vector<uint8_t> grant(16, 0);
    grant[0] = static_cast<uint8_t>(4u << 5);
    grant[1] = 0x25;
    writeBitsMsb(grant, (1 + 2) * 8, 16, 0x6002);
    writeBitsMsb(grant, (1 + 4) * 8, 16, 0x6020);
    writeBitsMsb(grant, (1 + 6) * 8, 16, 1201);
    writeBitsMsb(grant, (1 + 9) * 8, 16, 0x6003);
    writeBitsMsb(grant, (1 + 11) * 8, 16, 0x6021);
    writeBitsMsb(grant, (1 + 13) * 8, 16, 1201);

    const auto events = analyzer.ingestPhase2MacPdu(4, 0, grant, true);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().talkgroupId == 1201);
    REQUIRE(events.front().channel == 0x6002);
}

TEST_CASE("P25 control analyzer parses Phase 2 MAC PTT user and encryption state")
{
    P25ControlChannelAnalyzer analyzer;
    std::vector<uint8_t> ptt(18, 0);
    ptt[0] = static_cast<uint8_t>(1u << 5);
    for (size_t i = 1; i <= 9; ++i) ptt[i] = static_cast<uint8_t>(i);
    ptt[10] = 0x80; // clear algorithm id
    ptt[11] = 0x12;
    ptt[12] = 0x34;
    writeBitsMsb(ptt, 13 * 8, 24, 0x102030);
    writeBitsMsb(ptt, 16 * 8, 16, 2345);

    const auto events = analyzer.ingestPhase2MacPdu(1, 0, ptt, true);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceUser);
    REQUIRE(ev.talkgroupId == 2345);
    REQUIRE(ev.sourceId == 0x102030);
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA);
    REQUIRE(ev.encryptionKnown);
    REQUIRE_FALSE(ev.encrypted);
    REQUIRE(ev.pttEncryptionSyncKnown);
    for (size_t i = 0; i < ev.messageIndicator.size(); ++i) {
        REQUIRE(ev.messageIndicator[i] == static_cast<uint8_t>(i + 1));
    }
    REQUIRE(ev.algorithmId == 0x80);
    REQUIRE(ev.keyId == 0x1234);

    ptt[10] = 0x84;
    const auto encEvents = analyzer.ingestPhase2MacPdu(1, 0, ptt, true);
    REQUIRE(encEvents.front().encryptionKnown);
    REQUIRE(encEvents.front().encrypted);
}

TEST_CASE("P25 control analyzer parses Phase 2 end PTT NAC without inventing encryption state")
{
    P25ControlChannelAnalyzer analyzer;
    std::vector<uint8_t> endPtt(18, 0);
    endPtt[0] = static_cast<uint8_t>(2u << 5);
    writeBitsMsb(endPtt, 12, 12, 0x2d2);
    writeBitsMsb(endPtt, 13 * 8, 24, 0x102030);
    writeBitsMsb(endPtt, 16 * 8, 16, 2345);

    const auto events = analyzer.ingestPhase2MacPdu(2, 0, endPtt, true);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceEnd);
    REQUIRE(ev.talkgroupId == 2345);
    REQUIRE(ev.sourceId == 0x102030);
    REQUIRE(ev.endPttNacKnown);
    REQUIRE(ev.endPttNac == 0x2d2);
    REQUIRE_FALSE(ev.encryptionKnown);
}

TEST_CASE("P25 control analyzer parses Motorola Phase 2 regroup grants only for MFID 0x90")
{
    P25ControlChannelAnalyzer analyzer;
    std::vector<uint8_t> iden(15, 0);
    iden[0] = static_cast<uint8_t>(4u << 5);
    iden[1] = 0xf3;
    writeBitsMsb(iden, (1 + 2) * 8, 4, 6);
    writeBitsMsb(iden, (1 + 2) * 8 + 4, 4, 3);
    writeBitsMsb(iden, (1 + 4) * 8 + 6, 10, 100);
    writeBitsMsb(iden, (1 + 6) * 8, 32, static_cast<uint32_t>(412475000.0 / 5.0));
    analyzer.ingestPhase2MacPdu(4, 0, iden, true);

    std::vector<uint8_t> grant(14, 0);
    grant[0] = static_cast<uint8_t>(4u << 5);
    grant[1] = 0xa3;
    grant[2] = 0x90;
    grant[3] = 13;
    writeBitsMsb(grant, (1 + 4) * 8, 16, 0x637d);
    writeBitsMsb(grant, (1 + 6) * 8, 16, 11178);
    writeBitsMsb(grant, (1 + 8) * 8, 24, 0x102030);

    const auto events = analyzer.ingestPhase2MacPdu(4, 0, grant, true);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(events.front().mfid == 0x90);
    REQUIRE(events.front().talkgroupId == 11178);
    REQUIRE(events.front().sourceId == 0x102030);
    REQUIRE(events.front().tdmaSlotKnown);
    REQUIRE(events.front().tdmaSlot == 1);

    grant[2] = 0x91;
    const auto nonMotorola = analyzer.ingestPhase2MacPdu(4, 0, grant, true);
    REQUIRE(nonMotorola.size() == 1);
    REQUIRE(nonMotorola.front().type == P25ControlEventType::VendorCommand);
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(nonMotorola.front()));
}

TEST_CASE("P25 control analyzer handles control-channel housekeeping TSBKs from 420.475 MHz capture")
{
    P25ControlChannelAnalyzer analyzer;
    analyzer.setNac(0x2dc);

    const auto sndcp = analyzer.ingestTsbk(
        p25ParseHexBytes("16 00 00 C0 43 68 FF FF 00 01 45 23"));
    REQUIRE(sndcp.size() == 1);
    REQUIRE(sndcp.front().type == P25ControlEventType::DataChannel);
    REQUIRE(sndcp.front().opcode == 0x16);
    REQUIRE(sndcp.front().label.find("SNDCP") != std::string::npos);
    REQUIRE(sndcp.front().channel == 0x4368);
    REQUIRE(sndcp.front().channelB == 0xffff);
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(sndcp.front()));

    const auto sccb = analyzer.ingestTsbk(
        p25ParseHexBytes("39 00 04 53 51 14 04 51 14 04 DE 46"));
    REQUIRE(sccb.size() == 1);
    REQUIRE(sccb.front().type == P25ControlEventType::SecondaryControlChannel);
    REQUIRE(sccb.front().rfssId == 4);
    REQUIRE(sccb.front().siteId == 83);
    REQUIRE(sccb.front().controlChannel == 0x5114);
    REQUIRE(sccb.front().controlChannelB == 0x5114);
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(sccb.front()));

    const auto adjacent = analyzer.ingestTsbk(
        p25ParseHexBytes("3C 00 00 32 D1 04 52 44 32 70 A0 38"));
    REQUIRE(adjacent.size() == 1);
    REQUIRE(adjacent.front().type == P25ControlEventType::AdjacentStatus);
    REQUIRE(adjacent.front().rfssId == 4);
    REQUIRE(adjacent.front().siteId == 82);
    REQUIRE(adjacent.front().controlChannel == 0x4432);
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(adjacent.front()));
}

TEST_CASE("P25 control analyzer labels Motorola vendor TSBKs without treating them as voice")
{
    P25ControlChannelAnalyzer analyzer;

    const auto opcode09 = analyzer.ingestTsbk(
        p25ParseHexBytes("09 90 16 80 00 00 00 00 00 00 20 3B"));
    REQUIRE(opcode09.size() == 1);
    REQUIRE(opcode09.front().type == P25ControlEventType::VendorCommand);
    REQUIRE(opcode09.front().mfid == 0x90);
    REQUIRE(opcode09.front().label.find("Unsupported") == std::string::npos);
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(opcode09.front()));

    const auto opcode0b = analyzer.ingestTsbk(
        p25ParseHexBytes("0B 90 00 00 00 00 00 00 50 4C 6F C0"));
    REQUIRE(opcode0b.size() == 1);
    REQUIRE(opcode0b.front().type == P25ControlEventType::VendorCommand);
    REQUIRE(opcode0b.front().mfid == 0x90);
    REQUIRE(opcode0b.front().label.find("Unsupported") == std::string::npos);
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(opcode0b.front()));
}

TEST_CASE("P25 control analyzer recognizes standard non-group TSBK voice and control opcodes")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x3d);
    writeBitsMsb(iden, 16, 4, 4);
    writeBitsMsb(iden, 29, 9, 0x100u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(420000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    auto unit = makeTsbk(0x04);
    writeBitsMsb(unit, 16, 16, 0x4002);
    writeBitsMsb(unit, 32, 24, 0x010203);
    writeBitsMsb(unit, 56, 24, 0x040506);
    const auto unitEvents = analyzer.ingestTsbk(unit);
    REQUIRE(unitEvents.size() == 1);
    REQUIRE(unitEvents.front().type == P25ControlEventType::UnitVoiceGrant);
    REQUIRE(unitEvents.front().channel == 0x4002);
    REQUIRE(unitEvents.front().sourceId == 0x040506);
    REQUIRE(unitEvents.front().voiceFrequencyHz == Catch::Approx(420025000.0).margin(1.0));
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(unitEvents.front()));

    auto telephone = makeTsbk(0x08);
    writeBitsMsb(telephone, 16, 8, 0x40);
    writeBitsMsb(telephone, 24, 16, 0x4003);
    writeBitsMsb(telephone, 56, 24, 0x070809);
    const auto telephoneEvents = analyzer.ingestTsbk(telephone);
    REQUIRE(telephoneEvents.size() == 1);
    REQUIRE(telephoneEvents.front().type == P25ControlEventType::TelephoneVoiceGrant);
    REQUIRE(telephoneEvents.front().encrypted);
    REQUIRE_FALSE(p25ControlEventIsVoiceGrant(telephoneEvents.front()));

    const auto systemService = analyzer.ingestTsbk(makeTsbk(0x38));
    REQUIRE(systemService.size() == 1);
    REQUIRE(systemService.front().type == P25ControlEventType::ControlMessage);
    REQUIRE(systemService.front().label.find("System service") != std::string::npos);
}

TEST_CASE("P25 control analyzer uses SDRTrunk MAC lengths to continue after housekeeping messages")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);
    writeBitsMsb(iden, 16, 4, 6);
    writeBitsMsb(iden, 20, 4, 3);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(410000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    std::vector<uint8_t> pdu(15, 0);
    pdu[0] = static_cast<uint8_t>(4u << 5);
    pdu[1] = 0x30; // Power Control Signal Quality, fixed 5-byte MAC structure.
    pdu[6] = 0x40; // Follow-on group voice channel grant.
    pdu[7] = 0x00;
    writeBitsMsb(pdu, 8 * 8, 16, 0x6003);
    writeBitsMsb(pdu, 10 * 8, 16, 3210);
    writeBitsMsb(pdu, 12 * 8, 24, 0x112233);

    const auto events = analyzer.ingestPhase2MacPdu(4, 0, pdu, true);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == P25ControlEventType::ControlMessage);
    REQUIRE(events[0].macMessageOpcode == 0x30);
    REQUIRE(events[1].type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(events[1].talkgroupId == 3210);
    REQUIRE(events[1].tdmaSlotKnown);
    REQUIRE(events[1].tdmaSlot == 1);
    REQUIRE(p25ControlEventIsVoiceGrant(events[1]));
}

TEST_CASE("P25 control analyzer honors SDRTrunk SACCH MAC structure scan limit")
{
    P25ControlChannelAnalyzer analyzer;
    P25ChannelIdentifier identifier;
    identifier.valid = true;
    identifier.id = 6;
    identifier.channelType = 3;
    identifier.baseHz = 410000000.0;
    identifier.spacingHz = 12500.0;
    identifier.bandwidthHz = 12500.0;
    identifier.slotsPerCarrier = 2;
    identifier.phase2Capable = true;
    analyzer.setChannelIdentifier(identifier);

    std::vector<uint8_t> pdu(21, 0);
    pdu[0] = static_cast<uint8_t>(4u << 5);
    pdu[1] = 0x30; // Power Control Signal Quality, fixed 5-byte MAC structure.
    pdu[6] = 0x40; // Group voice grant starts before SACCH's maxIndex=99.
    writeBitsMsb(pdu, 8 * 8, 16, 0x6002);
    writeBitsMsb(pdu, 10 * 8, 16, 3210);
    writeBitsMsb(pdu, 12 * 8, 24, 0x112233);
    pdu[15] = 0x40; // Would be a false third structure if we scanned past bit 99.
    writeBitsMsb(pdu, 17 * 8, 16, 0x6004);
    writeBitsMsb(pdu, 19 * 8, 16, 6543);

    const auto events = analyzer.ingestPhase2MacPdu(4, 0, pdu, true, 99);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == P25ControlEventType::ControlMessage);
    REQUIRE(events[0].macMessageOpcode == 0x30);
    REQUIRE(events[1].type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(events[1].talkgroupId == 3210);
    REQUIRE(events[1].channel == 0x6002);
}

TEST_CASE("P25 control analyzer consumes full MAC release before later structures")
{
    P25ControlChannelAnalyzer analyzer;
    auto iden = makeTsbk(0x33);
    writeBitsMsb(iden, 16, 4, 5);
    writeBitsMsb(iden, 20, 4, 3);
    writeBitsMsb(iden, 24, 14, 0x2000u);
    writeBitsMsb(iden, 38, 10, 100);
    writeBitsMsb(iden, 48, 32, static_cast<uint32_t>(419000000.0 / 5.0));
    analyzer.ingestTsbk(iden);

    std::vector<uint8_t> pdu(17, 0);
    pdu[0] = static_cast<uint8_t>(4u << 5);
    pdu[1] = 0x31; // MAC release is 7 bytes in SDRTrunk's table.
    pdu[2] = 0x12;
    pdu[3] = 0x34;
    pdu[8] = 0x40;
    writeBitsMsb(pdu, 10 * 8, 16, 0x5002);
    writeBitsMsb(pdu, 12 * 8, 16, 4444);
    writeBitsMsb(pdu, 14 * 8, 24, 0x445566);

    const auto events = analyzer.ingestPhase2MacPdu(4, 0, pdu, true);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == P25ControlEventType::GroupVoiceEnd);
    REQUIRE(events[1].type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(events[1].talkgroupId == 4444);
}

TEST_CASE("P25 control analyzer parses abbreviated Phase 2 MAC TDMA identifiers")
{
    P25ControlChannelAnalyzer analyzer;

    std::vector<uint8_t> iden(10, 0);
    iden[0] = static_cast<uint8_t>(4u << 5);
    iden[1] = 0x73;
    writeBitsMsb(iden, (1 * 8) + 8, 4, 10);
    writeBitsMsb(iden, (1 * 8) + 12, 4, 3);
    writeBitsMsb(iden, (1 * 8) + 16, 1, 1);
    writeBitsMsb(iden, (1 * 8) + 17, 13, 0);
    writeBitsMsb(iden, (1 * 8) + 30, 10, 100);
    writeBitsMsb(iden, (1 * 8) + 40, 32, static_cast<uint32_t>(421000000.0 / 5.0));

    const auto idEvents = analyzer.ingestPhase2MacPdu(4, 0, iden, true);
    REQUIRE(idEvents.size() == 1);
    REQUIRE(idEvents.front().type == P25ControlEventType::IdentifierUpdate);
    REQUIRE(idEvents.front().identifier == 10);
    REQUIRE(idEvents.front().slotsPerCarrier == 2);
    REQUIRE(idEvents.front().bandwidthHz == Catch::Approx(12500.0).margin(1.0));

    auto slot0 = analyzer.channelToFrequencyHz(0xA000);
    auto slot1 = analyzer.channelToFrequencyHz(0xA001);
    auto nextCarrier = analyzer.channelToFrequencyHz(0xA002);
    REQUIRE(slot0.has_value());
    REQUIRE(slot1.has_value());
    REQUIRE(nextCarrier.has_value());
    REQUIRE(*slot0 == Catch::Approx(421000000.0).margin(1.0));
    REQUIRE(*slot1 == Catch::Approx(421000000.0).margin(1.0));
    REQUIRE(*nextCarrier == Catch::Approx(421012500.0).margin(1.0));
}

TEST_CASE("P25 control analyzer names SDRTrunk-covered Phase 2 MAC opcodes")
{
    REQUIRE(p25Phase2MacMessageOpcodeToString(0x02).find("Unit-to-unit") != std::string::npos);
    REQUIRE(p25Phase2MacMessageOpcodeToString(0x79).find("Secondary control") != std::string::npos);
    REQUIRE(p25Phase2MacMessageOpcodeToString(0xc8).find("Telephone") != std::string::npos);
    REQUIRE(p25Phase2MacMessageOpcodeToString(0xa3, 0x90).find("Motorola") != std::string::npos);
    REQUIRE(p25Phase2MacMessageOpcodeToString(0xa0, 0xa4).find("L3Harris") != std::string::npos);
}

TEST_CASE("P25 control analyzer parses Phase 1 AMBTC group voice grants")
{
    P25ControlChannelAnalyzer analyzer;
    P25ChannelIdentifier identifier;
    identifier.valid = true;
    identifier.id = 7;
    identifier.channelType = 3;
    identifier.baseHz = 420000000.0;
    identifier.spacingHz = 12500.0;
    identifier.bandwidthHz = 12500.0;
    identifier.slotsPerCarrier = 2;
    identifier.phase2Capable = true;
    analyzer.setChannelIdentifier(identifier);

    std::vector<uint8_t> header(12, 0);
    writeBitsMsb(header, 2, 1, 1);       // outbound
    writeBitsMsb(header, 3, 5, 23);      // AMBTC
    writeBitsMsb(header, 16, 8, 0x00);   // standard
    writeBitsMsb(header, 24, 24, 0x123456);
    writeBitsMsb(header, 49, 7, 1);
    writeBitsMsb(header, 58, 6, 0x00);
    writeBitsMsb(header, 64, 8, 0x00);

    std::vector<uint8_t> block0(12, 0);
    writeBitsMsb(block0, 16, 4, 7);
    writeBitsMsb(block0, 20, 12, 0x039);
    writeBitsMsb(block0, 32, 4, 7);
    writeBitsMsb(block0, 36, 12, 0x039);
    writeBitsMsb(block0, 48, 16, 11178);

    const auto events = analyzer.ingestPhase1Pdu(23, 0x00, 0x00, header, {block0}, true);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceGrant);
    REQUIRE(p25ControlEventIsVoiceGrant(ev));
    REQUIRE(ev.sourceId == 0x123456);
    REQUIRE(ev.talkgroupId == 11178);
    REQUIRE(ev.channel == 0x7039);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(420350000.0).margin(1.0));
    REQUIRE(ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA);
    REQUIRE(ev.tdmaSlotKnown);
    REQUIRE(ev.tdmaSlot == 1);
}

TEST_CASE("P25 control analyzer parses Motorola AMBTC regroup grants")
{
    P25ControlChannelAnalyzer analyzer;
    P25ChannelIdentifier identifier;
    identifier.valid = true;
    identifier.id = 7;
    identifier.channelType = 3;
    identifier.baseHz = 420000000.0;
    identifier.spacingHz = 12500.0;
    identifier.bandwidthHz = 12500.0;
    identifier.slotsPerCarrier = 2;
    identifier.phase2Capable = true;
    analyzer.setChannelIdentifier(identifier);

    std::vector<uint8_t> header(12, 0);
    writeBitsMsb(header, 2, 1, 1);
    writeBitsMsb(header, 3, 5, 23);
    writeBitsMsb(header, 16, 8, 0x90);
    writeBitsMsb(header, 24, 24, 0x654321);
    writeBitsMsb(header, 49, 7, 1);
    writeBitsMsb(header, 58, 6, 0x02);
    writeBitsMsb(header, 64, 8, 0x40);

    std::vector<uint8_t> block0(12, 0);
    writeBitsMsb(block0, 0, 4, 7);
    writeBitsMsb(block0, 4, 12, 0x038);
    writeBitsMsb(block0, 16, 4, 7);
    writeBitsMsb(block0, 20, 12, 0x120);
    writeBitsMsb(block0, 32, 16, 2222);

    const auto events = analyzer.ingestPhase1Pdu(23, 0x90, 0x02, header, {block0}, true);
    REQUIRE(events.size() == 1);
    const auto& ev = events.front();
    REQUIRE(ev.type == P25ControlEventType::GroupVoiceGrantExplicit);
    REQUIRE(p25ControlEventIsVoiceGrant(ev));
    REQUIRE(ev.encryptionKnown);
    REQUIRE(ev.encrypted);
    REQUIRE(ev.sourceId == 0x654321);
    REQUIRE(ev.talkgroupId == 2222);
    REQUIRE(ev.channel == 0x7038);
    REQUIRE(ev.channelB == 0x7120);
    REQUIRE(ev.explicitChannelKnown);
    REQUIRE(ev.voiceFrequencyHz == Catch::Approx(420350000.0).margin(1.0));
}

TEST_CASE("P25 control analyzer rejects CRC-failed Phase 1 PDU headers")
{
    P25ControlChannelAnalyzer analyzer;
    std::vector<uint8_t> header(12, 0);
    writeBitsMsb(header, 3, 5, 23);
    writeBitsMsb(header, 58, 6, 0x00);
    std::vector<uint8_t> block0(12, 0);
    REQUIRE(analyzer.ingestPhase1Pdu(23, 0x00, 0x00, header, {block0}, false).empty());
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
