#include "Sgp4.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace Sgp4 {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kXke = 0.0743669161331734132; // sqrt(GM) earth (er^1.5/min) classic WGS
constexpr double kXkmper = 6378.135;
constexpr double kAe = 1.0;
constexpr double kCk2 = 5.413079e-4;
constexpr double kCk4 = 0.62098875e-6;
constexpr double kS = 1.012229;
constexpr double kQoms2t = 1.88027916e-9;
constexpr double kXj3 = -2.53881e-6;
constexpr double kZhr = 0.2617993877991494;

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
    if (!out || line1.size() < 69 || line2.size() < 69) return false;
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
        // bstar: 12345-6 style
        std::string bs = line1.substr(53, 8);
        // trim
        while (!bs.empty() && bs.front() == ' ') bs.erase(bs.begin());
        if (bs.size() >= 2) {
            const char signExp = bs.back();
            bs.pop_back();
            char signMant = '+';
            if (!bs.empty() && (bs[0] == '+' || bs[0] == '-')) {
                signMant = bs[0];
                bs.erase(bs.begin());
            }
            double mant = std::stod(std::string("0.") + bs);
            if (signMant == '-') mant = -mant;
            const int expv = signExp - '0';
            // last char is sign of exponent in classic TLE; actually format is NNNNN±N
            // line1 cols 54-61: bstar as ±NNNNN±N
        }
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
        el.valid = el.no > 0.0 && el.ecco < 1.0;
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
    State st{};
    if (!el.valid) return st;

    const double cosio = std::cos(el.inclo);
    const double sinio = std::sin(el.inclo);
    const double ao = std::pow(kXke / el.no, 2.0 / 3.0);
    const double del1 = 1.5 * kCk2 * (3.0 * cosio * cosio - 1.0) / (ao * ao * std::pow(1.0 - el.ecco * el.ecco, 1.5));
    const double a1 = ao * (1.0 - del1 / 3.0 - del1 * del1 - 134.0 * del1 * del1 * del1 / 81.0);
    const double delo = 1.5 * kCk2 * (3.0 * cosio * cosio - 1.0) / (a1 * a1 * std::pow(1.0 - el.ecco * el.ecco, 1.5));
    const double xnodp = el.no / (1.0 + delo);
    const double aodp = a1 / (1.0 - delo);

    const bool isimp = (aodp * (1.0 - el.ecco) / kAe) < (220.0 / kXkmper + kAe);
    double s4 = kS;
    double qoms24 = kQoms2t;
    const double perige = (aodp * (1.0 - el.ecco) - kAe) * kXkmper;
    if (perige < 156.0) {
        s4 = perige - 78.0;
        if (perige <= 98.0) s4 = 20.0;
        qoms24 = std::pow((120.0 - s4) * kAe / kXkmper, 4.0);
        s4 = s4 / kXkmper + kAe;
    }

    const double pinvsq = 1.0 / (aodp * aodp * (1.0 - el.ecco * el.ecco) * (1.0 - el.ecco * el.ecco));
    const double tsi = 1.0 / (aodp - s4);
    const double eta = aodp * el.ecco * tsi;
    const double etasq = eta * eta;
    const double eeta = el.ecco * eta;
    const double psisq = std::abs(1.0 - etasq);
    const double coef = qoms24 * std::pow(tsi, 4.0);
    const double coef1 = coef / std::pow(psisq, 3.5);
    const double c2 = coef1 * xnodp *
                      (aodp * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
                       0.75 * kCk2 * tsi / psisq * el.no *
                           (3.0 * cosio * cosio - 1.0) * (8.0 + 3.0 * etasq * (8.0 + etasq)));
    const double c1 = el.bstar * c2;
    const double sinio2 = sinio * sinio;
    const double a3ovk2 = -kXj3 / kCk2 * std::pow(kAe, 3.0);
    const double c3 = coef * tsi * a3ovk2 * xnodp * kAe * sinio / el.ecco;
    const double x1mth2 = 1.0 - cosio * cosio;
    const double c4 = 2.0 * xnodp * coef1 * aodp * (1.0 - el.ecco * el.ecco) *
                      (eta * (2.0 + 0.5 * etasq) + el.ecco * (0.5 + 2.0 * etasq) -
                       2.0 * kCk2 * tsi / (aodp * psisq) *
                           (-3.0 * (1.0 - cosio * cosio) * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
                            0.75 * (1.0 - cosio * cosio) * (2.0 * etasq - eeta * (1.0 + etasq)) * std::cos(2.0 * el.argpo)));
    double c5 = 0.0;
    if (!isimp)
        c5 = 2.0 * coef1 * aodp * (1.0 - el.ecco * el.ecco) * (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);

    const double theta2 = cosio * cosio;
    const double pinh = 1.0 - el.ecco * el.ecco;
    const double xmdot = xnodp + 0.5 * xnodp * kCk2 * pinvsq * (3.0 * theta2 - 1.0) * std::sqrt(pinh);
    const double x1m5th = 1.0 - 5.0 * theta2;
    const double xhdot1 = -2.0 * xnodp * kCk2 * pinvsq * cosio;
    const double omgdot = -0.5 * xnodp * kCk2 * pinvsq * x1m5th;
    const double xnodot = xhdot1 + 0.5 * xhdot1 * kCk2 * pinvsq * (4.0 - 19.0 * theta2);

    const double xmdf = el.mo + xmdot * tsince;
    const double omgadf = el.argpo + omgdot * tsince;
    const double xnoddf = el.nodeo + xnodot * tsince;
    double tsq = tsince * tsince;
    double xnode = xnoddf + xnodot * 1.5 * c1 * tsq;
    double tempa = 1.0 - c1 * tsince;
    double tempe = el.bstar * c4 * tsince;
    double templ = xnodp * 1.5 * c1 * tsq;
    if (!isimp) {
        const double tcube = tsq * tsince;
        const double tfour = tcube * tsince;
        const double d2 = 4.0 * aodp * tsi * c1 * c1;
        const double d3 = (17.0 * aodp + s4) * tsi * d2 * c1 / 3.0;
        const double d4 = 0.5 * tsi * d2 * c1 * aodp * tsi * (221.0 * aodp + 31.0 * s4) * c1 / 3.0;
        tempa = tempa - d2 * tsq - d3 * tcube - d4 * tfour;
        tempe = tempe + el.bstar * c5 * (std::sin(xmdf) - std::sin(el.mo));
        templ = templ + xnodp * (d2 * 2.0 * tsq + d3 * 3.0 * tcube + d4 * 2.0 * tfour) * 0.25;
    }
    double a = aodp * tempa * tempa;
    double e = el.ecco - tempe;
    if (e >= 1.0 || e < -0.001) return st;
    e = std::max(1.0e-6, e);
    const double xl = xmdf + omgadf + xnode + templ;
    const double beta = std::sqrt(1.0 - e * e);
    const double xn = kXke / std::pow(a, 1.5);

    // Kepler
    double u = wrapPi(xl - xnode);
    double eo1 = u;
    for (int i = 0; i < 10; ++i) {
        const double sineo1 = std::sin(eo1);
        const double coseo1 = std::cos(eo1);
        const double tem5 = 1.0 - coseo1 * e;
        const double delta = (u - eo1 + e * sineo1) / tem5;
        eo1 += delta;
        if (std::abs(delta) < 1e-12) break;
    }
    const double sineo1 = std::sin(eo1);
    const double coseo1 = std::cos(eo1);
    const double el2 = e * e;
    const double pl = a * (1.0 - el2);
    const double r = a * (1.0 - e * coseo1);
    const double rdot = kXke * std::sqrt(a) * e * sineo1 / r;
    const double rfdot = kXke * std::sqrt(pl) / r;
    const double betal = std::sqrt(1.0 - el2);
    const double cosu = (coseo1 - e) / (1.0 - e * coseo1);
    const double sinu = betal * sineo1 / (1.0 - e * coseo1);
    const double uang = std::atan2(sinu, cosu);
    const double sin2u = 2.0 * sinu * cosu;
    const double cos2u = 2.0 * cosu * cosu - 1.0;
    const double rk = r * (1.0 - 1.5 * kCk2 * std::sqrt(1.0 - el2) * (3.0 * cosio * cosio - 1.0) / (pl * pl)) +
                      0.5 * kCk2 * x1mth2 * cos2u / pl;
    const double uk = uang - 0.25 * kCk2 * (7.0 * cosio * cosio - 1.0) * sin2u / pl;
    const double xnodek = xnode + 1.5 * kCk2 * cosio * sin2u / pl;
    const double xinck = el.inclo + 1.5 * kCk2 * cosio * sinio * cos2u / pl;
    const double rdotk = rdot - xn * kCk2 * x1mth2 * sin2u / pl;
    const double rfdotk = rfdot + xn * kCk2 * ((1.0 - cosio * cosio) * cos2u + 1.5 * (1.0 - 3.0 * theta2)) / pl;

    const double sinuk = std::sin(uk);
    const double cosuk = std::cos(uk);
    const double sinik = std::sin(xinck);
    const double cosik = std::cos(xinck);
    const double sinnok = std::sin(xnodek);
    const double cosnok = std::cos(xnodek);
    const double xmx = -sinnok * cosik;
    const double xmy = cosnok * cosik;
    const double ux = xmx * sinuk + cosnok * cosuk;
    const double uy = xmy * sinuk + sinnok * cosuk;
    const double uz = sinik * sinuk;
    const double vx = xmx * cosuk - cosnok * sinuk;
    const double vy = xmy * cosuk - sinnok * sinuk;
    const double vz = sinik * cosuk;

    st.r[0] = rk * ux * kXkmper;
    st.r[1] = rk * uy * kXkmper;
    st.r[2] = rk * uz * kXkmper;
    const double vkmps = kXkmper / 60.0; // er/min → km/s
    st.v[0] = (rdotk * ux + rfdotk * vx) * vkmps;
    st.v[1] = (rdotk * uy + rfdotk * vy) * vkmps;
    st.v[2] = (rdotk * uz + rfdotk * vz) * vkmps;
    st.ok = std::isfinite(st.r[0]) && std::isfinite(st.v[0]);
    return st;
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
