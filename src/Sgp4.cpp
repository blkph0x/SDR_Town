#include "Sgp4.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include "../external/sgp4/SGP4.h"

namespace Sgp4 {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kDeg2Rad = kPi / 180.0;

double gstime(double jd) {
    double tut1 = (jd - 2451545.0) / 36525.0;
    double temp = -6.2e-6 * tut1 * tut1 * tut1 + 0.093104 * tut1 * tut1 +
                  (876600.0 * 3600.0 + 8640184.812866) * tut1 + 67310.54841;
    temp = std::fmod(temp * kDeg2Rad / 240.0, kTwoPi);
    if (temp < 0.0) temp += kTwoPi;
    return temp;
}

double julianDay(int year, int mon, int day, int hr, int minute, double sec) {
    int y = year;
    int m = mon;
    if (m <= 2) { y -= 1; m += 12; }
    const int A = y / 100;
    const int B = 2 - A + A / 4;
    const double jd = std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + day + B - 1524.5;
    return jd + (hr + minute / 60.0 + sec / 3600.0) / 24.0;
}

double wrapPi(double a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return a;
}

} // namespace

double julianDateUtc(int year, int mon, int day, int hour, int min, double sec) {
    return julianDay(year, mon, day, hour, min, sec);
}

double julianNowUtc() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return julianDay(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

bool parseTle(const std::string& line1, const std::string& line2, Elements* out) {
    if (!out) return false;
    *out = {};
    if (line1.size() < 69 || line2.size() < 69 || line1[0] != '1' || line2[0] != '2' ||
        line1.substr(2, 5) != line2.substr(2, 5)) return false;
    Elements el{};
    try {
        el.satnum = std::stoi(line1.substr(2, 5));
        // epoch yyddd.ffffffff
        const double epoch = std::stod(line1.substr(18, 14));
        int year = static_cast<int>(epoch / 1000.0);
        const double day = epoch - year * 1000.0;
        year += (year < 57) ? 2000 : 1900;
        el.epochJd = julianDay(year, 1, 1, 0, 0, 0.0) + day - 1.0;

        el.ndot = std::stod(line1.substr(33, 10));
        // More reliable bstar parse: cols 53-59 mantissa, 60-61 exp
        {
            const char s1 = line1[53];
            const std::string mantStr = line1.substr(54, 5);
            const char s2 = line1[59];
            const char echar = line1[60];
            double mant = std::stod(std::string("0.") + mantStr);
            if (s1 == '-') mant = -mant;
            int expv = echar - '0';
            if (s2 == '-') expv = -expv;
            el.bstar = mant * std::pow(10.0, expv);
        }

        el.inclo = std::stod(line2.substr(8, 8)) * kDeg2Rad;
        el.nodeo = std::stod(line2.substr(17, 8)) * kDeg2Rad;
        el.ecco = std::stod(std::string("0.") + line2.substr(26, 7));
        el.argpo = std::stod(line2.substr(34, 8)) * kDeg2Rad;
        el.mo = std::stod(line2.substr(43, 8)) * kDeg2Rad;
        const double nRevDay = std::stod(line2.substr(52, 11));
        el.no = nRevDay * kTwoPi / 1440.0; // rad/min
        el.valid = std::isfinite(el.no) && el.no > 0.0 && std::isfinite(el.ecco) &&
                   el.ecco >= 0.0 && el.ecco < 1.0 && std::isfinite(el.inclo) &&
                   el.inclo >= 0.0 && el.inclo <= kPi && std::isfinite(el.epochJd) &&
                   std::isfinite(el.bstar) && std::isfinite(el.nodeo) &&
                   std::isfinite(el.argpo) && std::isfinite(el.mo);
        *out = el;
        return el.valid;
    } catch (...) {
        return false;
    }
}

double minutesSinceEpoch(const Elements& el, double jdUtc) {
    return (jdUtc - el.epochJd) * 1440.0;
}

State propagate(const Elements& el, double tsince) {
    State state{};
    if (!el.valid || !std::isfinite(tsince) || !std::isfinite(el.epochJd) ||
        !std::isfinite(el.no) || el.no <= 0 || !std::isfinite(el.ecco) ||
        el.ecco < 0 || el.ecco >= 1 || !std::isfinite(el.inclo) ||
        el.inclo < 0 || el.inclo > kPi || !std::isfinite(el.nodeo) ||
        !std::isfinite(el.argpo) || !std::isfinite(el.mo) ||
        !std::isfinite(el.bstar)) return state;

    // DEC-0112: initialize a private record for independent, thread-safe and
    // non-monotonic queries, including the deep-space integrator.
    ElsetRec rec{};
    rec.whichconst = wgs72;
    rec.jdsatepoch = std::floor(el.epochJd);
    rec.jdsatepochF = el.epochJd - rec.jdsatepoch;
    rec.no_kozai = el.no;
    rec.ecco = el.ecco;
    rec.inclo = el.inclo;
    rec.nodeo = el.nodeo;
    rec.argpo = el.argpo;
    rec.mo = el.mo;
    rec.bstar = el.bstar;
    rec.ndot = el.ndot * kTwoPi / (1440.0 * 1440.0);
    rec.nddot = el.nddot * kTwoPi / (1440.0 * 1440.0 * 1440.0);
    if (!sgp4init('i', &rec) || rec.error != 0) return state;
    if (!sgp4(&rec, tsince, state.r, state.v) || rec.error != 0) return State{};
    state.ok = true;
    for (int i = 0; i < 3; ++i)
        state.ok = state.ok && std::isfinite(state.r[i]) && std::isfinite(state.v[i]);
    return state;
}

void geodeticToEcef(double latDeg, double lonDeg, double altM, double ecef[3]) {
    const double lat = latDeg * kDeg2Rad;
    const double lon = lonDeg * kDeg2Rad;
    const double a = 6378.137;
    const double f = 1.0 / 298.257223563;
    const double e2 = f * (2.0 - f);
    const double sl = std::sin(lat);
    const double cl = std::cos(lat);
    const double N = a / std::sqrt(1.0 - e2 * sl * sl);
    const double h = altM / 1000.0;
    ecef[0] = (N + h) * cl * std::cos(lon);
    ecef[1] = (N + h) * cl * std::sin(lon);
    ecef[2] = (N * (1.0 - e2) + h) * sl;
}

void lookAngles(const double satEcef[3], const double satVel[3],
                double latDeg, double lonDeg, double altM,
                double* elevationDeg, double* azimuthDeg, double* rangeKm, double* rangeRateKmS) {
    double obs[3];
    geodeticToEcef(latDeg, lonDeg, altM, obs);
    const double dx = satEcef[0] - obs[0];
    const double dy = satEcef[1] - obs[1];
    const double dz = satEcef[2] - obs[2];
    const double range = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (rangeKm) *rangeKm = range;

    const double lat = latDeg * kDeg2Rad;
    const double lon = lonDeg * kDeg2Rad;
    const double sl = std::sin(lat), cl = std::cos(lat);
    const double sb = std::sin(lon), cb = std::cos(lon);
    // topocentric SEZ
    const double south = sl * cb * dx + sl * sb * dy - cl * dz;
    const double east = -sb * dx + cb * dy;
    const double zenith = cl * cb * dx + cl * sb * dy + sl * dz;
    const double el = std::atan2(zenith, std::sqrt(south * south + east * east));
    double az = std::atan2(east, -south);
    if (az < 0.0) az += kTwoPi;
    if (elevationDeg) *elevationDeg = el / kDeg2Rad;
    if (azimuthDeg) *azimuthDeg = az / kDeg2Rad;

    if (rangeRateKmS) {
        const double rx = dx / range, ry = dy / range, rz = dz / range;
        *rangeRateKmS = rx * satVel[0] + ry * satVel[1] + rz * satVel[2];
    }
}

void temeToEcef(double jdUtc, const double rTeme[3], const double vTeme[3],
                double rEcef[3], double vEcef[3]) {
    const double theta = gstime(jdUtc);
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    rEcef[0] = c * rTeme[0] + s * rTeme[1];
    rEcef[1] = -s * rTeme[0] + c * rTeme[1];
    rEcef[2] = rTeme[2];
    const double vx = c * vTeme[0] + s * vTeme[1];
    const double vy = -s * vTeme[0] + c * vTeme[1];
    const double vz = vTeme[2];
    constexpr double kOmega = 7.2921151467e-5; // rad/s Earth rotation
    vEcef[0] = vx + kOmega * rEcef[1];
    vEcef[1] = vy - kOmega * rEcef[0];
    vEcef[2] = vz;
}

void lookAnglesTeme(double jdUtc, const double rTeme[3], const double vTeme[3],
                    double latDeg, double lonDeg, double altM,
                    double* elevationDeg, double* azimuthDeg, double* rangeKm, double* rangeRateKmS) {
    double rE[3], vE[3];
    temeToEcef(jdUtc, rTeme, vTeme, rE, vE);
    lookAngles(rE, vE, latDeg, lonDeg, altM, elevationDeg, azimuthDeg, rangeKm, rangeRateKmS);
}

double dopplerShiftHz(double freqHz, double rangeRateKmS) {
    constexpr double kCKmS = 299792.458;
    if (!std::isfinite(freqHz) || freqHz <= 0.0 || !std::isfinite(rangeRateKmS)) return 0.0;
    return freqHz * (-rangeRateKmS / kCKmS);
}

} // namespace Sgp4
