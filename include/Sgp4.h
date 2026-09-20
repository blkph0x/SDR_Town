#pragma once

#include <string>

// Compact SGP4 (near-Earth) for LEO pass / Doppler. Deep-space omitted.
namespace Sgp4 {

struct Elements {
    double epochJd = 0.0; // Julian date of epoch
    double ndot = 0.0;
    double nddot = 0.0;
    double bstar = 0.0;
    double inclo = 0.0;  // rad
    double nodeo = 0.0;  // rad
    double ecco = 0.0;
    double argpo = 0.0;  // rad
    double mo = 0.0;     // rad
    double no = 0.0;     // rad/min mean motion
    int satnum = 0;
    bool valid = false;
};

struct State {
    double r[3]{}; // TEME km
    double v[3]{}; // TEME km/s
    bool ok = false;
};

bool parseTle(const std::string& line1, const std::string& line2, Elements* out);
State propagate(const Elements& el, double minutesFromEpoch);

// Observer geodetic → ECEF km; TEME≈ECEF for short-horizon elevation (good enough for LEO UI).
void geodeticToEcef(double latDeg, double lonDeg, double altM, double ecef[3]);
void lookAngles(const double satEcef[3], const double satVel[3],
                double latDeg, double lonDeg, double altM,
                double* elevationDeg, double* azimuthDeg, double* rangeKm, double* rangeRateKmS);

// Convert TEME (SGP4 output) to ECEF using GMST at jdUtc, including Earth rotation on velocity.
void temeToEcef(double jdUtc, const double rTeme[3], const double vTeme[3],
                double rEcef[3], double vEcef[3]);

// Look angles + range-rate from TEME state at a given UTC Julian date (uses observer lat/lon/alt).
void lookAnglesTeme(double jdUtc, const double rTeme[3], const double vTeme[3],
                    double latDeg, double lonDeg, double altM,
                    double* elevationDeg, double* azimuthDeg, double* rangeKm, double* rangeRateKmS);

// f_rx - f_tx = f_tx * (-rangeRateKmS / c). Approaching (negative range rate) raises frequency.
double dopplerShiftHz(double freqHz, double rangeRateKmS);

double julianDateUtc(int year, int mon, int day, int hour, int min, double sec);
double julianNowUtc();
double minutesSinceEpoch(const Elements& el, double jdUtc);

} // namespace Sgp4
