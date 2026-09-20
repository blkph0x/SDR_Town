#include "ModeS.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ModeS {
namespace {

const uint32_t kCrcPoly = 0xFFFA0480u;

uint32_t modesChecksum(const uint8_t* msg, int bits) {
    uint32_t crc = 0;
    for (int i = 0; i < bits; ++i) {
        const int byte = i / 8;
        const int bit = 7 - (i % 8);
        const uint32_t bitVal = (msg[byte] >> bit) & 1u;
        crc ^= bitVal << 31;
        if (crc & 0x80000000u) crc = (crc << 1) ^ kCrcPoly;
        else crc <<= 1;
    }
    return (crc >> 8) & 0xFFFFFFu;
}

int cprNL(double lat) {
    if (lat < 0) lat = -lat;
    if (lat > 87.0) return 1;
    static const double table[] = {
        0, 10.47047130, 14.82817437, 18.18638199, 21.02939493, 23.54504487, 25.82924707, 27.93898710,
        29.91135686, 31.77251106, 33.53993436, 35.22889912, 36.85025108, 38.41241892, 39.92256684,
        41.38651832, 42.80914012, 44.19454951, 45.54526722, 46.86733252, 48.16039128, 49.42776439,
        50.67150166, 51.89342469, 53.09516153, 54.27817472, 55.44378444, 56.59318756, 57.72747354,
        58.84763776, 59.95459277, 61.04917774, 62.13216659, 63.20427479, 64.26607639, 65.31810694,
        66.36092021, 67.39503168, 68.42097148, 69.43926636, 70.45043708, 71.45499422, 72.45343686,
        73.44624619, 74.43389430, 75.41686320, 76.39564886, 77.37075091, 78.34249029, 79.31116878,
        80.27731796, 81.24110044, 82.20299191, 83.16372040, 84.12415318, 85.08427098, 86.04414783,
        87.0};
    for (int i = 1; i < 60; ++i) {
        if (lat < table[i]) return 60 - i;
    }
    return 1;
}

int cprN(double lat, bool isOdd) {
    int nl = cprNL(lat) - (isOdd ? 1 : 0);
    if (nl < 1) nl = 1;
    return nl;
}

double cprDlon(double lat, bool isOdd) {
    return 360.0 / cprN(lat, isOdd);
}

int cprModInt(int a, int b) {
    if (b <= 0) return 0;
    int res = a % b;
    if (res < 0) res += b;
    return res;
}

double cprModD(double a, double b) {
    if (b == 0.0) return 0.0;
    double res = std::fmod(a, b);
    if (res < 0.0) res += b;
    return res;
}

} // namespace

bool crcOk(const uint8_t* msg, int lenBytes) {
    if (!msg || (lenBytes != 7 && lenBytes != 14)) return false;
    const int bits = lenBytes * 8;
    const uint32_t crc = modesChecksum(msg, bits - 24);
    const uint32_t got = (uint32_t(msg[lenBytes - 3]) << 16) | (uint32_t(msg[lenBytes - 2]) << 8) |
                         uint32_t(msg[lenBytes - 1]);
    return crc == got;
}

uint32_t icaoAddress(const uint8_t* msg) {
    if (!msg) return 0;
    return (uint32_t(msg[1]) << 16) | (uint32_t(msg[2]) << 8) | uint32_t(msg[3]);
}

int downlinkFormat(const uint8_t* msg) {
    return msg ? (msg[0] >> 3) : -1;
}

int typeCode(const uint8_t* msg14) {
    return msg14 ? ((msg14[4] >> 3) & 0x1F) : -1;
}

AdsbPosition decodeCprPair(bool evenIsNewer, int latCprEven, int lonCprEven, bool oddPresent, int latCprOdd,
                           int lonCprOdd, int /*nlHint*/) {
    AdsbPosition out;
    if (!oddPresent) return out;
    const double AirDlat0 = 360.0 / 60.0;
    const double AirDlat1 = 360.0 / 59.0;
    double j = std::floor((59.0 * latCprEven - 60.0 * latCprOdd) / 131072.0 + 0.5);
    double lat0 = AirDlat0 * (cprModInt(static_cast<int>(std::floor(j)), 60) + latCprEven / 131072.0);
    double lat1 = AirDlat1 * (cprModInt(static_cast<int>(std::floor(j)), 59) + latCprOdd / 131072.0);
    if (lat0 >= 270.0) lat0 -= 360.0;
    if (lat1 >= 270.0) lat1 -= 360.0;
    if (cprNL(lat0) != cprNL(lat1)) return out;
    const double lat = evenIsNewer ? lat0 : lat1;
    const int nl = cprNL(lat);
    if (nl <= 0) return out;
    double m = std::floor((lonCprEven * (nl - 1.0) - lonCprOdd * nl) / 131072.0 + 0.5);
    const int ni = std::max(1, nl - 0);
    double lon = cprDlon(lat, false) * (cprModD(m, static_cast<double>(ni)) + lonCprEven / 131072.0);
    if (lon >= 180.0) lon -= 360.0;
    out.valid = std::isfinite(lat) && std::isfinite(lon);
    out.latDeg = lat;
    out.lonDeg = lon;
    return out;
}

AdsbPosition decodeCprLocal(bool isOdd, int latCpr, int lonCpr, double refLat, double refLon) {
    AdsbPosition out;
    const double dlat = isOdd ? (360.0 / 59.0) : (360.0 / 60.0);
    const double j = std::floor(refLat / dlat) +
                     std::floor(0.5 + cprModD(refLat, dlat) / dlat - (latCpr / 131072.0));
    double lat = dlat * (j + latCpr / 131072.0);
    const double dlon = cprDlon(lat, isOdd);
    const double m = std::floor(refLon / dlon) +
                     std::floor(0.5 + cprModD(refLon + 180.0, dlon) / dlon - (lonCpr / 131072.0));
    double lon = dlon * (m + lonCpr / 131072.0);
    if (lon >= 180.0) lon -= 360.0;
    out.valid = std::isfinite(lat) && std::isfinite(lon);
    out.latDeg = lat;
    out.lonDeg = lon;
    return out;
}

AdsbVelocity decodeVelocity(const uint8_t* msg14) {
    AdsbVelocity v;
    if (!msg14) return v;
    const int tc = typeCode(msg14);
    if (tc != 19) return v;
    const int subtype = msg14[4] & 0x07;
    if (subtype != 1 && subtype != 2) return v;
    const int ewDir = (msg14[5] >> 2) & 1;
    const int ewVel = ((msg14[5] & 3) << 8) | msg14[6];
    const int nsDir = (msg14[7] >> 7) & 1;
    const int nsVel = ((msg14[7] & 0x7F) << 3) | ((msg14[8] >> 5) & 7);
    if (ewVel == 0 || nsVel == 0) return v;
    double ew = ewVel - 1.0;
    double ns = nsVel - 1.0;
    if (ewDir) ew = -ew;
    if (nsDir) ns = -ns;
    v.groundSpeedKt = std::sqrt(ew * ew + ns * ns);
    v.trackDeg = std::fmod(std::atan2(ew, ns) * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
    const int vrSign = (msg14[8] >> 3) & 1;
    const int vr = ((msg14[8] & 7) << 6) | ((msg14[9] >> 2) & 0x3F);
    if (vr != 0) {
        v.verticalRateFpm = (vr - 1) * 64.0;
        if (vrSign) v.verticalRateFpm = -v.verticalRateFpm;
    }
    v.valid = true;
    return v;
}

AdsbIdentity decodeIdentity(const uint8_t* msg14) {
    AdsbIdentity id;
    if (!msg14) return id;
    const int tc = typeCode(msg14);
    if (tc < 1 || tc > 4) return id;
    static const char* lut = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ#####_###############0123456789######";
    char cs[9]{};
    const uint64_t chars = (uint64_t(msg14[5]) << 40) | (uint64_t(msg14[6]) << 32) |
                           (uint64_t(msg14[7]) << 24) | (uint64_t(msg14[8]) << 16) |
                           (uint64_t(msg14[9]) << 8) | uint64_t(msg14[10]);
    for (int i = 0; i < 8; ++i) {
        const int c = int((chars >> (42 - 6 * i)) & 0x3F);
        cs[i] = lut[c];
    }
    // trim
    std::string s(cs);
    while (!s.empty() && (s.back() == '_' || s.back() == ' ' || s.back() == '#')) s.pop_back();
    for (char& ch : s)
        if (ch == '_') ch = ' ';
    id.callsign = s;
    id.valid = !s.empty();
    return id;
}

std::vector<std::vector<uint8_t>> extractFramesFromMagnitude(const float* mag, size_t n,
                                                             double sampleRateHz) {
    std::vector<std::vector<uint8_t>> out;
    if (!mag || n < 1000 || sampleRateHz < 1.5e6) return out;
    const int spb = std::max(1, int(std::lround(sampleRateHz / 1e6))); // ~1 µs chips @ 2 Msps → 2
    // Preamble: 10100001010000 (µs) simplified peak search
    float thr = 0.0f;
    for (size_t i = 0; i < n; ++i) thr += mag[i];
    thr = (thr / float(n)) * 3.5f;

    for (size_t i = 0; i + size_t(spb) * 240 < n; ++i) {
        if (mag[i] < thr) continue;
        // Sample 112 bits after ~8 µs preamble offset
        const size_t data0 = i + size_t(spb) * 8;
        if (data0 + size_t(spb) * 224 >= n) break;
        uint8_t msg[14]{};
        bool okBits = true;
        for (int b = 0; b < 112; ++b) {
            const size_t a = data0 + size_t(b * spb);
            const size_t c = a + size_t(spb / 2);
            if (c >= n) {
                okBits = false;
                break;
            }
            const bool bit = mag[a] > mag[c];
            if (bit) msg[b / 8] |= uint8_t(1u << (7 - (b % 8)));
        }
        if (!okBits) continue;
        const int df = downlinkFormat(msg);
        if (df != 17 && df != 18) continue;
        if (!crcOk(msg, 14)) continue;
        out.emplace_back(msg, msg + 14);
        i += size_t(spb) * 120; // skip ahead
        if (out.size() > 64) break;
    }
    return out;
}

} // namespace ModeS
