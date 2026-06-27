#include "P25Control.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <sstream>

namespace {

uint64_t readBitsMsb(const std::vector<uint8_t>& bytes, int startBit, int count)
{
    if (count <= 0 || count > 64 || startBit < 0) return 0;
    uint64_t out = 0;
    for (int i = 0; i < count; ++i) {
        const int bit = startBit + i;
        const int byteIndex = bit / 8;
        if (byteIndex < 0 || byteIndex >= static_cast<int>(bytes.size())) return 0;
        const int bitInByte = 7 - (bit % 8);
        out = (out << 1) | ((bytes[static_cast<size_t>(byteIndex)] >> bitInByte) & 0x1u);
    }
    return out;
}

const std::vector<uint8_t>& normalizeTsbkBlock(const std::vector<uint8_t>& block,
                                                std::vector<uint8_t>& scratch,
                                                bool& ok)
{
    ok = block.size() >= 10;
    if (!ok) return scratch;
    if (block.size() >= 12) return block;
    scratch.assign(12, 0);
    std::copy_n(block.begin(), block.size(), scratch.begin());
    return scratch;
}

bool finitePositive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

bool hasBytes(const std::vector<uint8_t>& bytes, size_t offset, size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

uint16_t readU16Msb(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (!hasBytes(bytes, offset, 2)) return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
                                 static_cast<uint16_t>(bytes[offset + 1]));
}

uint32_t readU24Msb(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (!hasBytes(bytes, offset, 3)) return 0;
    return (static_cast<uint32_t>(bytes[offset]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           static_cast<uint32_t>(bytes[offset + 2]);
}

bool allRemainingZero(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset >= bytes.size()) return true;
    return std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end(),
                       [](uint8_t v) { return v == 0; });
}

void applyServiceOptions(P25ControlEvent& event, uint8_t serviceOptions)
{
    event.serviceOptionsKnown = true;
    event.serviceOptions = serviceOptions;
    event.serviceEmergency = (serviceOptions & 0x80u) != 0;
    event.serviceDuplexFull = (serviceOptions & 0x20u) != 0;
    event.servicePacketMode = (serviceOptions & 0x10u) != 0;
    event.servicePriority = static_cast<uint8_t>(serviceOptions & 0x07u);
    event.encryptionKnown = true;
    event.encrypted = (serviceOptions & 0x40u) != 0;
}

struct P25ChannelTypeProfile {
    double bandwidthHz = 0.0;
    int slotsPerCarrier = 1;
    bool tdma = false;
    bool known = false;
};

P25ChannelTypeProfile channelTypeProfile(uint8_t channelType)
{
    switch (channelType & 0x0fu) {
        case 0x0: return {12500.0, 1, false, true};
        case 0x1: return {12500.0, 1, false, true};
        case 0x2: return {6250.0, 1, false, true};
        case 0x3: return {12500.0, 2, true, true};
        case 0x4: return {25000.0, 4, true, true};
        case 0x5: return {12500.0, 2, true, true};
        default:
            return {0.0, 2, true, false};
    }
}

double bandwidthForChannelType(uint8_t channelType)
{
    return channelTypeProfile(channelType).bandwidthHz;
}

std::string hexOpcode(uint8_t value)
{
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << static_cast<int>(value);
    return ss.str();
}

const char* standardOspTsbkLabel(uint8_t opcode)
{
    static constexpr const char* labels[64] = {
        "Group voice channel grant",
        "OSP reserved 1",
        "Group voice channel grant update",
        "Group voice channel grant update explicit",
        "Unit-to-unit voice channel grant",
        "Unit-to-unit answer request",
        "Unit-to-unit voice channel grant update",
        "OSP reserved 7",
        "Telephone interconnect voice channel grant",
        "Telephone interconnect voice channel grant update",
        "Telephone interconnect answer request",
        "OSP reserved 11",
        "OSP reserved 12",
        "OSP reserved 13",
        "OSP reserved 14",
        "OSP reserved 15",
        "Individual data channel grant obsolete",
        "Group data channel grant obsolete",
        "Group data channel announcement obsolete",
        "Group data channel announcement explicit obsolete",
        "SNDCP data channel grant",
        "SNDCP data channel page request",
        "SNDCP data channel announcement explicit",
        "OSP reserved 23",
        "Status update",
        "OSP reserved 25",
        "Status query",
        "OSP reserved 27",
        "Message update",
        "Radio unit monitor command",
        "Radio unit monitor enhanced command",
        "Call alert",
        "Acknowledge response",
        "Queued response",
        "OSP reserved 34",
        "OSP reserved 35",
        "Extended function command",
        "OSP reserved 37",
        "OSP reserved 38",
        "Deny response",
        "Group affiliation response",
        "Secondary control channel broadcast explicit",
        "Group affiliation query",
        "Location registration response",
        "Unit registration response",
        "Unit registration command",
        "Authentication command",
        "Unit deregistration acknowledge",
        "TDMA synchronization broadcast",
        "Authentication demand",
        "Authentication FNE response",
        "Identifier update TDMA",
        "Identifier update VHF/UHF",
        "Time and date announcement",
        "Roaming address command",
        "Roaming address update",
        "System service broadcast",
        "Secondary control channel broadcast",
        "RFSS status broadcast",
        "Network status broadcast",
        "Adjacent status broadcast",
        "Identifier update",
        "Adjacent status broadcast uncoordinated band plan",
        "OSP reserved 63",
    };
    return labels[opcode & 0x3fu];
}

const char* motorolaOspTsbkLabel(uint8_t opcode)
{
    switch (opcode) {
        case 0x00: return "Motorola group regroup add";
        case 0x01: return "Motorola group regroup delete";
        case 0x02: return "Motorola group regroup channel grant";
        case 0x03: return "Motorola patch group channel grant update";
        case 0x04: return "Motorola extended function command";
        case 0x05: return "Motorola traffic channel";
        case 0x06: return "Motorola queued response";
        case 0x07: return "Motorola deny response";
        case 0x08: return "Motorola acknowledge response";
        case 0x09: return "Motorola system loading";
        case 0x0a: return "Motorola emergency alarm activation";
        case 0x0b: return "Control channel base station ID";
        case 0x0e: return "Control channel planned shutdown";
        case 0x0f: return "Motorola opcode 15";
        case 0x16: return "Motorola TDMA data channel";
        default: return "Motorola OSP unknown opcode";
    }
}

const char* harrisOspTsbkLabel(uint8_t opcode)
{
    switch (opcode) {
        case 0x30: return "Harris group regroup command with explicit encryption";
        default: return "Harris OSP unknown opcode";
    }
}

bool isUnsupportedPhase2MacLabel(const std::string& label)
{
    return label.find("Unsupported") != std::string::npos ||
           label.find("UNKNOWN") != std::string::npos ||
           label.find("Unknown") != std::string::npos;
}

P25ControlEvent makePhase2MacEvent(uint8_t pduType,
                                   uint8_t pduOffset,
                                   uint8_t messageOpcode,
                                   size_t messageOffset,
                                   P25ControlEventType type,
                                   std::string label)
{
    P25ControlEvent event;
    event.type = type;
    event.opcode = messageOpcode;
    event.label = std::move(label);
    event.phase2Mac = true;
    event.macPduType = static_cast<uint8_t>(pduType & 0x07u);
    event.macPduOffset = static_cast<uint8_t>(pduOffset & 0x07u);
    event.macMessageOpcode = messageOpcode;
    event.macMessageOffset = messageOffset;
    return event;
}

size_t phase2MacMessageLength(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset >= bytes.size()) return 0;
    const uint8_t op = bytes[offset];
    switch (op) {
        case 0x00: return 1;   // Null / pad.
        case 0x01: return 7;
        case 0x02: return 8;
        case 0x03: return 7;
        case 0x05: return 16;
        case 0x21: return 14;
        case 0x22: return 15;
        case 0x25: return 15;
        case 0x30: return 5;
        case 0x31: return 7;
        case 0x40: return 9;
        case 0x41: return 7;
        case 0x42: return 9;
        case 0x44: return 9;
        case 0x45: return 10;
        case 0x46: return 9;
        case 0x48: return 10;
        case 0x49: return 10;
        case 0x4a: return 7;
        case 0x4c: return 10;
        case 0x52: return 8;
        case 0x53: return 9;
        case 0x54: return 9;
        case 0x55: return 7;
        case 0x58: return 10;
        case 0x5a: return 7;
        case 0x5c: return 10;
        case 0x5d: return 8;
        case 0x5e: return 14;
        case 0x5f: return 7;
        case 0x60: return 9;
        case 0x61: return 9;
        case 0x64: return 9;
        case 0x67: return 9;
        case 0x68: return 10;
        case 0x6a: return 7;
        case 0x6b: return 10;
        case 0x6c: return 10;
        case 0x6d: return 7;
        case 0x6f: return 9;
        case 0x70: return 9;
        case 0x71: return 18;
        case 0x72: return 9;
        case 0x73: return 9;
        case 0x74: return 9;
        case 0x75: return 9;
        case 0x76: return 10;
        case 0x77: return 13;
        case 0x78: return 9;
        case 0x79: return 9;
        case 0x7a: return 9;
        case 0x7b: return 11;
        case 0x7c: return 9;
        case 0x7d: return 9;
        case 0x88: return 5;
        case 0x90: return 7;
        case 0xc0: return 11;
        case 0xc3: return 8;
        case 0xc4: return 15;
        case 0xc5: return 14;
        case 0xc6: return 15;
        case 0xc7: return 18;
        case 0xc8: return 12;
        case 0xc9: return 12;
        case 0xcb: return 18;
        case 0xcc: return 14;
        case 0xcd: return 18;
        case 0xce: return 18;
        case 0xcf: return 18;
        case 0xd6: return 9;
        case 0xd8: return 14;
        case 0xd9: return 18;
        case 0xda: return 11;
        case 0xdb: return 18;
        case 0xdc: return 14;
        case 0xde: return 18;
        case 0xdf: return 11;
        case 0xe0: return 18;
        case 0xe4: return 17;
        case 0xe5: return 14;
        case 0xe8: return 16;
        case 0xe9: return 8;
        case 0xea: return 11;
        case 0xec: return 13;
        case 0xf2: return 16;
        case 0xf3: return 14;
        case 0xfa: return 11;
        case 0xfb: return 13;
        case 0xfc: return 11;
        case 0xfe: return 15;
        default:
            break;
    }

    // Variable-length standard messages carry a six-bit length in octet 2.
    if ((op == 0x08 || op == 0x10 || op == 0x11 || op == 0x12) && hasBytes(bytes, offset + 1, 1)) {
        const size_t len = bytes[offset + 1] & 0x3fu;
        if (len >= 2) return len;
    }

    if ((op & 0xc0u) == 0x80u && hasBytes(bytes, offset + 1, 1)) {
        const uint8_t mfid = bytes[offset + 1];
        if (mfid == 0x90) {
            switch (op) {
                case 0x80: return 8;
                case 0x81: return 17;
                case 0x83: return 7;
                case 0x84: return 11;
                case 0x89: return 17;
                case 0x91: return 17;
                case 0x95: return 17;
                case 0xa0: return 16;
                case 0xa3: return 11;
                case 0xa4: return 13;
                case 0xa5: return 11;
                case 0xa6: return 11;
                case 0xa7: return 11;
                case 0xa8: return 10;
                default: break;
            }
        } else if (mfid == 0xa4) {
            switch (op) {
                case 0xa0: return 9;
                case 0xaa: return 17;
                case 0xac: return 12;
                default: break;
            }
        }
    }

    // Manufacturer-specific MAC messages (B1/B2 == 10) carry MFID in the
    // second octet and a six-bit length field in the third octet.
    if ((op & 0xc0u) == 0x80u && hasBytes(bytes, offset + 2, 1)) {
        const size_t len = bytes[offset + 2] & 0x3fu;
        if (len >= 3) return len;
    }
    return 0;
}

int tdmaSlotsForChannelType(uint8_t channelType)
{
    return channelTypeProfile(channelType).slotsPerCarrier;
}

P25ChannelIdentifier makeTdmaIdentifierPlan(uint8_t id,
                                            uint8_t channelType,
                                            double baseHz,
                                            double spacingHz,
                                            uint32_t offsetMagnitude,
                                            bool positiveOffset)
{
    const auto profile = channelTypeProfile(channelType);
    P25ChannelIdentifier plan;
    plan.valid = true;
    plan.id = id;
    plan.channelType = channelType;
    plan.baseHz = baseHz;
    plan.spacingHz = spacingHz;
    plan.txOffsetHz = static_cast<double>(offsetMagnitude) * profile.bandwidthHz * (positiveOffset ? 1.0 : -1.0);
    plan.bandwidthHz = profile.bandwidthHz;
    plan.slotsPerCarrier = profile.slotsPerCarrier;
    plan.phase2Capable = profile.tdma;
    return plan;
}

bool shouldEmitGrant(uint32_t group, uint16_t channel, uint32_t firstGroup = 0)
{
    return group != 0 && channel != 0 && group != firstGroup;
}

void populateIdentifierEvent(P25ControlEvent& out, const P25ChannelIdentifier& plan, uint8_t channelType)
{
    out.identifierKnown = true;
    out.identifier = plan.id;
    out.channelType = channelType;
    out.slotsPerCarrier = plan.slotsPerCarrier;
    out.baseFrequencyHz = plan.baseHz;
    out.channelSpacingHz = plan.spacingHz;
    out.transmitOffsetHz = plan.txOffsetHz;
    out.bandwidthHz = plan.bandwidthHz;
    out.phase2Candidate = plan.phase2Capable;
}

} // namespace

void P25ControlChannelAnalyzer::reset()
{
    m_identifiers = {};
    m_nacKnown = false;
    m_nac = 0;
    m_networkStatusKnown = false;
    m_wacn = 0;
    m_systemId = 0;
    m_lra = 0;
    m_rfssStatusKnown = false;
    m_rfssId = 0;
    m_siteId = 0;
}

void P25ControlChannelAnalyzer::setNac(uint16_t nac)
{
    m_nacKnown = true;
    m_nac = static_cast<uint16_t>(nac & 0x0fffu);
}

std::optional<double> P25ControlChannelAnalyzer::channelToFrequencyHz(uint16_t channel) const
{
    const uint8_t id = static_cast<uint8_t>((channel >> 12) & 0x0f);
    const uint16_t number = static_cast<uint16_t>(channel & 0x0fff);
    const auto& plan = m_identifiers[id];
    if (!plan.valid || !finitePositive(plan.baseHz) || !finitePositive(plan.spacingHz)) return std::nullopt;

    const int slots = std::max(1, plan.slotsPerCarrier);
    const uint16_t carrierNumber = static_cast<uint16_t>(number / slots);
    return plan.baseHz + static_cast<double>(carrierNumber) * plan.spacingHz;
}

void P25ControlChannelAnalyzer::setChannelIdentifier(const P25ChannelIdentifier& identifier)
{
    if (identifier.id >= m_identifiers.size()) return;
    if (!identifier.valid || !finitePositive(identifier.baseHz) || !finitePositive(identifier.spacingHz)) return;
    P25ChannelIdentifier plan = identifier;
    plan.slotsPerCarrier = std::max(1, plan.slotsPerCarrier);
    plan.phase2Capable = plan.phase2Capable || plan.slotsPerCarrier > 1;
    m_identifiers[plan.id] = plan;
}

void P25ControlChannelAnalyzer::annotateCurrentSystemMetadata(P25ControlEvent& event) const
{
    annotateSystemMetadata(event);
}

std::vector<P25ControlEvent> P25ControlChannelAnalyzer::ingestTsbk(const std::vector<uint8_t>& block)
{
    // Production hardening stage 3: do not silently zero-pad short/manual TSBKs.
    // Live decoder paths already pass FEC/CRC-validated 12-byte blocks. Padding
    // a 10-byte partial block can synthesize false grants in CLI/API tooling.
    if (block.size() < 12) return {};

    std::vector<uint8_t> paddedTsbk;
    bool normalized = false;
    const auto& b = normalizeTsbkBlock(block, paddedTsbk, normalized);
    if (!normalized) return {};

    P25ControlEvent event;
    event.opcode = static_cast<uint8_t>(readBitsMsb(b, 2, 6));
    event.mfid = static_cast<uint8_t>(readBitsMsb(b, 8, 8));

    switch (event.opcode) {
        case 0x33:
            applyIdentifierUpdateTdma(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x3d:
            applyIdentifierUpdate(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x34:
            applyIdentifierUpdateVhfUhf(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x3a:
            applyRfssStatus(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x3b:
            applyNetworkStatus(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        {
            event.type = P25ControlEventType::DataChannel;
            event.label = standardOspTsbkLabel(event.opcode);
            if (event.opcode == 0x10) {
                event.channel = static_cast<uint16_t>(readBitsMsb(b, 16, 16));
                event.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
            } else if (event.opcode == 0x11) {
                event.channel = static_cast<uint16_t>(readBitsMsb(b, 24, 16));
                event.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 40, 16));
                event.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
            } else if (event.opcode == 0x12) {
                event.channel = static_cast<uint16_t>(readBitsMsb(b, 16, 16));
                event.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 32, 16));
                event.channelB = static_cast<uint16_t>(readBitsMsb(b, 48, 16));
            } else {
                event.channel = static_cast<uint16_t>(readBitsMsb(b, 32, 16));
                event.channelB = static_cast<uint16_t>(readBitsMsb(b, 48, 16));
                event.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 64, 16));
            }
            if (auto f = channelToFrequencyHz(event.channel)) event.channelFrequencyHz = *f;
            if (auto f = channelToFrequencyHz(event.channelB)) event.channelFrequencyHzB = *f;
            annotateSystemMetadata(event);
            return {event};
        }
        case 0x14:
        case 0x16:
            applySndcpDataChannel(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x29:
        case 0x39:
            applySecondaryControlChannelBroadcast(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x3c:
        case 0x3e:
            applyAdjacentStatusBroadcast(b, event);
            annotateSystemMetadata(event);
            return {event};
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x06:
        case 0x08:
        case 0x09:
        {
            auto events = parseGrantBlock(b);
            for (auto& ev : events) annotateSystemMetadata(ev);
            return events;
        }
        default:
            if (event.mfid == 0x00) {
                event.type = P25ControlEventType::ControlMessage;
                event.label = standardOspTsbkLabel(event.opcode);
            } else if (event.mfid == 0x90) {
                event.type = P25ControlEventType::VendorCommand;
                event.label = motorolaOspTsbkLabel(event.opcode);
            } else if (event.mfid == 0xa4) {
                event.type = P25ControlEventType::VendorCommand;
                event.label = harrisOspTsbkLabel(event.opcode);
            } else {
                event.type = P25ControlEventType::VendorCommand;
                event.label = "Vendor OSP opcode " + hexOpcode(event.opcode);
            }
            annotateSystemMetadata(event);
            return {event};
    }
}

std::vector<P25ControlEvent> P25ControlChannelAnalyzer::ingestPhase2MacPdu(uint8_t pduType,
                                                                            uint8_t pduOffset,
                                                                            const std::vector<uint8_t>& bytes,
                                                                            bool crcValid)
{
    if (!crcValid || bytes.empty()) return {};

    const uint8_t headerPduType = static_cast<uint8_t>((bytes[0] >> 5) & 0x07u);
    const uint8_t headerPduOffset = static_cast<uint8_t>((bytes[0] >> 2) & 0x07u);
    if (headerPduType != 0 || (pduType & 0x07u) == 0) pduType = headerPduType;
    if (headerPduOffset != 0 || (pduOffset & 0x07u) == 0) pduOffset = headerPduOffset;

    std::vector<P25ControlEvent> events;
    const uint8_t normalizedPduType = static_cast<uint8_t>(pduType & 0x07u);
    const uint8_t normalizedPduOffset = static_cast<uint8_t>(pduOffset & 0x07u);

    if (normalizedPduType == 1) {
        P25ControlEvent ev = makePhase2MacEvent(normalizedPduType,
                                                normalizedPduOffset,
                                                normalizedPduType,
                                                0,
                                                P25ControlEventType::GroupVoiceUser,
                                                "Phase 2 MAC PTT");
        if (hasBytes(bytes, 1, ev.messageIndicator.size())) {
            ev.pttEncryptionSyncKnown = true;
            std::copy_n(bytes.begin() + 1, ev.messageIndicator.size(), ev.messageIndicator.begin());
        }
        if (hasBytes(bytes, 10, 3)) {
            ev.pttEncryptionSyncKnown = true;
            ev.algorithmId = bytes[10];
            ev.keyId = readU16Msb(bytes, 11);
            ev.encryptionKnown = true;
            ev.encrypted = ev.algorithmId != 0x80;
        }
        if (hasBytes(bytes, 13, 5)) {
            ev.sourceId = readU24Msb(bytes, 13);
            ev.talkgroupId = readU16Msb(bytes, 16);
        }
        ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
        ev.phase2Candidate = true;
        annotateSystemMetadata(ev);
        events.push_back(ev);
        return events;
    }

    if (normalizedPduType == 2) {
        P25ControlEvent ev = makePhase2MacEvent(normalizedPduType,
                                                normalizedPduOffset,
                                                normalizedPduType,
                                                0,
                                                P25ControlEventType::GroupVoiceEnd,
                                                "Phase 2 MAC end PTT");
        if (hasBytes(bytes, 1, 2)) {
            ev.endPttNacKnown = true;
            ev.endPttNac = static_cast<uint16_t>(readBitsMsb(bytes, 12, 12));
        }
        if (hasBytes(bytes, 13, 5)) {
            ev.sourceId = readU24Msb(bytes, 13);
            ev.talkgroupId = readU16Msb(bytes, 16);
        }
        ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
        ev.phase2Candidate = true;
        annotateSystemMetadata(ev);
        events.push_back(ev);
        return events;
    }

    events = parsePhase2MacMessages(normalizedPduType, normalizedPduOffset, bytes);
    for (auto& ev : events) annotateSystemMetadata(ev);
    return events;
}

void P25ControlChannelAnalyzer::applyIdentifierUpdate(const std::vector<uint8_t>& b, P25ControlEvent& out)
{
    const uint8_t id = static_cast<uint8_t>(readBitsMsb(b, 16, 4));
    const uint32_t bandwidthUnits = static_cast<uint32_t>(readBitsMsb(b, 20, 9));
    const uint32_t offsetRaw = static_cast<uint32_t>(readBitsMsb(b, 29, 9));
    const uint32_t spacingUnits = static_cast<uint32_t>(readBitsMsb(b, 38, 10));
    const uint32_t baseUnits = static_cast<uint32_t>(readBitsMsb(b, 48, 32));

    const bool positiveOffset = (offsetRaw & 0x100u) != 0;
    const uint32_t offsetMagnitude = offsetRaw & 0x0ffu;

    P25ChannelIdentifier plan;
    plan.valid = true;
    plan.id = id;
    plan.channelType = 0;
    plan.baseHz = static_cast<double>(baseUnits) * 5.0;
    plan.spacingHz = static_cast<double>(spacingUnits) * 125.0;
    plan.txOffsetHz = static_cast<double>(offsetMagnitude) * 250000.0 * (positiveOffset ? 1.0 : -1.0);
    plan.bandwidthHz = static_cast<double>(bandwidthUnits) * 125.0;
    plan.slotsPerCarrier = 1;
    plan.phase2Capable = plan.slotsPerCarrier > 1;

    if (id < m_identifiers.size() && finitePositive(plan.baseHz) && finitePositive(plan.spacingHz)) {
        m_identifiers[id] = plan;
    }

    out.type = P25ControlEventType::IdentifierUpdate;
    out.label = "Identifier update";
    out.channel = static_cast<uint16_t>(id << 12);
    populateIdentifierEvent(out, plan, 0);
    (void)bandwidthUnits;
}

void P25ControlChannelAnalyzer::applyIdentifierUpdateVhfUhf(const std::vector<uint8_t>& b, P25ControlEvent& out)
{
    const uint8_t id = static_cast<uint8_t>(readBitsMsb(b, 16, 4));
    const uint32_t offsetRaw = static_cast<uint32_t>(readBitsMsb(b, 22, 14));
    const uint32_t spacingUnits = static_cast<uint32_t>(readBitsMsb(b, 38, 10));
    const uint32_t baseUnits = static_cast<uint32_t>(readBitsMsb(b, 48, 32));

    const double spacingHz = static_cast<double>(spacingUnits) * 125.0;
    const bool positiveOffset = (offsetRaw & 0x2000u) != 0;
    const uint32_t offsetMagnitude = offsetRaw & 0x1fffu;

    P25ChannelIdentifier plan;
    plan.valid = true;
    plan.id = id;
    plan.channelType = 0;
    plan.baseHz = static_cast<double>(baseUnits) * 5.0;
    plan.spacingHz = spacingHz;
    plan.txOffsetHz = static_cast<double>(offsetMagnitude) * spacingHz * (positiveOffset ? 1.0 : -1.0);
    plan.bandwidthHz = 0.0;
    plan.slotsPerCarrier = 1;
    plan.phase2Capable = plan.slotsPerCarrier > 1;

    if (id < m_identifiers.size() && finitePositive(plan.baseHz) && finitePositive(plan.spacingHz)) {
        m_identifiers[id] = plan;
    }

    out.type = P25ControlEventType::IdentifierUpdate;
    out.label = "Identifier update VHF/UHF";
    out.channel = static_cast<uint16_t>(id << 12);
    populateIdentifierEvent(out, plan, 0);
}

void P25ControlChannelAnalyzer::applyIdentifierUpdateTdma(const std::vector<uint8_t>& b, P25ControlEvent& out)
{
    const uint8_t id = static_cast<uint8_t>(readBitsMsb(b, 16, 4));
    const uint8_t channelType = static_cast<uint8_t>(readBitsMsb(b, 20, 4));
    const uint32_t offsetRaw = static_cast<uint32_t>(readBitsMsb(b, 24, 14));
    const uint32_t spacingUnits = static_cast<uint32_t>(readBitsMsb(b, 38, 10));
    const uint32_t baseUnits = static_cast<uint32_t>(readBitsMsb(b, 48, 32));

    const double spacingHz = static_cast<double>(spacingUnits) * 125.0;
    const bool positiveOffset = (offsetRaw & 0x2000u) != 0;
    const uint32_t offsetMagnitude = offsetRaw & 0x1fffu;

    P25ChannelIdentifier plan = makeTdmaIdentifierPlan(id,
                                                       channelType,
                                                       static_cast<double>(baseUnits) * 5.0,
                                                       spacingHz,
                                                       offsetMagnitude,
                                                       positiveOffset);

    if (id < m_identifiers.size() && finitePositive(plan.baseHz) && finitePositive(plan.spacingHz)) {
        m_identifiers[id] = plan;
    }

    out.type = P25ControlEventType::IdentifierUpdate;
    out.label = "Identifier update TDMA";
    if (!channelTypeProfile(channelType).known) {
        out.label += " (unknown channel type; assuming " + std::to_string(plan.slotsPerCarrier) + "-slot)";
    }
    out.channel = static_cast<uint16_t>(id << 12);
    populateIdentifierEvent(out, plan, channelType);
}

void P25ControlChannelAnalyzer::applyNetworkStatus(const std::vector<uint8_t>& b, P25ControlEvent& out)
{
    out.type = P25ControlEventType::NetworkStatus;
    out.label = "Network status broadcast";
    out.networkStatusKnown = true;
    out.lra = static_cast<uint8_t>(readBitsMsb(b, 16, 8));
    out.wacn = static_cast<uint32_t>(readBitsMsb(b, 24, 20));
    out.systemId = static_cast<uint16_t>(readBitsMsb(b, 44, 12));
    out.controlChannel = static_cast<uint16_t>(readBitsMsb(b, 56, 16));
    if (auto f = channelToFrequencyHz(out.controlChannel)) out.controlChannelFrequencyHz = *f;
    m_networkStatusKnown = true;
    m_wacn = out.wacn;
    m_systemId = out.systemId;
    m_lra = out.lra;
}

void P25ControlChannelAnalyzer::applyRfssStatus(const std::vector<uint8_t>& b, P25ControlEvent& out)
{
    out.type = P25ControlEventType::RfssStatus;
    out.label = "RFSS status broadcast";
    out.rfssStatusKnown = true;
    out.lra = static_cast<uint8_t>(readBitsMsb(b, 16, 8));
    out.rfssNetworkActiveKnown = true;
    out.rfssNetworkActive = readBitsMsb(b, 27, 1) != 0;
    out.systemId = static_cast<uint16_t>(readBitsMsb(b, 28, 12));
    out.rfssId = static_cast<uint8_t>(readBitsMsb(b, 40, 8));
    out.siteId = static_cast<uint8_t>(readBitsMsb(b, 48, 8));
    out.controlChannel = static_cast<uint16_t>(readBitsMsb(b, 56, 16));
    if (auto f = channelToFrequencyHz(out.controlChannel)) out.controlChannelFrequencyHz = *f;
    m_rfssStatusKnown = true;
    m_systemId = out.systemId;
    m_lra = out.lra;
    m_rfssId = out.rfssId;
    m_siteId = out.siteId;
}

void P25ControlChannelAnalyzer::applySecondaryControlChannelBroadcast(const std::vector<uint8_t>& b,
                                                                      P25ControlEvent& out)
{
    const uint8_t opcode = static_cast<uint8_t>(readBitsMsb(b, 2, 6));
    out.type = P25ControlEventType::SecondaryControlChannel;
    out.label = opcode == 0x29 ? "Secondary control channel broadcast explicit"
                               : "Secondary control channel broadcast";
    out.rfssStatusKnown = true;
    out.rfssId = static_cast<uint8_t>(readBitsMsb(b, 16, 8));
    out.siteId = static_cast<uint8_t>(readBitsMsb(b, 24, 8));
    out.controlChannel = static_cast<uint16_t>(readBitsMsb(b, 32, 16));
    out.controlChannelB = static_cast<uint16_t>(readBitsMsb(b, 56, 16));
    if (auto f = channelToFrequencyHz(out.controlChannel)) out.controlChannelFrequencyHz = *f;
    if (auto f = channelToFrequencyHz(out.controlChannelB)) out.controlChannelFrequencyHzB = *f;
}

void P25ControlChannelAnalyzer::applyAdjacentStatusBroadcast(const std::vector<uint8_t>& b,
                                                             P25ControlEvent& out)
{
    const uint8_t opcode = static_cast<uint8_t>(readBitsMsb(b, 2, 6));
    out.type = P25ControlEventType::AdjacentStatus;
    out.label = opcode == 0x3e ? "Adjacent status broadcast uncoordinated band plan"
                               : "Adjacent status broadcast";
    out.rfssStatusKnown = true;
    out.rfssId = static_cast<uint8_t>(readBitsMsb(b, 40, 8));
    out.siteId = static_cast<uint8_t>(readBitsMsb(b, 48, 8));
    out.controlChannel = static_cast<uint16_t>(readBitsMsb(b, 56, 16));
    if (auto f = channelToFrequencyHz(out.controlChannel)) out.controlChannelFrequencyHz = *f;
}

void P25ControlChannelAnalyzer::applySndcpDataChannel(const std::vector<uint8_t>& b,
                                                      P25ControlEvent& out)
{
    const uint8_t opcode = static_cast<uint8_t>(readBitsMsb(b, 2, 6));
    out.type = P25ControlEventType::DataChannel;
    out.label = opcode == 0x14 ? "SNDCP data channel grant"
                               : "SNDCP data channel announcement explicit";
    if (opcode == 0x14) {
        out.channel = static_cast<uint16_t>(readBitsMsb(b, 24, 16));
        out.channelB = static_cast<uint16_t>(readBitsMsb(b, 40, 16));
        out.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
    } else {
        out.channel = static_cast<uint16_t>(readBitsMsb(b, 32, 16));
        out.channelB = static_cast<uint16_t>(readBitsMsb(b, 48, 16));
    }
    if (auto f = channelToFrequencyHz(out.channel)) out.channelFrequencyHz = *f;
    if (auto f = channelToFrequencyHz(out.channelB)) out.channelFrequencyHzB = *f;
}

std::vector<P25ControlEvent> P25ControlChannelAnalyzer::parsePhase2MacMessages(uint8_t pduType,
                                                                                uint8_t pduOffset,
                                                                                const std::vector<uint8_t>& bytes)
{
    std::vector<P25ControlEvent> events;
    if (bytes.size() < 2) return events;

    auto addVoiceChannel = [&](P25ControlEvent& ev, uint16_t channel) {
        ev.channel = channel;
        if (auto f = channelToFrequencyHz(ev.channel)) ev.voiceFrequencyHz = *f;
        annotateVoiceChannel(ev, ev.channel);
        if (ev.voiceProtocol == P25VoiceProtocol::Unknown) {
            ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
            ev.phase2Candidate = true;
        }
    };

    auto addExplicitVoiceChannel = [&](P25ControlEvent& ev, uint16_t transmitChannel, uint16_t receiveChannel) {
        ev.explicitChannelKnown = true;
        ev.explicitChannel.valid = true;
        ev.explicitChannel.protocolTransmitChannel = transmitChannel;
        ev.explicitChannel.protocolReceiveChannel = receiveChannel;
        ev.explicitChannel.scannerDownlinkChannel = transmitChannel;
        ev.explicitChannel.subscriberUplinkChannel = receiveChannel;
        ev.explicitChannel.transmitBand = static_cast<uint8_t>((transmitChannel >> 12) & 0x0f);
        ev.explicitChannel.transmitNumber = static_cast<uint16_t>(transmitChannel & 0x0fff);
        ev.explicitChannel.receiveBand = static_cast<uint8_t>((receiveChannel >> 12) & 0x0f);
        ev.explicitChannel.receiveNumber = static_cast<uint16_t>(receiveChannel & 0x0fff);
        ev.explicitChannel.downlinkChannel = transmitChannel;
        ev.explicitChannel.uplinkChannel = receiveChannel;
        ev.channel = ev.explicitChannel.downlinkChannel;
        ev.channelB = ev.explicitChannel.uplinkChannel;
        if (auto f = channelToFrequencyHz(ev.explicitChannel.downlinkChannel)) {
            ev.explicitChannel.downlinkHz = *f;
            ev.explicitChannel.transmitHz = *f;
            ev.explicitChannel.protocolTransmitHz = *f;
            ev.explicitChannel.scannerDownlinkHz = *f;
            ev.voiceFrequencyHz = *f;
        }
        if (auto f = channelToFrequencyHz(ev.explicitChannel.uplinkChannel)) {
            ev.explicitChannel.uplinkHz = *f;
            ev.explicitChannel.receiveHz = *f;
            ev.explicitChannel.protocolReceiveHz = *f;
            ev.explicitChannel.subscriberUplinkHz = *f;
            ev.voiceFrequencyHzB = *f;
        }
        annotateVoiceChannel(ev, ev.explicitChannel.downlinkChannel);
        if (ev.voiceProtocol == P25VoiceProtocol::Unknown) {
            ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
            ev.phase2Candidate = true;
        }
    };

    auto makeGrant = [&](uint8_t op, size_t pos, P25ControlEventType type, const char* label) {
        return makePhase2MacEvent(pduType, pduOffset, op, pos, type, label);
    };

    auto appendUnknownOrVendor = [&](uint8_t op, size_t pos) {
        const uint8_t mfid = ((op & 0xc0u) == 0x80u && hasBytes(bytes, pos + 1, 1)) ? bytes[pos + 1] : 0;
        const std::string label = p25Phase2MacMessageOpcodeToString(op, mfid);
        P25ControlEvent ev = makePhase2MacEvent(
            pduType,
            pduOffset,
            op,
            pos,
            (op & 0xc0u) == 0x80u ? P25ControlEventType::VendorCommand :
                (isUnsupportedPhase2MacLabel(label) ? P25ControlEventType::Unknown : P25ControlEventType::ControlMessage),
            label);
        ev.mfid = mfid;
        events.push_back(ev);
    };

    auto appendImplicitGrant = [&](uint8_t op,
                                   size_t pos,
                                   size_t serviceOffset,
                                   size_t channelOffset,
                                   size_t groupOffset,
                                   const char* label,
                                   uint32_t previousGroup = 0) {
        const uint16_t channel = readU16Msb(bytes, channelOffset);
        const uint32_t group = readU16Msb(bytes, groupOffset);
        if (!shouldEmitGrant(group, channel, previousGroup)) return;
        P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceUpdate, label);
        applyServiceOptions(ev, bytes[serviceOffset]);
        ev.talkgroupId = group;
        addVoiceChannel(ev, channel);
        events.push_back(ev);
    };

    auto appendExplicitGrant = [&](uint8_t op,
                                   size_t pos,
                                   size_t serviceOffset,
                                   size_t txChannelOffset,
                                   size_t rxChannelOffset,
                                   size_t groupOffset,
                                   const char* label,
                                   uint32_t previousGroup = 0) {
        const uint16_t txChannel = readU16Msb(bytes, txChannelOffset);
        const uint16_t rxChannel = readU16Msb(bytes, rxChannelOffset);
        const uint32_t group = readU16Msb(bytes, groupOffset);
        if (!shouldEmitGrant(group, txChannel, previousGroup)) return;
        P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceGrantExplicit, label);
        if (serviceOffset < bytes.size()) applyServiceOptions(ev, bytes[serviceOffset]);
        ev.talkgroupId = group;
        addExplicitVoiceChannel(ev, txChannel, rxChannel);
        events.push_back(ev);
    };

    auto storeIdentifierPlan = [&](uint8_t id,
                                   uint8_t channelType,
                                   double baseHz,
                                   double spacingHz,
                                   double txOffsetHz,
                                   double bandwidthHz,
                                   int slotsPerCarrier,
                                   bool phase2Capable) {
        P25ChannelIdentifier plan;
        plan.valid = true;
        plan.id = id;
        plan.channelType = channelType;
        plan.baseHz = baseHz;
        plan.spacingHz = spacingHz;
        plan.txOffsetHz = txOffsetHz;
        plan.bandwidthHz = bandwidthHz;
        plan.slotsPerCarrier = std::max(1, slotsPerCarrier);
        plan.phase2Capable = phase2Capable;
        if (id < m_identifiers.size() && finitePositive(plan.baseHz) && finitePositive(plan.spacingHz)) {
            m_identifiers[id] = plan;
        }
        return plan;
    };

    auto appendMacIdentifier = [&](uint8_t op, size_t pos, const char* label) {
        const int bit = static_cast<int>(pos * 8);
        const uint8_t id = static_cast<uint8_t>(readBitsMsb(bytes, bit + 8, 4));
        const uint32_t bandwidthUnits = static_cast<uint32_t>(readBitsMsb(bytes, bit + 12, 9));
        const bool positiveOffset = readBitsMsb(bytes, bit + 21, 1) != 0;
        const uint32_t offsetMagnitude = static_cast<uint32_t>(readBitsMsb(bytes, bit + 22, 8));
        const uint32_t spacingUnits = static_cast<uint32_t>(readBitsMsb(bytes, bit + 30, 10));
        const uint32_t baseUnits = static_cast<uint32_t>(readBitsMsb(bytes, bit + 40, 32));
        const auto plan = storeIdentifierPlan(
            id,
            0,
            static_cast<double>(baseUnits) * 5.0,
            static_cast<double>(spacingUnits) * 125.0,
            static_cast<double>(offsetMagnitude) * 250000.0 * (positiveOffset ? 1.0 : -1.0),
            static_cast<double>(bandwidthUnits) * 125.0,
            1,
            false);
        P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::IdentifierUpdate, label);
        ev.channel = static_cast<uint16_t>(id << 12);
        populateIdentifierEvent(ev, plan, 0);
        events.push_back(ev);
    };

    auto appendMacIdentifierVhfUhf = [&](uint8_t op, size_t pos, const char* label) {
        const int bit = static_cast<int>(pos * 8);
        const uint8_t id = static_cast<uint8_t>(readBitsMsb(bytes, bit + 8, 4));
        const uint8_t bandwidthCode = static_cast<uint8_t>(readBitsMsb(bytes, bit + 12, 4));
        const bool positiveOffset = readBitsMsb(bytes, bit + 16, 1) != 0;
        const uint32_t offsetMagnitude = static_cast<uint32_t>(readBitsMsb(bytes, bit + 17, 13));
        const uint32_t spacingUnits = static_cast<uint32_t>(readBitsMsb(bytes, bit + 30, 10));
        const uint32_t baseUnits = static_cast<uint32_t>(readBitsMsb(bytes, bit + 40, 32));
        const double spacingHz = static_cast<double>(spacingUnits) * 125.0;
        const double bandwidthHz = bandwidthCode == 0x4 ? 6250.0 : (bandwidthCode == 0x5 ? 12500.0 : 0.0);
        const auto plan = storeIdentifierPlan(
            id,
            0,
            static_cast<double>(baseUnits) * 5.0,
            spacingHz,
            static_cast<double>(offsetMagnitude) * spacingHz * (positiveOffset ? 1.0 : -1.0),
            bandwidthHz,
            1,
            false);
        P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::IdentifierUpdate, label);
        ev.channel = static_cast<uint16_t>(id << 12);
        populateIdentifierEvent(ev, plan, 0);
        events.push_back(ev);
    };

    auto appendMacIdentifierTdma = [&](uint8_t op, size_t pos, const char* label, bool extended) {
        const int bit = static_cast<int>(pos * 8);
        const int idBit = extended ? bit + 16 : bit + 8;
        const int signBit = extended ? bit + 24 : bit + 16;
        const int offsetBit = extended ? bit + 25 : bit + 17;
        const int spacingBit = extended ? bit + 38 : bit + 30;
        const int baseBit = extended ? bit + 48 : bit + 40;
        const uint8_t id = static_cast<uint8_t>(readBitsMsb(bytes, idBit, 4));
        const uint8_t channelType = static_cast<uint8_t>(readBitsMsb(bytes, idBit + 4, 4));
        const bool positiveOffset = readBitsMsb(bytes, signBit, 1) != 0;
        const uint32_t offsetMagnitude = static_cast<uint32_t>(readBitsMsb(bytes, offsetBit, 13));
        const uint32_t spacingUnits = static_cast<uint32_t>(readBitsMsb(bytes, spacingBit, 10));
        const uint32_t baseUnits = static_cast<uint32_t>(readBitsMsb(bytes, baseBit, 32));
        const auto tdmaPlan = makeTdmaIdentifierPlan(
            id,
            channelType,
            static_cast<double>(baseUnits) * 5.0,
            static_cast<double>(spacingUnits) * 125.0,
            offsetMagnitude,
            positiveOffset);
        const auto plan = storeIdentifierPlan(
            tdmaPlan.id,
            tdmaPlan.channelType,
            tdmaPlan.baseHz,
            tdmaPlan.spacingHz,
            tdmaPlan.txOffsetHz,
            tdmaPlan.bandwidthHz,
            tdmaPlan.slotsPerCarrier,
            tdmaPlan.phase2Capable);
        P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::IdentifierUpdate, label);
        ev.channel = static_cast<uint16_t>(id << 12);
        populateIdentifierEvent(ev, plan, channelType);
        if (extended) {
            ev.networkStatusKnown = true;
            ev.wacn = static_cast<uint32_t>(readBitsMsb(bytes, bit + 80, 20));
            ev.systemId = static_cast<uint16_t>(readBitsMsb(bytes, bit + 100, 12));
            if (ev.wacn != 0 || ev.systemId != 0) {
                m_networkStatusKnown = true;
                m_wacn = ev.wacn;
                m_systemId = ev.systemId;
            }
        }
        events.push_back(ev);
    };

    size_t pos = 1; // byte 0 is the MAC PDU header.
    while (pos < bytes.size()) {
        if (allRemainingZero(bytes, pos)) break;
        const uint8_t op = bytes[pos];
        const size_t len = phase2MacMessageLength(bytes, pos);
        if (len == 0 || !hasBytes(bytes, pos, len)) {
            const uint8_t mfid = ((op & 0xc0u) == 0x80u && hasBytes(bytes, pos + 1, 1)) ? bytes[pos + 1] : 0;
            const std::string label = p25Phase2MacMessageOpcodeToString(op, mfid);
            P25ControlEvent ev = makePhase2MacEvent(
                pduType,
                pduOffset,
                op,
                pos,
                isUnsupportedPhase2MacLabel(label) ? P25ControlEventType::Unknown : P25ControlEventType::ControlMessage,
                label);
            ev.mfid = mfid;
            events.push_back(ev);
            break;
        }

        switch (op) {
            case 0x00:
                break;

            case 0x01:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceUser,
                                               "Phase 2 MAC group voice channel user");
                applyServiceOptions(ev, bytes[pos + 1]);
                ev.talkgroupId = readU16Msb(bytes, pos + 2);
                ev.sourceId = readU24Msb(bytes, pos + 4);
                ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
                ev.phase2Candidate = true;
                events.push_back(ev);
                break;
            }

            case 0x21:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceUser,
                                               "Phase 2 MAC group voice channel user extended");
                applyServiceOptions(ev, bytes[pos + 1]);
                ev.talkgroupId = readU16Msb(bytes, pos + 2);
                ev.sourceId = readU24Msb(bytes, pos + 4);
                ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
                ev.phase2Candidate = true;
                events.push_back(ev);
                break;
            }

            case 0x05:
            {
                const uint32_t firstGroup = readU16Msb(bytes, pos + 4);
                const uint32_t secondGroup = readU16Msb(bytes, pos + 9);
                appendImplicitGrant(op, pos, pos + 1, pos + 2, pos + 4,
                                    "Phase 2 MAC group voice channel grant update multiple");
                appendImplicitGrant(op, pos, pos + 6, pos + 7, pos + 9,
                                    "Phase 2 MAC group voice channel grant update multiple",
                                    firstGroup);
                appendImplicitGrant(op, pos, pos + 11, pos + 12, pos + 14,
                                    "Phase 2 MAC group voice channel grant update multiple",
                                    secondGroup != 0 ? secondGroup : firstGroup);
                break;
            }

            case 0x25:
            {
                const uint32_t firstGroup = readU16Msb(bytes, pos + 6);
                appendExplicitGrant(op, pos, pos + 1, pos + 2, pos + 4, pos + 6,
                                    "Phase 2 MAC group voice channel grant update multiple explicit");
                appendExplicitGrant(op, pos, pos + 8, pos + 9, pos + 11, pos + 13,
                                    "Phase 2 MAC group voice channel grant update multiple explicit",
                                    firstGroup);
                break;
            }

            case 0x40:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceGrant,
                                               "Phase 2 MAC group voice channel grant");
                applyServiceOptions(ev, bytes[pos + 1]);
                ev.talkgroupId = readU16Msb(bytes, pos + 4);
                ev.sourceId = readU24Msb(bytes, pos + 6);
                addVoiceChannel(ev, readU16Msb(bytes, pos + 2));
                if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
                break;
            }

            case 0xc0:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceGrantExplicit,
                                               "Phase 2 MAC group voice channel grant explicit");
                applyServiceOptions(ev, bytes[pos + 1]);
                ev.talkgroupId = readU16Msb(bytes, pos + 6);
                ev.sourceId = readU24Msb(bytes, pos + 8);
                addExplicitVoiceChannel(ev, readU16Msb(bytes, pos + 2), readU16Msb(bytes, pos + 4));
                if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
                break;
            }

            case 0x42:
            {
                P25ControlEvent first = makeGrant(op, pos, P25ControlEventType::GroupVoiceUpdate,
                                                  "Phase 2 MAC group voice channel grant update");
                first.talkgroupId = readU16Msb(bytes, pos + 3);
                addVoiceChannel(first, readU16Msb(bytes, pos + 1));
                if (shouldEmitGrant(first.talkgroupId, first.channel)) events.push_back(first);

                P25ControlEvent second = makeGrant(op, pos, P25ControlEventType::GroupVoiceUpdate,
                                                   "Phase 2 MAC group voice channel grant update");
                second.talkgroupId = readU16Msb(bytes, pos + 7);
                addVoiceChannel(second, readU16Msb(bytes, pos + 5));
                if (shouldEmitGrant(second.talkgroupId, second.channel, first.talkgroupId)) events.push_back(second);
                break;
            }

            case 0xc3:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceGrantExplicit,
                                               "Phase 2 MAC group voice channel grant update explicit");
                ev.talkgroupId = readU16Msb(bytes, pos + 5);
                addExplicitVoiceChannel(ev, readU16Msb(bytes, pos + 1), readU16Msb(bytes, pos + 3));
                if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
                break;
            }

            case 0x73:
                appendMacIdentifierTdma(op, pos, "Phase 2 MAC identifier update TDMA abbreviated", false);
                break;

            case 0x74:
                appendMacIdentifierVhfUhf(op, pos, "Phase 2 MAC identifier update VHF/UHF");
                break;

            case 0x79:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::SecondaryControlChannel,
                                               "Phase 2 MAC secondary control channel broadcast implicit");
                ev.rfssStatusKnown = true;
                ev.rfssId = bytes[pos + 1];
                ev.siteId = bytes[pos + 2];
                ev.controlChannel = readU16Msb(bytes, pos + 3);
                ev.controlChannelB = readU16Msb(bytes, pos + 6);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                if (auto f = channelToFrequencyHz(ev.controlChannelB)) ev.controlChannelFrequencyHzB = *f;
                events.push_back(ev);
                break;
            }

            case 0x7a:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::RfssStatus,
                                               "Phase 2 MAC RFSS status broadcast implicit");
                ev.rfssStatusKnown = true;
                ev.lra = bytes[pos + 1];
                ev.rfssNetworkActiveKnown = true;
                ev.rfssNetworkActive = (bytes[pos + 2] & 0x10u) != 0;
                ev.systemId = static_cast<uint16_t>(readU16Msb(bytes, pos + 2) & 0x0fffu);
                ev.rfssId = bytes[pos + 4];
                ev.siteId = bytes[pos + 5];
                ev.controlChannel = readU16Msb(bytes, pos + 6);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                m_rfssStatusKnown = true;
                m_systemId = ev.systemId;
                m_lra = ev.lra;
                m_rfssId = ev.rfssId;
                m_siteId = ev.siteId;
                events.push_back(ev);
                break;
            }

            case 0x7b:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::NetworkStatus,
                                               "Phase 2 MAC network status broadcast implicit");
                ev.networkStatusKnown = true;
                ev.lra = bytes[pos + 1];
                ev.wacn = static_cast<uint32_t>((readU24Msb(bytes, pos + 2) >> 4) & 0xfffffu);
                ev.systemId = static_cast<uint16_t>(readU16Msb(bytes, pos + 4) & 0x0fffu);
                ev.controlChannel = readU16Msb(bytes, pos + 6);
                ev.nacKnown = true;
                ev.nac = static_cast<uint16_t>(readU16Msb(bytes, pos + 9) & 0x0fffu);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                m_networkStatusKnown = true;
                m_wacn = ev.wacn;
                m_systemId = ev.systemId;
                m_lra = ev.lra;
                m_nacKnown = true;
                m_nac = ev.nac;
                events.push_back(ev);
                break;
            }

            case 0x7c:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::AdjacentStatus,
                                               "Phase 2 MAC adjacent status broadcast implicit");
                ev.rfssStatusKnown = true;
                ev.lra = bytes[pos + 1];
                ev.systemId = static_cast<uint16_t>(readU16Msb(bytes, pos + 2) & 0x0fffu);
                ev.rfssId = bytes[pos + 4];
                ev.siteId = bytes[pos + 5];
                ev.controlChannel = readU16Msb(bytes, pos + 6);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                events.push_back(ev);
                break;
            }

            case 0x7d:
                appendMacIdentifier(op, pos, "Phase 2 MAC identifier update");
                break;

            case 0xe9:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::SecondaryControlChannel,
                                               "Phase 2 MAC secondary control channel broadcast explicit");
                ev.rfssStatusKnown = true;
                ev.rfssId = bytes[pos + 1];
                ev.siteId = bytes[pos + 2];
                ev.controlChannel = readU16Msb(bytes, pos + 3);
                ev.controlChannelB = readU16Msb(bytes, pos + 5);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                if (auto f = channelToFrequencyHz(ev.controlChannelB)) ev.controlChannelFrequencyHzB = *f;
                events.push_back(ev);
                break;
            }

            case 0xf3:
                appendMacIdentifierTdma(op, pos, "Phase 2 MAC identifier update TDMA extended", true);
                break;

            case 0xfa:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::RfssStatus,
                                               "Phase 2 MAC RFSS status broadcast explicit");
                ev.rfssStatusKnown = true;
                ev.lra = bytes[pos + 1];
                ev.systemId = static_cast<uint16_t>(readU16Msb(bytes, pos + 2) & 0x0fffu);
                ev.rfssId = bytes[pos + 4];
                ev.siteId = bytes[pos + 5];
                ev.controlChannel = readU16Msb(bytes, pos + 6);
                ev.channelB = readU16Msb(bytes, pos + 8);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                if (auto f = channelToFrequencyHz(ev.channelB)) ev.controlChannelFrequencyHzB = *f;
                m_rfssStatusKnown = true;
                m_systemId = ev.systemId;
                m_lra = ev.lra;
                m_rfssId = ev.rfssId;
                m_siteId = ev.siteId;
                events.push_back(ev);
                break;
            }

            case 0xfb:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::NetworkStatus,
                                               "Phase 2 MAC network status broadcast explicit");
                ev.networkStatusKnown = true;
                ev.lra = bytes[pos + 1];
                ev.wacn = static_cast<uint32_t>((readU24Msb(bytes, pos + 2) >> 4) & 0xfffffu);
                ev.systemId = static_cast<uint16_t>(readU16Msb(bytes, pos + 4) & 0x0fffu);
                ev.controlChannel = readU16Msb(bytes, pos + 6);
                ev.channelB = readU16Msb(bytes, pos + 8);
                ev.nacKnown = true;
                ev.nac = static_cast<uint16_t>(readU16Msb(bytes, pos + 11) & 0x0fffu);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                if (auto f = channelToFrequencyHz(ev.channelB)) ev.controlChannelFrequencyHzB = *f;
                m_networkStatusKnown = true;
                m_wacn = ev.wacn;
                m_systemId = ev.systemId;
                m_lra = ev.lra;
                m_nacKnown = true;
                m_nac = ev.nac;
                events.push_back(ev);
                break;
            }

            case 0xfc:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::AdjacentStatus,
                                               "Phase 2 MAC adjacent status broadcast explicit");
                ev.rfssStatusKnown = true;
                ev.lra = bytes[pos + 1];
                ev.systemId = static_cast<uint16_t>(readU16Msb(bytes, pos + 2) & 0x0fffu);
                ev.rfssId = bytes[pos + 4];
                ev.siteId = bytes[pos + 5];
                ev.controlChannel = readU16Msb(bytes, pos + 6);
                ev.channelB = readU16Msb(bytes, pos + 8);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                if (auto f = channelToFrequencyHz(ev.channelB)) ev.controlChannelFrequencyHzB = *f;
                events.push_back(ev);
                break;
            }

            case 0xfe:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::AdjacentStatus,
                                               "Phase 2 MAC adjacent status broadcast extended explicit");
                ev.networkStatusKnown = true;
                ev.rfssStatusKnown = true;
                ev.lra = bytes[pos + 2];
                ev.systemId = static_cast<uint16_t>(readU16Msb(bytes, pos + 3) & 0x0fffu);
                ev.rfssId = bytes[pos + 5];
                ev.siteId = bytes[pos + 6];
                ev.controlChannel = readU16Msb(bytes, pos + 7);
                ev.channelB = readU16Msb(bytes, pos + 9);
                ev.wacn = static_cast<uint32_t>((readU24Msb(bytes, pos + 12) >> 4) & 0xfffffu);
                if (auto f = channelToFrequencyHz(ev.controlChannel)) ev.controlChannelFrequencyHz = *f;
                if (auto f = channelToFrequencyHz(ev.channelB)) ev.controlChannelFrequencyHzB = *f;
                events.push_back(ev);
                break;
            }

            case 0x80:
            case 0xa0:
            {
                const uint8_t mfid = hasBytes(bytes, pos + 1, 1) ? bytes[pos + 1] : 0;
                if (mfid != 0x90) {
                    appendUnknownOrVendor(op, pos);
                    break;
                }
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceUser,
                                               op == 0x80
                                                   ? "Motorola Phase 2 regroup voice channel user"
                                                   : "Motorola Phase 2 regroup voice channel user extended");
                ev.mfid = mfid;
                if (hasBytes(bytes, pos + 2, 1)) applyServiceOptions(ev, bytes[pos + 2]);
                ev.talkgroupId = readU16Msb(bytes, op == 0x80 ? pos + 3 : pos + 4);
                ev.sourceId = readU24Msb(bytes, op == 0x80 ? pos + 5 : pos + 6);
                ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
                ev.phase2Candidate = true;
                events.push_back(ev);
                break;
            }

            case 0xa3:
            {
                const uint8_t mfid = hasBytes(bytes, pos + 1, 1) ? bytes[pos + 1] : 0;
                if (mfid != 0x90) {
                    appendUnknownOrVendor(op, pos);
                    break;
                }
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceGrant,
                                               "Motorola Phase 2 regroup channel grant");
                ev.mfid = mfid;
                if (hasBytes(bytes, pos + 2, 1)) applyServiceOptions(ev, bytes[pos + 2]);
                ev.talkgroupId = readU16Msb(bytes, pos + 6);
                ev.sourceId = readU24Msb(bytes, pos + 8);
                addVoiceChannel(ev, readU16Msb(bytes, pos + 4));
                if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
                break;
            }

            case 0xa4:
            {
                const uint8_t mfid = hasBytes(bytes, pos + 1, 1) ? bytes[pos + 1] : 0;
                if (mfid != 0x90) {
                    appendUnknownOrVendor(op, pos);
                    break;
                }
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceGrantExplicit,
                                               "Motorola Phase 2 regroup channel grant explicit");
                ev.mfid = mfid;
                if (hasBytes(bytes, pos + 2, 1)) applyServiceOptions(ev, bytes[pos + 2]);
                ev.talkgroupId = readU16Msb(bytes, pos + 8);
                ev.sourceId = readU24Msb(bytes, pos + 10);
                addExplicitVoiceChannel(ev, readU16Msb(bytes, pos + 4), readU16Msb(bytes, pos + 6));
                if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
                break;
            }

            case 0xa5:
            {
                const uint8_t mfid = hasBytes(bytes, pos + 1, 1) ? bytes[pos + 1] : 0;
                if (mfid != 0x90) {
                    appendUnknownOrVendor(op, pos);
                    break;
                }
                P25ControlEvent first = makeGrant(op, pos, P25ControlEventType::GroupVoiceUpdate,
                                                  "Motorola Phase 2 regroup channel update");
                first.mfid = mfid;
                if (hasBytes(bytes, pos + 2, 1)) applyServiceOptions(first, bytes[pos + 2]);
                first.talkgroupId = readU16Msb(bytes, pos + 5);
                addVoiceChannel(first, readU16Msb(bytes, pos + 3));
                if (shouldEmitGrant(first.talkgroupId, first.channel)) events.push_back(first);

                P25ControlEvent second = makeGrant(op, pos, P25ControlEventType::GroupVoiceUpdate,
                                                   "Motorola Phase 2 regroup channel update");
                second.mfid = mfid;
                if (hasBytes(bytes, pos + 2, 1)) applyServiceOptions(second, bytes[pos + 2]);
                second.talkgroupId = readU16Msb(bytes, pos + 9);
                addVoiceChannel(second, readU16Msb(bytes, pos + 7));
                if (shouldEmitGrant(second.talkgroupId, second.channel, first.talkgroupId)) events.push_back(second);
                break;
            }

            case 0x31:
            {
                P25ControlEvent ev = makeGrant(op, pos, P25ControlEventType::GroupVoiceEnd,
                                               "Phase 2 MAC release");
                ev.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
                ev.phase2Candidate = true;
                events.push_back(ev);
                break;
            }

            default:
            {
                appendUnknownOrVendor(op, pos);
                break;
            }
        }

        pos += len;
    }

    return events;
}

void P25ControlChannelAnalyzer::annotateVoiceChannel(P25ControlEvent& event, uint16_t channel) const
{
    const uint8_t id = static_cast<uint8_t>((channel >> 12) & 0x0f);
    if (id >= m_identifiers.size()) return;
    const auto& plan = m_identifiers[id];
    if (!plan.valid) return;
    populateIdentifierEvent(event, plan, plan.channelType);

    if (plan.slotsPerCarrier > 1) {
        event.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
        event.phase2Candidate = true;
        event.tdmaSlotKnown = true;
        event.tdmaSlot = static_cast<uint8_t>((channel & 0x0fffu) % static_cast<uint16_t>(plan.slotsPerCarrier));
    } else {
        event.voiceProtocol = P25VoiceProtocol::Phase1FDMA;
    }
}

void P25ControlChannelAnalyzer::annotateSystemMetadata(P25ControlEvent& event) const
{
    if (m_nacKnown) {
        event.nacKnown = true;
        event.nac = m_nac;
    }
    if (m_networkStatusKnown && !event.networkStatusKnown) {
        event.networkStatusKnown = true;
        event.wacn = m_wacn;
        event.systemId = m_systemId;
        event.lra = m_lra;
    }
    if (m_rfssStatusKnown && !event.rfssStatusKnown) {
        event.rfssStatusKnown = true;
        event.systemId = m_systemId;
        event.lra = m_lra;
        event.rfssId = m_rfssId;
        event.siteId = m_siteId;
    }
}

std::vector<P25ControlEvent> P25ControlChannelAnalyzer::parseGrantBlock(const std::vector<uint8_t>& b)
{
    const uint8_t opcode = static_cast<uint8_t>(readBitsMsb(b, 2, 6));
    const uint8_t mfid = static_cast<uint8_t>(readBitsMsb(b, 8, 8));
    std::vector<P25ControlEvent> events;

    auto applyExplicitChannel = [&](P25ControlEvent& ev, uint16_t downlinkChannel, uint16_t uplinkChannel) {
        ev.explicitChannelKnown = true;
        ev.explicitChannel.valid = true;
        ev.explicitChannel.protocolTransmitChannel = downlinkChannel;
        ev.explicitChannel.protocolReceiveChannel = uplinkChannel;
        ev.explicitChannel.scannerDownlinkChannel = downlinkChannel;
        ev.explicitChannel.subscriberUplinkChannel = uplinkChannel;
        ev.explicitChannel.transmitBand = static_cast<uint8_t>((downlinkChannel >> 12) & 0x0f);
        ev.explicitChannel.transmitNumber = static_cast<uint16_t>(downlinkChannel & 0x0fff);
        ev.explicitChannel.receiveBand = static_cast<uint8_t>((uplinkChannel >> 12) & 0x0f);
        ev.explicitChannel.receiveNumber = static_cast<uint16_t>(uplinkChannel & 0x0fff);
        ev.explicitChannel.downlinkChannel = downlinkChannel;
        ev.explicitChannel.uplinkChannel = uplinkChannel;
        ev.channel = downlinkChannel;
        ev.channelB = uplinkChannel;
        if (auto f = channelToFrequencyHz(downlinkChannel)) {
            ev.explicitChannel.downlinkHz = *f;
            ev.explicitChannel.transmitHz = *f;
            ev.explicitChannel.protocolTransmitHz = *f;
            ev.explicitChannel.scannerDownlinkHz = *f;
            ev.voiceFrequencyHz = *f;
        }
        if (auto f = channelToFrequencyHz(uplinkChannel)) {
            ev.explicitChannel.uplinkHz = *f;
            ev.explicitChannel.receiveHz = *f;
            ev.explicitChannel.protocolReceiveHz = *f;
            ev.explicitChannel.subscriberUplinkHz = *f;
            ev.voiceFrequencyHzB = *f;
        }
        annotateVoiceChannel(ev, downlinkChannel);
    };

    if (mfid != 0x00 && mfid != 0x90) {
        P25ControlEvent ev;
        ev.type = P25ControlEventType::VendorCommand;
        ev.opcode = opcode;
        ev.mfid = mfid;
        ev.label = mfid == 0xa4 ? harrisOspTsbkLabel(opcode) : "Vendor OSP opcode " + hexOpcode(opcode);
        events.push_back(ev);
        return events;
    }

    if (opcode == 0x00) {
        if (mfid == 0x90) {
            P25ControlEvent ev;
            ev.type = P25ControlEventType::VendorCommand;
            ev.opcode = opcode;
            ev.mfid = mfid;
            const auto supergroup = static_cast<uint32_t>(readBitsMsb(b, 16, 16));
            const auto group1 = static_cast<uint32_t>(readBitsMsb(b, 32, 16));
            const auto group2 = static_cast<uint32_t>(readBitsMsb(b, 48, 16));
            const auto group3 = static_cast<uint32_t>(readBitsMsb(b, 64, 16));
            ev.label = "Motorola group regroup add command SG=" + std::to_string(supergroup) +
                " members=" + std::to_string(group1) + "," + std::to_string(group2) + "," + std::to_string(group3);
            events.push_back(ev);
            return events;
        }

        P25ControlEvent ev;
        ev.type = P25ControlEventType::GroupVoiceGrant;
        ev.opcode = opcode;
        ev.mfid = mfid;
        ev.label = "Group voice channel grant";
        const uint8_t serviceOptions = static_cast<uint8_t>(readBitsMsb(b, 16, 8));
        applyServiceOptions(ev, serviceOptions);
        ev.channel = static_cast<uint16_t>(readBitsMsb(b, 24, 16));
        ev.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 40, 16));
        ev.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
        if (auto f = channelToFrequencyHz(ev.channel)) ev.voiceFrequencyHz = *f;
        annotateVoiceChannel(ev, ev.channel);
        if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
    } else if (opcode == 0x01) {
        P25ControlEvent ev;
        ev.opcode = opcode;
        ev.mfid = mfid;
        if (mfid == 0x90) {
            ev.type = P25ControlEventType::VendorCommand;
            const auto supergroup = static_cast<uint32_t>(readBitsMsb(b, 16, 16));
            const auto group1 = static_cast<uint32_t>(readBitsMsb(b, 32, 16));
            const auto group2 = static_cast<uint32_t>(readBitsMsb(b, 48, 16));
            const auto group3 = static_cast<uint32_t>(readBitsMsb(b, 64, 16));
            ev.label = "Motorola group regroup delete command SG=" + std::to_string(supergroup) +
                " members=" + std::to_string(group1) + "," + std::to_string(group2) + "," + std::to_string(group3);
        } else {
            ev.type = P25ControlEventType::Unknown;
            ev.label = "Reserved/unknown TSBK opcode";
        }
        events.push_back(ev);
    } else if (opcode == 0x02) {
        if (mfid == 0x90) {
            P25ControlEvent ev;
            ev.type = P25ControlEventType::GroupVoiceUpdate;
            ev.opcode = opcode;
            ev.mfid = mfid;
            ev.label = "Motorola group regroup channel grant";
            ev.channel = static_cast<uint16_t>(readBitsMsb(b, 24, 16));
            ev.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 40, 16));
            ev.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
            if (auto f = channelToFrequencyHz(ev.channel)) ev.voiceFrequencyHz = *f;
            annotateVoiceChannel(ev, ev.channel);
            if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
        } else {
            P25ControlEvent first;
            first.type = P25ControlEventType::GroupVoiceUpdate;
            first.opcode = opcode;
            first.mfid = mfid;
            first.label = "Group voice channel grant update";
            first.channel = static_cast<uint16_t>(readBitsMsb(b, 16, 16));
            first.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 32, 16));
            if (auto f = channelToFrequencyHz(first.channel)) first.voiceFrequencyHz = *f;
            annotateVoiceChannel(first, first.channel);
            if (shouldEmitGrant(first.talkgroupId, first.channel)) events.push_back(first);

            P25ControlEvent second = first;
            second.channel = static_cast<uint16_t>(readBitsMsb(b, 48, 16));
            second.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 64, 16));
            if (auto f = channelToFrequencyHz(second.channel)) second.voiceFrequencyHz = *f;
            else second.voiceFrequencyHz = 0.0;
            second.voiceProtocol = P25VoiceProtocol::Unknown;
            second.tdmaSlot = 0;
            second.tdmaSlotKnown = false;
            second.phase2Candidate = false;
            annotateVoiceChannel(second, second.channel);
            if (shouldEmitGrant(second.talkgroupId, second.channel, first.talkgroupId)) events.push_back(second);
        }
    } else if (opcode == 0x03) {
        if (mfid == 0x90) {
            P25ControlEvent first;
            first.type = P25ControlEventType::GroupVoiceUpdate;
            first.opcode = opcode;
            first.mfid = mfid;
            first.label = "Motorola group regroup channel grant update";
            first.channel = static_cast<uint16_t>(readBitsMsb(b, 16, 16));
            first.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 32, 16));
            if (auto f = channelToFrequencyHz(first.channel)) first.voiceFrequencyHz = *f;
            annotateVoiceChannel(first, first.channel);
            if (shouldEmitGrant(first.talkgroupId, first.channel)) events.push_back(first);

            P25ControlEvent second = first;
            second.channel = static_cast<uint16_t>(readBitsMsb(b, 48, 16));
            second.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 64, 16));
            if (auto f = channelToFrequencyHz(second.channel)) second.voiceFrequencyHz = *f;
            else second.voiceFrequencyHz = 0.0;
            second.voiceProtocol = P25VoiceProtocol::Unknown;
            second.tdmaSlot = 0;
            second.tdmaSlotKnown = false;
            second.phase2Candidate = false;
            annotateVoiceChannel(second, second.channel);
            if (shouldEmitGrant(second.talkgroupId, second.channel, first.talkgroupId)) events.push_back(second);
        } else {
            P25ControlEvent ev;
            ev.type = P25ControlEventType::GroupVoiceGrantExplicit;
            ev.opcode = opcode;
            ev.mfid = mfid;
            ev.label = "Group voice channel grant update explicit";
            const uint8_t serviceOptions = static_cast<uint8_t>(readBitsMsb(b, 16, 8));
            applyServiceOptions(ev, serviceOptions);
            ev.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 64, 16));
            applyExplicitChannel(ev,
                                 static_cast<uint16_t>(readBitsMsb(b, 32, 16)),
                                 static_cast<uint16_t>(readBitsMsb(b, 48, 16)));
            if (shouldEmitGrant(ev.talkgroupId, ev.channel)) events.push_back(ev);
        }
    } else if (opcode == 0x04 || opcode == 0x06) {
        P25ControlEvent ev;
        ev.opcode = opcode;
        ev.mfid = mfid;
        if (mfid != 0x00) {
            ev.type = P25ControlEventType::VendorCommand;
            ev.label = mfid == 0x90 ? motorolaOspTsbkLabel(opcode) : "Vendor OSP opcode " + hexOpcode(opcode);
            events.push_back(ev);
            return events;
        }
        ev.type = P25ControlEventType::UnitVoiceGrant;
        const uint32_t target = static_cast<uint32_t>(readBitsMsb(b, 32, 24));
        ev.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
        ev.channel = static_cast<uint16_t>(readBitsMsb(b, 16, 16));
        ev.label = opcode == 0x04 ? "Unit-to-unit voice channel grant"
                                  : "Unit-to-unit voice channel grant update";
        ev.label += " target=" + std::to_string(target);
        if (auto f = channelToFrequencyHz(ev.channel)) ev.voiceFrequencyHz = *f;
        annotateVoiceChannel(ev, ev.channel);
        events.push_back(ev);
    } else if (opcode == 0x08 || opcode == 0x09) {
        P25ControlEvent ev;
        ev.opcode = opcode;
        ev.mfid = mfid;
        if (mfid != 0x00) {
            ev.type = P25ControlEventType::VendorCommand;
            ev.label = mfid == 0x90 ? motorolaOspTsbkLabel(opcode) : "Vendor OSP opcode " + hexOpcode(opcode);
            events.push_back(ev);
            return events;
        }
        ev.type = P25ControlEventType::TelephoneVoiceGrant;
        ev.label = opcode == 0x08 ? "Telephone interconnect voice channel grant"
                                  : "Telephone interconnect voice channel grant update";
        const uint8_t serviceOptions = static_cast<uint8_t>(readBitsMsb(b, 16, 8));
        applyServiceOptions(ev, serviceOptions);
        ev.channel = static_cast<uint16_t>(readBitsMsb(b, 24, 16));
        ev.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
        if (auto f = channelToFrequencyHz(ev.channel)) ev.voiceFrequencyHz = *f;
        annotateVoiceChannel(ev, ev.channel);
        events.push_back(ev);
    }

    return events;
}

std::vector<uint8_t> p25ParseHexBytes(const std::string& text)
{
    std::string hex;
    hex.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isxdigit(c)) hex.push_back(static_cast<char>(c));
    }
    if (hex.size() % 2 != 0) return {};

    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int v = 0;
        std::stringstream ss;
        ss << std::hex << hex.substr(i, 2);
        ss >> v;
        out.push_back(static_cast<uint8_t>(v & 0xffu));
    }
    return out;
}

bool p25ControlEventIsVoiceGrant(const P25ControlEvent& event)
{
    const bool grantType =
        event.type == P25ControlEventType::GroupVoiceGrant ||
        event.type == P25ControlEventType::GroupVoiceUpdate ||
        event.type == P25ControlEventType::GroupVoiceGrantExplicit;
    return grantType && event.talkgroupId != 0;
}

std::string p25VoiceProtocolToString(P25VoiceProtocol protocol)
{
    switch (protocol) {
        case P25VoiceProtocol::Phase1FDMA: return "P25 Phase 1 FDMA";
        case P25VoiceProtocol::Phase2TDMA: return "P25 Phase 2 TDMA";
        case P25VoiceProtocol::Unknown:
        default: return "Unknown";
    }
}

std::string p25ControlEventTypeToString(P25ControlEventType type)
{
    switch (type) {
        case P25ControlEventType::IdentifierUpdate: return "Identifier";
        case P25ControlEventType::GroupVoiceGrant: return "Group Grant";
        case P25ControlEventType::GroupVoiceUpdate: return "Group Update";
        case P25ControlEventType::GroupVoiceGrantExplicit: return "Group Grant Explicit";
        case P25ControlEventType::UnitVoiceGrant: return "Unit Grant";
        case P25ControlEventType::TelephoneVoiceGrant: return "Telephone Grant";
        case P25ControlEventType::GroupVoiceUser: return "Group User";
        case P25ControlEventType::GroupVoiceEnd: return "Group End";
        case P25ControlEventType::NetworkStatus: return "Network Status";
        case P25ControlEventType::RfssStatus: return "RFSS Status";
        case P25ControlEventType::SecondaryControlChannel: return "Secondary CC";
        case P25ControlEventType::AdjacentStatus: return "Adjacent Status";
        case P25ControlEventType::DataChannel: return "Data Channel";
        case P25ControlEventType::ControlMessage: return "Control";
        case P25ControlEventType::VendorCommand: return "Vendor Command";
        case P25ControlEventType::Unknown:
        default: return "Unknown";
    }
}

std::string p25Phase2MacPduTypeToString(uint8_t pduType)
{
    switch (pduType & 0x07u) {
        case 0x0: return "MAC reserved/unknown";
        case 0x1: return "MAC_PTT";
        case 0x2: return "MAC_END_PTT";
        case 0x3: return "MAC_IDLE";
        case 0x4: return "MAC_ACTIVE";
        case 0x5: return "MAC reserved/unknown";
        case 0x6: return "MAC_HANGTIME";
        case 0x7: return "MAC reserved/unknown";
        default: return "MAC unknown";
    }
}

std::string p25Phase2MacMessageOpcodeToString(uint8_t opcode, uint8_t mfid)
{
    switch (opcode) {
        case 0x00: return "Null";
        case 0x01: return "Group voice channel user";
        case 0x02: return "Unit-to-unit voice channel user";
        case 0x03: return "Telephone interconnect voice channel user";
        case 0x05: return "Group voice channel grant update multiple";
        case 0x08: return "Null avoid zero bias";
        case 0x10: return "Multi-fragment continuation message";
        case 0x11: return "Indirect group paging without priority";
        case 0x12: return "Individual paging with priority";
        case 0x21: return "Group voice channel user extended";
        case 0x22: return "Unit-to-unit voice channel user extended";
        case 0x25: return "Group voice channel grant update multiple explicit";
        case 0x30: return "Power control signal quality";
        case 0x31: return "MAC release";
        case 0x40: return "Group voice channel grant";
        case 0x41: return "Group voice service request";
        case 0x42: return "Group voice channel grant update";
        case 0x44: return "Unit-to-unit voice service channel grant abbreviated";
        case 0x45: return "Unit-to-unit answer request abbreviated";
        case 0x46: return "Unit-to-unit voice channel grant update abbreviated";
        case 0x48: return "Telephone interconnect voice channel grant";
        case 0x49: return "Telephone interconnect voice channel grant update";
        case 0x4a: return "Telephone interconnect answer response";
        case 0x4c: return "Radio unit monitor command abbreviated";
        case 0x52: return "SNDCP data channel request";
        case 0x53: return "SNDCP data page response";
        case 0x54: return "SNDCP data channel grant";
        case 0x55: return "SNDCP data page request";
        case 0x58: return "Status update abbreviated";
        case 0x5a: return "Status query abbreviated";
        case 0x5c: return "Message update abbreviated";
        case 0x5d: return "Radio unit monitor command obsolete";
        case 0x5e: return "Radio unit monitor enhanced command abbreviated";
        case 0x5f: return "Call alert abbreviated";
        case 0x60: return "Acknowledge response FNE abbreviated";
        case 0x61: return "Queued response";
        case 0x64: return "Extended function command abbreviated";
        case 0x67: return "Deny response";
        case 0x68: return "Group affiliation response abbreviated";
        case 0x6a: return "Group affiliation query abbreviated";
        case 0x6b: return "Location registration response";
        case 0x6c: return "Unit registration response abbreviated";
        case 0x6d: return "Unit registration command abbreviated";
        case 0x6f: return "Deregistration acknowledge";
        case 0x70: return "Synchronization broadcast";
        case 0x71: return "Authentication demand";
        case 0x72: return "Authentication FNE response abbreviated";
        case 0x73: return "Identifier update TDMA abbreviated";
        case 0x74: return "Identifier update V/UHF";
        case 0x75: return "Time and date announcement";
        case 0x76: return "Roaming address command";
        case 0x77: return "Roaming address update";
        case 0x78: return "System service broadcast";
        case 0x79: return "Secondary control channel broadcast implicit";
        case 0x7a: return "RFSS status broadcast implicit";
        case 0x7b: return "Network status broadcast implicit";
        case 0x7c: return "Adjacent status broadcast implicit";
        case 0x7d: return "Identifier update";
        case 0x80:
            return mfid == 0x90 ? "Motorola regroup voice channel user" : "Manufacturer-specific MAC message";
        case 0x81:
            if (mfid == 0x90) return "Motorola regroup add command";
            if (mfid == 0xa4) return "L3Harris unknown opcode 129";
            return "Manufacturer-specific MAC message";
        case 0x82: return mfid == 0x90 ? "Motorola active group radios 130" : "Manufacturer-specific MAC message";
        case 0x83: return mfid == 0x90 ? "Motorola regroup voice channel update" : "Manufacturer-specific MAC message";
        case 0x84: return mfid == 0x90 ? "Motorola regroup extended function command" : "Manufacturer-specific MAC message";
        case 0x87: return mfid == 0x90 ? "Motorola unknown opcode 135" : "Manufacturer-specific MAC message";
        case 0x88: return "Unknown LCCH opcode 136";
        case 0x89: return mfid == 0x90 ? "Motorola regroup delete command" : "Manufacturer-specific MAC message";
        case 0x8b: return mfid == 0x90 ? "Motorola TDMA data channel" : "Manufacturer-specific MAC message";
        case 0x8f:
            if (mfid == 0x90) return "Motorola active group radios 143";
            if (mfid == 0xa4) return "L3Harris unknown opcode 143";
            return "Manufacturer-specific MAC message";
        case 0x90: return "Group regroup voice channel user abbreviated";
        case 0x91: return mfid == 0x90 ? "Motorola talker alias header" : "Manufacturer-specific MAC message";
        case 0x95: return mfid == 0x90 ? "Motorola talker alias data block" : "Manufacturer-specific MAC message";
        case 0xa0:
            if (mfid == 0x90) return "Motorola regroup voice channel user extended";
            if (mfid == 0xa4) return "L3Harris private data channel grant";
            return "Manufacturer-specific MAC message";
        case 0xa3: return mfid == 0x90 ? "Motorola regroup channel grant" : "Manufacturer-specific MAC message";
        case 0xa4: return mfid == 0x90 ? "Motorola regroup channel grant explicit" : "Manufacturer-specific MAC message";
        case 0xa5: return mfid == 0x90 ? "Motorola regroup channel update" : "Manufacturer-specific MAC message";
        case 0xa6: return mfid == 0x90 ? "Motorola queued response" : "Manufacturer-specific MAC message";
        case 0xa7: return mfid == 0x90 ? "Motorola deny response" : "Manufacturer-specific MAC message";
        case 0xa8:
            if (mfid == 0x90) return "Motorola acknowledge response";
            if (mfid == 0xa4) return "L3Harris talker alias";
            return "Manufacturer-specific MAC message";
        case 0xaa: return mfid == 0xa4 ? "L3Harris GPS location" : "Manufacturer-specific MAC message";
        case 0xac: return mfid == 0xa4 ? "L3Harris unit-to-unit data channel grant" : "Manufacturer-specific MAC message";
        case 0xb0: return mfid == 0xa4 ? "L3Harris group regroup explicit encryption command" : "Manufacturer-specific MAC message";
        case 0xbf: return mfid == 0x90 ? "Motorola active group radios 191" : "Manufacturer-specific MAC message";
        case 0xc0: return "Group voice channel grant explicit";
        case 0xc3: return "Group voice channel grant update explicit";
        case 0xc4: return "Unit-to-unit voice service channel grant extended VCH";
        case 0xc5: return "Unit-to-unit answer request extended";
        case 0xc6: return "Unit-to-unit voice channel grant update extended VCH";
        case 0xc7: return "Unit-to-unit voice channel grant update extended LCCH";
        case 0xc8: return "Telephone interconnect voice channel grant explicit";
        case 0xc9: return "Telephone interconnect voice channel grant update explicit";
        case 0xcb: return "Call alert extended LCCH";
        case 0xcc: return "Radio unit monitor command extended VCH";
        case 0xcd: return "Radio unit monitor command extended LCCH";
        case 0xce: return "Message update extended LCCH";
        case 0xcf: return "Unit-to-unit voice service grant extended LCCH";
        case 0xd6: return "SNDCP data channel announcement";
        case 0xd8: return "Status update extended VCH";
        case 0xd9: return "Status update extended LCCH";
        case 0xda: return "Status query extended VCH";
        case 0xdb: return "Status query extended LCCH";
        case 0xdc: return "Message update extended VCH";
        case 0xde: return "Radio unit monitor enhanced command extended";
        case 0xdf: return "Call alert extended VCH";
        case 0xe0: return "Acknowledge response FNE extended";
        case 0xe4: return "Extended function command extended VCH";
        case 0xe5: return "Extended function command extended LCCH";
        case 0xe8: return "Group affiliation response extended";
        case 0xe9: return "Secondary control channel broadcast explicit";
        case 0xea: return "Group affiliation query extended";
        case 0xec: return "Unit registration response extended";
        case 0xf2: return "Authentication FNE response extended";
        case 0xf3: return "Identifier update TDMA extended";
        case 0xfa: return "RFSS status broadcast explicit";
        case 0xfb: return "Network status broadcast explicit";
        case 0xfc: return "Adjacent status broadcast explicit";
        case 0xfe: return "Adjacent status broadcast extended explicit";
        default:
            if ((opcode & 0xc0u) == 0x80u) return "Manufacturer-specific MAC message";
            if (opcode <= 0x3f) return "Unknown TDMA MAC opcode";
            if (opcode <= 0x7f) return "Unknown Phase 1 MAC opcode";
            return "Unknown extended Phase 1 MAC opcode";
    }
}
