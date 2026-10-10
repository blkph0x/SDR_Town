#include "frontend/LinkBudgetHint.h"

#include <cmath>

LinkMarginHint linkMarginHint(double dishCm, double claimedNoiseFigureDb, double elevationDeg, double minElevationDeg) {
    if (!std::isfinite(elevationDeg) || elevationDeg < minElevationDeg) return LinkMarginHint::Masked;
    if (!std::isfinite(dishCm) || dishCm <= 0.0 || !std::isfinite(claimedNoiseFigureDb))
        return LinkMarginHint::Unconfigured;
    if (dishCm < 45.0 || claimedNoiseFigureDb > 1.0) return LinkMarginHint::Poor;
    if (dishCm >= 90.0 && claimedNoiseFigureDb <= 0.5 && elevationDeg >= 20.0) return LinkMarginHint::Good;
    return LinkMarginHint::Fair;
}

const char* linkMarginHintName(LinkMarginHint hint) {
    switch (hint) {
    case LinkMarginHint::Masked: return "masked";
    case LinkMarginHint::Poor: return "poor";
    case LinkMarginHint::Fair: return "fair";
    case LinkMarginHint::Good: return "good";
    default: return "unconfigured";
    }
}
