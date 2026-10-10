#include "frontend/LnbConversion.h"

#include <cmath>

double noiseTempFromNoiseFigureDb(double noiseFigureDb) {
    if (!std::isfinite(noiseFigureDb)) return 0.0;
    return 290.0 * (std::pow(10.0, noiseFigureDb / 10.0) - 1.0);
}

bool claimedNoiseFigureNeedsCaution(double noiseFigureDb) {
    return std::isfinite(noiseFigureDb) && noiseFigureDb < 0.2;
}

IfTune lnbTune(const LnbProfile& lnb, double trueRfHz, bool horizontalOrRight, bool highBand) {
    IfTune out;
    out.trueRfHz = trueRfHz;
    out.highBand = highBand;
    out.voltageV = horizontalOrRight ? 18 : 13;
    out.tone22kHz = lnb.toneSelectsHighBand && highBand;
    out.loHz = highBand ? lnb.loHighHz : lnb.loLowHz;
    if (!std::isfinite(trueRfHz) || trueRfHz <= 0.0 || !std::isfinite(out.loHz) || out.loHz <= 0.0) {
        out.error = "RF and LO must be finite and positive";
        return out;
    }
    out.ifHz = std::abs(trueRfHz - out.loHz);
    if (out.ifHz < 950e6 || out.ifHz > 2150e6) {
        out.error = "IF is outside 950-2150 MHz";
        return out;
    }
    out.ok = true;
    return out;
}
