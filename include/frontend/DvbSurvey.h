#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// DEC-0210 D1: occupancy from the supplied IF capture. Not a demodulator.
struct DvbSurveyResult {
    bool powerMeasured = false;
    double meanPower = 0.0;
    bool peakMeasured = false;
    double peakOffsetHz = 0.0;
    double occupancyHz = 0.0;
    bool symbolRateMeasured = false;
    bool plsDetected = false;
    std::string note;
};

DvbSurveyResult surveyIfCapture(const std::complex<float>* iq, std::size_t count, double sampleRateHz);

// Inventories MPEG-TS packets the caller already has. Scrambled packets are
// counted and their payload is not read. This function is not a demodulator.
struct ClearTsInventory {
    bool aligned = false;
    int packets = 0;
    int scrambledPackets = 0;
    std::vector<int> pids;
};

ClearTsInventory inventoryClearTransportStream(const std::uint8_t* data, std::size_t size);

// Supplied-rate demod is implemented. Commercial conditional-access decrypt is refused.
bool dvbDemodAvailable();
bool commercialDecryptAvailable();
const char* dvbStageNote();
