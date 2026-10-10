#pragma once

// DEC-0210: qualitative operator bin. Not a calculated C/N or G/T.
enum class LinkMarginHint { Unconfigured, Masked, Poor, Fair, Good };

LinkMarginHint linkMarginHint(double dishCm, double claimedNoiseFigureDb, double elevationDeg, double minElevationDeg);
const char* linkMarginHintName(LinkMarginHint hint);
