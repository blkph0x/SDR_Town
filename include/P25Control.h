#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class P25ControlEventType {
    Unknown,
    IdentifierUpdate,
    GroupVoiceGrant,
    GroupVoiceUpdate,
    GroupVoiceGrantExplicit,
    NetworkStatus,
    RfssStatus,
    VendorCommand,
};

enum class P25VoiceProtocol {
    Unknown,
    Phase1FDMA,
    Phase2TDMA,
};

struct P25ChannelIdentifier {
    bool valid = false;
    uint8_t id = 0;
    double baseHz = 0.0;
    double spacingHz = 0.0;
    double txOffsetHz = 0.0;
    double bandwidthHz = 0.0;
    int slotsPerCarrier = 1;
    bool phase2Capable = false;
};

struct P25ControlEvent {
    P25ControlEventType type = P25ControlEventType::Unknown;
    uint8_t opcode = 0;
    uint8_t mfid = 0;
    std::string label;

    bool identifierKnown = false;
    uint8_t identifier = 0;
    uint8_t channelType = 0;
    int slotsPerCarrier = 1;
    double baseFrequencyHz = 0.0;
    double channelSpacingHz = 0.0;
    double transmitOffsetHz = 0.0;
    double bandwidthHz = 0.0;

    uint32_t talkgroupId = 0;
    uint32_t sourceId = 0;
    uint16_t channel = 0;
    uint16_t channelB = 0;
    double voiceFrequencyHz = 0.0;
    double voiceFrequencyHzB = 0.0;
    bool encryptionKnown = false;
    bool encrypted = false;
    P25VoiceProtocol voiceProtocol = P25VoiceProtocol::Unknown;
    uint8_t tdmaSlot = 0;
    bool tdmaSlotKnown = false;
    bool phase2Candidate = false;

    bool nacKnown = false;
    uint16_t nac = 0;
    bool networkStatusKnown = false;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint8_t lra = 0;
    uint16_t controlChannel = 0;
    double controlChannelFrequencyHz = 0.0;

    bool rfssStatusKnown = false;
    uint8_t rfssId = 0;
    uint8_t siteId = 0;
    bool rfssNetworkActiveKnown = false;
    bool rfssNetworkActive = false;
};

class P25ControlChannelAnalyzer {
public:
    void reset();
    void setNac(uint16_t nac);

    // Ingest one decoded P25 TSBK-like trunking block.
    // Accepts 10-byte payloads or 12-byte blocks that still include the trailing CRC.
    std::vector<P25ControlEvent> ingestTsbk(const std::vector<uint8_t>& block);

    std::optional<double> channelToFrequencyHz(uint16_t channel) const;
    const std::array<P25ChannelIdentifier, 16>& channelIdentifiers() const { return m_identifiers; }

private:
    std::array<P25ChannelIdentifier, 16> m_identifiers{};
    bool m_nacKnown = false;
    uint16_t m_nac = 0;
    bool m_networkStatusKnown = false;
    uint32_t m_wacn = 0;
    uint16_t m_systemId = 0;
    uint8_t m_lra = 0;
    bool m_rfssStatusKnown = false;
    uint8_t m_rfssId = 0;
    uint8_t m_siteId = 0;

    void applyIdentifierUpdate(const std::vector<uint8_t>& b, P25ControlEvent& out);
    void applyIdentifierUpdateVhfUhf(const std::vector<uint8_t>& b, P25ControlEvent& out);
    void applyIdentifierUpdateTdma(const std::vector<uint8_t>& b, P25ControlEvent& out);
    void applyNetworkStatus(const std::vector<uint8_t>& b, P25ControlEvent& out);
    void applyRfssStatus(const std::vector<uint8_t>& b, P25ControlEvent& out);
    std::vector<P25ControlEvent> parseGrantBlock(const std::vector<uint8_t>& b);
    void annotateVoiceChannel(P25ControlEvent& event, uint16_t channel) const;
    void annotateSystemMetadata(P25ControlEvent& event) const;
};

std::vector<uint8_t> p25ParseHexBytes(const std::string& text);
std::string p25ControlEventTypeToString(P25ControlEventType type);
std::string p25VoiceProtocolToString(P25VoiceProtocol protocol);
