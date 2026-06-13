#include "P25Control.h"

#include <algorithm>
#include <cctype>
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

std::vector<uint8_t> normalizeTsbkBlock(const std::vector<uint8_t>& block)
{
    if (block.size() < 10) return {};
    std::vector<uint8_t> out(12, 0);
    const size_t copy = std::min<size_t>(block.size(), 12);
    std::copy_n(block.begin(), copy, out.begin());
    return out;
}

bool finitePositive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

int tdmaSlotsForChannelType(uint8_t channelType)
{
    // P25 IDEN_UP_TDMA channel type is a compact standard profile index.
    // Type 3 is the common two-slot Phase 2 voice carrier. Type 4 is reserved
    // by some implementations for four-slot data/control use; unknown TDMA
    // profiles are treated conservatively as two-slot voice so grants are not
    // mistaken for Phase 1 FDMA.
    switch (channelType & 0x0fu) {
        case 0x0:
        case 0x1:
        case 0x2:
            return 1;
        case 0x4:
            return 4;
        default:
            return 2;
    }
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

std::vector<P25ControlEvent> P25ControlChannelAnalyzer::ingestTsbk(const std::vector<uint8_t>& block)
{
    const auto b = normalizeTsbkBlock(block);
    if (b.empty()) return {};

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
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        {
            auto events = parseGrantBlock(b);
            for (auto& ev : events) annotateSystemMetadata(ev);
            return events;
        }
        default:
            event.type = P25ControlEventType::Unknown;
            event.label = "Unsupported/unknown TSBK opcode";
            annotateSystemMetadata(event);
            return {event};
    }
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

    P25ChannelIdentifier plan;
    plan.valid = true;
    plan.id = id;
    plan.channelType = channelType;
    plan.baseHz = static_cast<double>(baseUnits) * 5.0;
    plan.spacingHz = spacingHz;
    plan.txOffsetHz = static_cast<double>(offsetMagnitude) * spacingHz * (positiveOffset ? 1.0 : -1.0);
    plan.bandwidthHz = 12500.0;
    plan.slotsPerCarrier = tdmaSlotsForChannelType(channelType);
    plan.phase2Capable = plan.slotsPerCarrier > 1;

    if (id < m_identifiers.size() && finitePositive(plan.baseHz) && finitePositive(plan.spacingHz)) {
        m_identifiers[id] = plan;
    }

    out.type = P25ControlEventType::IdentifierUpdate;
    out.label = "Identifier update TDMA";
    if (channelType > 0x4) {
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
        ev.encryptionKnown = true;
        ev.encrypted = (serviceOptions & 0x40u) != 0;
        ev.channel = static_cast<uint16_t>(readBitsMsb(b, 24, 16));
        ev.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 40, 16));
        ev.sourceId = static_cast<uint32_t>(readBitsMsb(b, 56, 24));
        if (auto f = channelToFrequencyHz(ev.channel)) ev.voiceFrequencyHz = *f;
        annotateVoiceChannel(ev, ev.channel);
        events.push_back(ev);
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
            events.push_back(ev);
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
            events.push_back(first);

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
            if (second.talkgroupId != 0 || second.channel != 0) events.push_back(second);
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
            events.push_back(first);

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
            if (second.talkgroupId != 0 || second.channel != 0) events.push_back(second);
        } else {
            P25ControlEvent ev;
            ev.type = P25ControlEventType::GroupVoiceGrantExplicit;
            ev.opcode = opcode;
            ev.mfid = mfid;
            ev.label = "Group voice channel grant update explicit";
            const uint8_t serviceOptions = static_cast<uint8_t>(readBitsMsb(b, 16, 8));
            ev.encryptionKnown = true;
            ev.encrypted = (serviceOptions & 0x40u) != 0;
            ev.channel = static_cast<uint16_t>(readBitsMsb(b, 32, 16));
            ev.channelB = static_cast<uint16_t>(readBitsMsb(b, 48, 16));
            ev.talkgroupId = static_cast<uint32_t>(readBitsMsb(b, 64, 16));
            if (auto f = channelToFrequencyHz(ev.channel)) ev.voiceFrequencyHz = *f;
            if (auto f = channelToFrequencyHz(ev.channelB)) ev.voiceFrequencyHzB = *f;
            annotateVoiceChannel(ev, ev.channel);
            events.push_back(ev);
        }
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
        case P25ControlEventType::NetworkStatus: return "Network Status";
        case P25ControlEventType::RfssStatus: return "RFSS Status";
        case P25ControlEventType::VendorCommand: return "Vendor Command";
        case P25ControlEventType::Unknown:
        default: return "Unknown";
    }
}
