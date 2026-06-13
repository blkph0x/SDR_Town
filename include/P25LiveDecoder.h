#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class P25DataUnitId : uint8_t {
    HDU = 0x0,
    TDU = 0x3,
    LDU1 = 0x5,
    TSDU = 0x7,
    LDU2 = 0xA,
    PDU = 0xC,
    TDULC = 0xF,
    Unknown = 0xFF,
};

enum class P25VoiceDecodeStatus {
    Decoded,
    BackendUnavailable,
    InvalidFrame,
};

struct P25LiveDecoderConfig {
    double symbolRate = 4800.0;
    double workSampleRate = 48000.0;
    double channelBandwidthHz = 12500.0;
    double c4fmInnerDeviationHz = 600.0;
    int maxFrameSyncBitErrors = 2;
    size_t maxFrameSyncs = 8;
    size_t maxRawTsbkBlocksPerFrame = 8;
};

struct P25FrameSyncEvent {
    size_t bitOffset = 0;
    bool inverted = false;
    int bitErrors = 0;
    double confidence = 0.0;
};

struct P25Nid {
    bool valid = false;
    size_t bitOffset = 0;
    uint16_t nac = 0;
    P25DataUnitId duid = P25DataUnitId::Unknown;
    uint8_t rawDuid = 0;
    uint64_t raw = 0;
    bool fecValidated = false;
    int correctedBitErrors = 0;
};

struct P25TsbkBlock {
    size_t bitOffset = 0;
    std::vector<uint8_t> bytes;
    bool crcPresent = true;
    bool crcValid = false;
    bool fecDecoded = false;
    int correctedDibitErrors = 0;
};

struct P25LiveDecoderStats {
    size_t inputSamples = 0;
    size_t discriminatorSamples = 0;
    size_t symbols = 0;
    size_t bits = 0;
    size_t frameSyncs = 0;
    int bestFrameSyncBitErrors = -1;
    size_t bestFrameSyncBitOffset = 0;
    bool bestFrameSyncInverted = false;
    bool bestFrameSyncBitAligned = false;
    int bestNidBchDistance = -1;
    bool bestNidValid = false;
    uint16_t bestNidNac = 0;
    uint8_t bestNidRawDuid = 0;
    double sampleRate = 0.0;
    double symbolRate = 4800.0;
    double symbolConfidence = 0.0;
    bool voiceBackendAvailable = false;
    std::string demodPath;
};

struct P25ImbeFrame {
    bool valid = false;
    size_t bitOffset = 0;
    std::array<uint8_t, 11> imbe88{};
    int correctedErrors = 0;
    std::string message;
};

struct P25LiveDecodeResult {
    std::vector<int> dibits;
    std::vector<uint8_t> bits;
    std::vector<P25FrameSyncEvent> syncs;
    std::vector<P25Nid> nids;
    std::vector<P25TsbkBlock> rawTsbkBlocks;
    std::vector<P25ImbeFrame> imbeFrames;
    P25LiveDecoderStats stats;
    std::vector<std::string> warnings;
};

struct P25VoiceDecodeResult {
    P25VoiceDecodeStatus status = P25VoiceDecodeStatus::BackendUnavailable;
    std::vector<float> pcm;
    double sampleRate = 8000.0;
    std::string message;
};

P25ImbeFrame p25DecodeImbeFrameFromVoiceDibits(const std::vector<int>& voiceFrameDibits);
uint64_t p25EncodeNidBch(uint16_t nac, P25DataUnitId duid);

class P25LiveDecoder {
public:
    static constexpr uint64_t FrameSyncWord = 0x5575F5FF77FFull;
    static constexpr size_t FrameSyncBits = 48;
    static constexpr size_t NidBits = 64;
    static constexpr double SymbolRate = 4800.0;

    explicit P25LiveDecoder(P25LiveDecoderConfig config = {});

    void reset();

    P25LiveDecodeResult processIq(const std::vector<std::complex<float>>& iq,
                                  double sampleRate,
                                  double centerFreqHz,
                                  double targetFreqHz);
    P25LiveDecodeResult processFmDiscriminator(const std::vector<float>& discriminatorHz,
                                                double sampleRate);
    P25LiveDecodeResult processHardDibits(const std::vector<int>& dibits);
    P25LiveDecodeResult processHardBits(const std::vector<uint8_t>& bits);

    static std::array<uint8_t, FrameSyncBits> frameSyncBits();
    static int dibitFromBits(uint8_t first, uint8_t second);
    static std::array<uint8_t, 2> bitsFromDibit(int dibit);
    static double nominalC4fmLevelForDibit(int dibit);
    static std::string dataUnitIdToString(P25DataUnitId duid);

private:
    P25LiveDecoderConfig m_config;
};

class P25ImbeVoiceDecoder {
public:
    P25ImbeVoiceDecoder();
    ~P25ImbeVoiceDecoder();

    P25ImbeVoiceDecoder(const P25ImbeVoiceDecoder&) = delete;
    P25ImbeVoiceDecoder& operator=(const P25ImbeVoiceDecoder&) = delete;
    P25ImbeVoiceDecoder(P25ImbeVoiceDecoder&&) noexcept;
    P25ImbeVoiceDecoder& operator=(P25ImbeVoiceDecoder&&) noexcept;

    bool backendAvailable() const;
    P25VoiceDecodeResult decodeImbe4400Frame(const std::array<uint8_t, 11>& imbe88);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
