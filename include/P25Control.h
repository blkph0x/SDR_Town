#pragma once

#include <array>
#include <cstddef>
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
    UnitVoiceGrant,
    TelephoneVoiceGrant,
    GroupVoiceUser,
    GroupVoiceEnd,
    NetworkStatus,
    RfssStatus,
    SecondaryControlChannel,
    AdjacentStatus,
    DataChannel,
    ControlMessage,
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
    uint8_t channelType = 0;
    double baseHz = 0.0;
    double spacingHz = 0.0;
    double txOffsetHz = 0.0;
    double bandwidthHz = 0.0;
    int slotsPerCarrier = 1;
    bool phase2Capable = false;
};

struct P25ExplicitChannelDescriptor {
    bool valid = false;
    uint16_t protocolTransmitChannel = 0;
    uint16_t protocolReceiveChannel = 0;
    uint16_t scannerDownlinkChannel = 0;
    uint16_t subscriberUplinkChannel = 0;
    uint8_t transmitBand = 0;
    uint16_t transmitNumber = 0;
    uint8_t receiveBand = 0;
    uint16_t receiveNumber = 0;
    uint16_t downlinkChannel = 0;
    uint16_t uplinkChannel = 0;
    double protocolTransmitHz = 0.0;
    double protocolReceiveHz = 0.0;
    double scannerDownlinkHz = 0.0;
    double subscriberUplinkHz = 0.0;
    double transmitHz = 0.0;
    double receiveHz = 0.0;
    double downlinkHz = 0.0;
    double uplinkHz = 0.0;
};

struct P25ControlEvent {
    P25ControlEventType type = P25ControlEventType::Unknown;
    uint8_t opcode = 0;
    uint8_t mfid = 0;
    std::string label;
    bool phase2Mac = false;
    uint8_t macPduType = 0;
    uint8_t macPduOffset = 0;
    uint8_t macMessageOpcode = 0;
    size_t macMessageOffset = 0;

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
    double channelFrequencyHz = 0.0;
    double channelFrequencyHzB = 0.0;
    bool serviceOptionsKnown = false;
    uint8_t serviceOptions = 0;
    bool serviceEmergency = false;
    bool serviceDuplexFull = false;
    bool servicePacketMode = false;
    uint8_t servicePriority = 0;
    bool encryptionKnown = false;
    bool encrypted = false;
    P25VoiceProtocol voiceProtocol = P25VoiceProtocol::Unknown;
    uint8_t tdmaSlot = 0;
    bool tdmaSlotKnown = false;
    bool phase2Candidate = false;

    bool explicitChannelKnown = false;
    P25ExplicitChannelDescriptor explicitChannel;

    bool pttEncryptionSyncKnown = false;
    std::array<uint8_t, 9> messageIndicator{};
    uint8_t algorithmId = 0;
    uint16_t keyId = 0;
    bool endPttNacKnown = false;
    uint16_t endPttNac = 0;

    bool nacKnown = false;
    uint16_t nac = 0;
    bool networkStatusKnown = false;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint8_t lra = 0;
    uint16_t controlChannel = 0;
    uint16_t controlChannelB = 0;
    double controlChannelFrequencyHz = 0.0;
    double controlChannelFrequencyHzB = 0.0;

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
    // Accepts 12-byte blocks that still include the trailing CRC. Short/manual
    // 10-byte payloads are rejected rather than zero-padded to avoid false grants.
    std::vector<P25ControlEvent> ingestTsbk(const std::vector<uint8_t>& block);

    // Ingest one CRC-valid Phase 2 MAC PDU payload produced by the live decoder.
    // The first byte is the MAC PDU header; variable-length MAC messages begin at byte 1.
    std::vector<P25ControlEvent> ingestPhase2MacPdu(uint8_t pduType,
                                                    uint8_t pduOffset,
                                                    const std::vector<uint8_t>& bytes,
                                                    bool crcValid = true);

    std::optional<double> channelToFrequencyHz(uint16_t channel) const;
    const std::array<P25ChannelIdentifier, 16>& channelIdentifiers() const { return m_identifiers; }
    void setChannelIdentifier(const P25ChannelIdentifier& identifier);
    void annotateCurrentSystemMetadata(P25ControlEvent& event) const;

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
    void applySecondaryControlChannelBroadcast(const std::vector<uint8_t>& b, P25ControlEvent& out);
    void applyAdjacentStatusBroadcast(const std::vector<uint8_t>& b, P25ControlEvent& out);
    void applySndcpDataChannel(const std::vector<uint8_t>& b, P25ControlEvent& out);
    std::vector<P25ControlEvent> parseGrantBlock(const std::vector<uint8_t>& b);
    std::vector<P25ControlEvent> parsePhase2MacMessages(uint8_t pduType,
                                                        uint8_t pduOffset,
                                                        const std::vector<uint8_t>& bytes);
    void annotateVoiceChannel(P25ControlEvent& event, uint16_t channel) const;
    void annotateSystemMetadata(P25ControlEvent& event) const;
};

std::vector<uint8_t> p25ParseHexBytes(const std::string& text);
bool p25ControlEventIsVoiceGrant(const P25ControlEvent& event);
std::string p25ControlEventTypeToString(P25ControlEventType type);
std::string p25VoiceProtocolToString(P25VoiceProtocol protocol);
std::string p25Phase2MacPduTypeToString(uint8_t pduType);
std::string p25Phase2MacMessageOpcodeToString(uint8_t opcode, uint8_t mfid = 0);
