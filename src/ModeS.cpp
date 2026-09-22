#include "ModeS.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ModeS {
namespace {

constexpr uint32_t kCrcPoly = 0xFFF409u;
constexpr double kCprScale = 131072.0;
constexpr double kMicrosecond = 1.0e-6;

uint32_t modesRemainder(const uint8_t* msg, int bits) {
    if (!msg || bits <= 0) return 0xFFFFFFu;
    uint32_t remainder = 0;
    for (int i = 0; i < bits; ++i) {
        const int byte = i / 8;
        const int bit = 7 - (i % 8);
        const uint32_t incoming = (msg[byte] >> bit) & 1u;
        const bool top = (remainder & 0x800000u) != 0;
        remainder = ((remainder << 1) & 0xFFFFFFu) | incoming;
        if (top) remainder ^= kCrcPoly;
    }
    return remainder & 0xFFFFFFu;
}

int cprNL(double lat) {
    lat = std::abs(lat);
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
    return std::max(1, cprNL(lat) - (isOdd ? 1 : 0));
}

double cprDlon(double lat, bool isOdd) {
    return 360.0 / static_cast<double>(cprN(lat, isOdd));
}

int cprModInt(int a, int b) {
    if (b <= 0) return 0;
    int result = a % b;
    if (result < 0) result += b;
    return result;
}

double cprModD(double a, double b) {
    if (b == 0.0) return 0.0;
    double result = std::fmod(a, b);
    if (result < 0.0) result += b;
    return result;
}

bool validCprValue(int value) {
    return value >= 0 && value < static_cast<int>(kCprScale);
}

float interpolatedMagnitude(const float* mag, size_t count, double sampleIndex) {
    if (!mag || count == 0 || !std::isfinite(sampleIndex) || sampleIndex < 0.0 ||
        sampleIndex > static_cast<double>(count - 1)) {
        return 0.0f;
    }
    const size_t left = static_cast<size_t>(std::floor(sampleIndex));
    const size_t right = std::min(count - 1, left + 1);
    const double fraction = sampleIndex - static_cast<double>(left);
    const double a = std::isfinite(mag[left]) ? std::max(0.0f, mag[left]) : 0.0f;
    const double b = std::isfinite(mag[right]) ? std::max(0.0f, mag[right]) : 0.0f;
    return static_cast<float>(a + (b - a) * fraction);
}

double windowMean(const float* mag, size_t count, double startSample,
                  double sampleRateHz, double beginUs, double endUs) {
    if (endUs <= beginUs || sampleRateHz <= 0.0) return 0.0;
    const double begin = startSample + beginUs * kMicrosecond * sampleRateHz;
    const double end = startSample + endUs * kMicrosecond * sampleRateHz;
    const int points = std::clamp(
        static_cast<int>(std::ceil(std::max(1.0, (end - begin) * 2.0))), 3, 20);
    double sum = 0.0;
    for (int i = 0; i < points; ++i) {
        const double fraction = (static_cast<double>(i) + 0.5) / static_cast<double>(points);
        sum += interpolatedMagnitude(mag, count, begin + (end - begin) * fraction);
    }
    return sum / static_cast<double>(points);
}

double windowStdDev(const float* mag, size_t count, double startSample,
                    double sampleRateHz, double beginUs, double endUs) {
    if (endUs <= beginUs || sampleRateHz <= 0.0) return 0.0;
    const double begin = startSample + beginUs * kMicrosecond * sampleRateHz;
    const double end = startSample + endUs * kMicrosecond * sampleRateHz;
    constexpr int points = 16;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int i = 0; i < points; ++i) {
        const double fraction = (static_cast<double>(i) + 0.5) / static_cast<double>(points);
        const double value = interpolatedMagnitude(mag, count, begin + (end - begin) * fraction);
        sum += value;
        sumSquares += value * value;
    }
    const double mean = sum / points;
    return std::sqrt(std::max(0.0, sumSquares / points - mean * mean));
}

struct PreambleQuality {
    bool valid = false;
    double highMean = 0.0;
    double lowMean = 0.0;
    double contrast = 0.0;
};

PreambleQuality evaluatePreamble(const float* mag, size_t count, double startSample,
                                 double sampleRateHz) {
    // Mode-S preamble pulses occupy 0-0.5, 1.0-1.5, 3.5-4.0 and
    // 4.5-5.0 microseconds. Evaluate narrow interiors to avoid edge ambiguity.
    constexpr std::array<std::array<double, 2>, 4> highs{{
        {{0.06, 0.44}}, {{1.06, 1.44}}, {{3.56, 3.94}}, {{4.56, 4.94}}
    }};
    constexpr std::array<std::array<double, 2>, 5> lows{{
        {{0.56, 0.94}}, {{1.65, 2.25}}, {{2.55, 3.25}},
        {{4.06, 4.44}}, {{5.25, 7.75}}
    }};

    double highSum = 0.0;
    double lowSum = 0.0;
    double highMin = std::numeric_limits<double>::infinity();
    double lowMax = 0.0;
    for (const auto& window : highs) {
        const double value = windowMean(mag, count, startSample, sampleRateHz,
                                        window[0], window[1]);
        highSum += value;
        highMin = std::min(highMin, value);
    }
    for (const auto& window : lows) {
        const double value = windowMean(mag, count, startSample, sampleRateHz,
                                        window[0], window[1]);
        lowSum += value;
        lowMax = std::max(lowMax, value);
    }

    PreambleQuality quality;
    quality.highMean = highSum / static_cast<double>(highs.size());
    quality.lowMean = lowSum / static_cast<double>(lows.size());
    quality.contrast = quality.highMean - quality.lowMean;

    const double noiseStd = startSample >= sampleRateHz * 6.0 * kMicrosecond
        ? windowStdDev(mag, count, startSample, sampleRateHz, -6.0, -0.25)
        : windowStdDev(mag, count, startSample, sampleRateHz, 5.25, 7.75);
    const double minimumContrast = std::max({1.0e-5, noiseStd * 3.0,
                                             quality.lowMean * 0.45});
    quality.valid = quality.contrast > minimumContrast &&
                    quality.highMean > quality.lowMean * 1.55 + 1.0e-6 &&
                    highMin > quality.lowMean + quality.contrast * 0.35 &&
                    lowMax < quality.lowMean + quality.contrast * 0.75;
    return quality;
}

bool decodeCandidate(const float* mag, size_t count, double startSample,
                     double sampleRateHz, const PreambleQuality& preamble,
                     std::array<uint8_t, 14>& message) {
    message.fill(0);
    int weakBits = 0;
    double confidenceSum = 0.0;
    double pulseSum = 0.0;

    for (int bit = 0; bit < 112; ++bit) {
        const double bitStartUs = 8.0 + static_cast<double>(bit);
        const double first = windowMean(mag, count, startSample, sampleRateHz,
                                        bitStartUs + 0.07, bitStartUs + 0.43);
        const double second = windowMean(mag, count, startSample, sampleRateHz,
                                         bitStartUs + 0.57, bitStartUs + 0.93);
        const double strongest = std::max(first, second);
        const double delta = std::abs(first - second);
        const double confidence = delta / std::max(1.0e-9, strongest);
        confidenceSum += confidence;
        pulseSum += strongest;
        if (confidence < 0.10 ||
            strongest < preamble.lowMean + preamble.contrast * 0.20) {
            ++weakBits;
        }
        if (first > second)
            message[static_cast<size_t>(bit / 8)] |=
                static_cast<uint8_t>(1u << (7 - (bit % 8)));
    }

    const double meanConfidence = confidenceSum / 112.0;
    const double meanPulse = pulseSum / 112.0;
    return weakBits <= 28 && meanConfidence >= 0.16 &&
           meanPulse >= preamble.lowMean + preamble.contrast * 0.25;
}

} // namespace

bool crcOk(const uint8_t* msg, int lenBytes) {
    if (!msg || (lenBytes != 7 && lenBytes != 14)) return false;
    return modesRemainder(msg, lenBytes * 8) == 0;
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

AdsbPosition decodeCprPair(bool evenIsNewer, int latCprEven, int lonCprEven,
                           bool oddPresent, int latCprOdd, int lonCprOdd,
                           int /*nlHint*/) {
    AdsbPosition out;
    if (!oddPresent || !validCprValue(latCprEven) || !validCprValue(lonCprEven) ||
        !validCprValue(latCprOdd) || !validCprValue(lonCprOdd)) {
        return out;
    }

    const double dLatEven = 360.0 / 60.0;
    const double dLatOdd = 360.0 / 59.0;
    const double j = std::floor(
        (59.0 * latCprEven - 60.0 * latCprOdd) / kCprScale + 0.5);
    double latEven = dLatEven *
        (cprModInt(static_cast<int>(j), 60) + latCprEven / kCprScale);
    double latOdd = dLatOdd *
        (cprModInt(static_cast<int>(j), 59) + latCprOdd / kCprScale);
    if (latEven >= 270.0) latEven -= 360.0;
    if (latOdd >= 270.0) latOdd -= 360.0;
    if (cprNL(latEven) != cprNL(latOdd)) return out;

    const double latitude = evenIsNewer ? latEven : latOdd;
    const int nl = cprNL(latitude);
    const double m = std::floor(
        (lonCprEven * (nl - 1.0) - lonCprOdd * nl) / kCprScale + 0.5);
    const bool useOdd = !evenIsNewer;
    const int ni = std::max(1, nl - (useOdd ? 1 : 0));
    const int lonCpr = useOdd ? lonCprOdd : lonCprEven;
    double longitude = (360.0 / static_cast<double>(ni)) *
        (cprModD(m, static_cast<double>(ni)) + lonCpr / kCprScale);
    if (longitude >= 180.0) longitude -= 360.0;

    out.valid = std::isfinite(latitude) && std::isfinite(longitude) &&
                latitude >= -90.0 && latitude <= 90.0 &&
                longitude >= -180.0 && longitude <= 180.0;
    if (out.valid) {
        out.latDeg = latitude;
        out.lonDeg = longitude;
    }
    return out;
}

AdsbPosition decodeCprLocal(bool isOdd, int latCpr, int lonCpr,
                            double refLat, double refLon) {
    AdsbPosition out;
    if (!validCprValue(latCpr) || !validCprValue(lonCpr) ||
        !std::isfinite(refLat) || !std::isfinite(refLon) ||
        refLat < -90.0 || refLat > 90.0) {
        return out;
    }

    const double dLat = isOdd ? (360.0 / 59.0) : (360.0 / 60.0);
    const double j = std::floor(refLat / dLat) +
        std::floor(0.5 + cprModD(refLat, dLat) / dLat - latCpr / kCprScale);
    double latitude = dLat * (j + latCpr / kCprScale);
    if (latitude >= 270.0) latitude -= 360.0;
    if (latitude < -90.0 || latitude > 90.0) return out;

    const double dLon = cprDlon(latitude, isOdd);
    const double m = std::floor(refLon / dLon) +
        std::floor(0.5 + cprModD(refLon, dLon) / dLon - lonCpr / kCprScale);
    double longitude = dLon * (m + lonCpr / kCprScale);
    while (longitude >= 180.0) longitude -= 360.0;
    while (longitude < -180.0) longitude += 360.0;

    out.valid = std::isfinite(latitude) && std::isfinite(longitude);
    if (out.valid) {
        out.latDeg = latitude;
        out.lonDeg = longitude;
    }
    return out;
}

AdsbVelocity decodeVelocity(const uint8_t* msg14) {
    AdsbVelocity velocity;
    if (!msg14 || typeCode(msg14) != 19) return velocity;
    const int subtype = msg14[4] & 0x07;
    if (subtype != 1 && subtype != 2) return velocity;

    const int ewDirection = (msg14[5] >> 2) & 1;
    const int ewEncoded = ((msg14[5] & 3) << 8) | msg14[6];
    const int nsDirection = (msg14[7] >> 7) & 1;
    const int nsEncoded = ((msg14[7] & 0x7F) << 3) | ((msg14[8] >> 5) & 7);
    if (ewEncoded == 0 || nsEncoded == 0) return velocity;

    const double scale = subtype == 2 ? 4.0 : 1.0;
    double eastWest = (ewEncoded - 1.0) * scale;
    double northSouth = (nsEncoded - 1.0) * scale;
    if (ewDirection) eastWest = -eastWest;
    if (nsDirection) northSouth = -northSouth;
    velocity.groundSpeedKt = std::hypot(eastWest, northSouth);
    velocity.trackDeg = std::fmod(
        std::atan2(eastWest, northSouth) * 180.0 / std::numbers::pi_v<double> + 360.0,
        360.0);

    const int verticalRateSign = (msg14[8] >> 3) & 1;
    const int verticalRateEncoded = ((msg14[8] & 7) << 6) | ((msg14[9] >> 2) & 0x3F);
    if (verticalRateEncoded != 0) {
        velocity.verticalRateFpm = (verticalRateEncoded - 1) * 64.0;
        if (verticalRateSign) velocity.verticalRateFpm = -velocity.verticalRateFpm;
    }
    velocity.valid = true;
    return velocity;
}

AdsbIdentity decodeIdentity(const uint8_t* msg14) {
    AdsbIdentity identity;
    if (!msg14) return identity;
    const int tc = typeCode(msg14);
    if (tc < 1 || tc > 4) return identity;
    static const char* lut = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ#####_###############0123456789######";
    char callsign[9]{};
    const uint64_t chars = (uint64_t(msg14[5]) << 40) | (uint64_t(msg14[6]) << 32) |
                           (uint64_t(msg14[7]) << 24) | (uint64_t(msg14[8]) << 16) |
                           (uint64_t(msg14[9]) << 8) | uint64_t(msg14[10]);
    for (int i = 0; i < 8; ++i) {
        const int code = int((chars >> (42 - 6 * i)) & 0x3F);
        callsign[i] = lut[code];
    }
    std::string text(callsign);
    while (!text.empty() && (text.back() == '_' || text.back() == ' ' || text.back() == '#'))
        text.pop_back();
    for (char& ch : text)
        if (ch == '_') ch = ' ';
    identity.callsign = text;
    identity.valid = !text.empty();
    return identity;
}

std::vector<std::vector<uint8_t>> extractFramesFromMagnitude(
    const float* mag, size_t count, double sampleRateHz) {
    std::vector<std::vector<uint8_t>> frames;
    if (!mag || !std::isfinite(sampleRateHz) || sampleRateHz < 1.5e6) return frames;

    const size_t requiredSamples = static_cast<size_t>(
        std::ceil(sampleRateHz * 120.0 * kMicrosecond)) + 3;
    if (count < requiredSamples) return frames;

    constexpr int phaseHypotheses = 4;
    for (size_t integerStart = 0; integerStart + requiredSamples < count; ++integerStart) {
        bool accepted = false;
        for (int phase = 0; phase < phaseHypotheses; ++phase) {
            const double startSample = static_cast<double>(integerStart) +
                                       static_cast<double>(phase) / phaseHypotheses;
            if (startSample + sampleRateHz * 120.0 * kMicrosecond >= count - 1)
                break;

            const PreambleQuality preamble = evaluatePreamble(
                mag, count, startSample, sampleRateHz);
            if (!preamble.valid) continue;

            std::array<uint8_t, 14> message{};
            if (!decodeCandidate(mag, count, startSample, sampleRateHz,
                                 preamble, message)) {
                continue;
            }
            const int df = downlinkFormat(message.data());
            if ((df != 17 && df != 18) || !crcOk(message.data(), 14)) continue;

            const std::vector<uint8_t> frame(message.begin(), message.end());
            if (std::find(frames.begin(), frames.end(), frame) == frames.end())
                frames.push_back(frame);
            accepted = true;
            break;
        }

        if (accepted) {
            const size_t skip = static_cast<size_t>(
                std::max(1.0, std::floor(sampleRateHz * 119.0 * kMicrosecond)));
            integerStart = std::min(count - requiredSamples, integerStart + skip);
            if (frames.size() >= 64) break;
        }
    }
    return frames;
}

} // namespace ModeS
