#pragma once

#include "InmarsatMessageStore.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Clean-room Classic Aero ACARS / C-assign / lightweight ADS-C text parser.
class InmarsatAcars {
public:
    using MessageSink = std::function<void(const InmarsatMessage&)>;

    void reset();
    void feedBytes(const uint8_t* data, size_t len, double freqHz);
    void setSink(MessageSink sink) { sink_ = std::move(sink); }

    // Parse a complete ACARS text body (for tests / fixtures).
    static InmarsatMessage parseAcarsText(const std::string& text, double freqHz = 0.0);
    static bool tryParseAdscPosition(const std::string& text, double* lat, double* lon);
    static bool tryParseCassign(const std::string& text, uint32_t* aes, double* rxHz, double* txHz);

private:
    void scanBuffer(double freqHz);
    void emit(InmarsatMessage msg);

    std::vector<uint8_t> buf_;
    MessageSink sink_;
};
