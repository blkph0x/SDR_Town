#pragma once

#include <cstdint>
#include <string>
#include <vector>

// AFSK 1200 baud AX.25 UI-frame decoder (APRS-capable text extraction).
class Ax25AprsDecoder {
public:
    void reset();
    // Feed mono PCM at sampleRateHz. Returns newly completed frame texts (may be empty).
    std::vector<std::string> processAudio(const float* samples, size_t count, double sampleRateHz);

    static uint16_t crc16Fcs(const uint8_t* data, size_t len);
    static bool verifyFcs(const std::vector<uint8_t>& frameWithFcs);
    static std::string formatUiFrame(const std::vector<uint8_t>& infoFrame);

private:
    void bitIn(bool bit);
    void onByte(uint8_t b);

    double phase_ = 0.0;
    double markPhase_ = 0.0;
    double spacePhase_ = 0.0;
    int bitClock_ = 0;
    int samplesPerBit_ = 40; // @ 48 kHz
    bool lastBit_ = true; // idle mark (no transition = 1)
    uint8_t ones_ = 0;
    bool inFrame_ = false;
    uint8_t byteAcc_ = 0;
    int bitCount_ = 0;
    std::vector<uint8_t> frameBuf_;
    std::vector<std::string> outFrames_;
};
