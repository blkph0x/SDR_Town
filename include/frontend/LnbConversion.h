#pragma once

#include <string>

// DEC-0210: manufacturer claim plus optional calibration. Not a measured G/T.
struct LnbProfile {
    double loLowHz = 9.75e9;
    double loHighHz = 10.6e9;
    bool inverted = true;
    double noiseFigureDb = 0.7;
    bool noiseSpecifiedAsTemperature = false;
    double noiseTempK = 0.0;
    double calibrationOffsetDb = 0.0;
    double maxCurrentMa = 200.0;
    bool toneSelectsHighBand = true;
    std::string loKind = "unknown"; // pll, dro, unknown
    std::string diseqcHex;
};

struct IfTune {
    bool ok = false;
    double trueRfHz = 0.0;
    double ifHz = 0.0;
    double loHz = 0.0;
    bool highBand = false;
    int voltageV = 0;
    bool tone22kHz = false;
    std::string error;
};

// IEEE noise temperature from NF: T = T0*(10^(NF/10)-1), T0 = 290 K.
double noiseTempFromNoiseFigureDb(double noiseFigureDb);
bool claimedNoiseFigureNeedsCaution(double noiseFigureDb);

// Horizontal/right selects 18 V. Vertical/left selects 13 V.
// Band follows 22 kHz when toneSelectsHighBand is set.
IfTune lnbTune(const LnbProfile& lnb, double trueRfHz, bool horizontalOrRight, bool highBand);
