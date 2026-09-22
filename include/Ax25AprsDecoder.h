#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Bell 202 AFSK 1200-baud AX.25 UI-frame decoder (APRS-capable text extraction).
// A small timing-phase bank prevents arbitrary audio block alignment from
// deciding whether a packet is decodable.  Completed frames are still accepted
// only after AX.25 FCS validation.
class Ax25AprsDecoder {
public:
    void reset();
    // Feed mono PCM at sampleRateHz. Returns newly completed frame texts (may be empty).
    std::vector<std::string> processAudio(const float* samples, size_t count, double sampleRateHz);

    static uint16_t crc16Fcs(const uint8_t* data, size_t len);
    static bool verifyFcs(const std::vector<uint8_t>& frameWithFcs);
    static std::string formatUiFrame(const std::vector<uint8_t>& infoFrame);

private:
    struct Candidate {
        int samplesUntilDecision = 1;
        double markI = 0.0;
        double markQ = 0.0;
        double spaceI = 0.0;
        double spaceQ = 0.0;
        bool lastTone = true;
        uint8_t ones = 0;
        bool inFrame = false;
        uint8_t byteAcc = 0;
        int bitCount = 0;
        std::vector<uint8_t> frameBuf;
    };

    void configureForRate(double sampleRateHz);
    void resetCandidate(Candidate& candidate, bool keepTiming);
    void bitIn(Candidate& candidate, bool toneIsMark);
    void onByte(Candidate& candidate, uint8_t byte);
    void emitFrame(const std::vector<uint8_t>& frameWithFcs);

    double markPhase_ = 0.0;
    double spacePhase_ = 0.0;
    double configuredRateHz_ = 0.0;
    int samplesPerBit_ = 40; // 48 kHz / 1200 baud
    std::vector<Candidate> candidates_;
    std::vector<std::string> outFrames_;
};
