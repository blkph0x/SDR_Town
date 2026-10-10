#pragma once

#include <complex>
#include <cstddef>
#include <string>

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

// D2/D3 are not linked. Commercial conditional-access decrypt is refused.
bool dvbDemodAvailable();
bool commercialDecryptAvailable();
const char* dvbStageNote();
