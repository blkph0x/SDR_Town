#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Mode-S / ADS-B DF17 helpers (public cooperative broadcast).
namespace ModeS {

bool crcOk(const uint8_t* msg, int lenBytes); // 7 or 14
uint32_t icaoAddress(const uint8_t* msg);     // bits from DF11/17/18
int downlinkFormat(const uint8_t* msg);

struct AdsbPosition {
    bool valid = false;
    double latDeg = 0.0;
    double lonDeg = 0.0;
};

struct AdsbVelocity {
    bool valid = false;
    double groundSpeedKt = 0.0;
    double trackDeg = 0.0;
    double verticalRateFpm = 0.0;
};

struct AdsbIdentity {
    bool valid = false;
    std::string callsign;
};

// Decode CPR even/odd pair into lat/lon (globally unambiguous when both recent).
// evenIsNewer: true to publish the even-frame latitude (even received later).
AdsbPosition decodeCprPair(bool evenIsNewer, int latCprEven, int lonCprEven, bool oddPresent, int latCprOdd, int lonCprOdd,
                           int nlHint = -1);
AdsbPosition decodeCprLocal(bool isOdd, int latCpr, int lonCpr, double refLat, double refLon);

AdsbVelocity decodeVelocity(const uint8_t* msg14);
AdsbIdentity decodeIdentity(const uint8_t* msg14);
int typeCode(const uint8_t* msg14);

// Extract 112-bit Mode-S frames from magnitude samples @ sampleRateHz near 1090 PPM.
// Returns packed 14-byte messages (DF17/18 when found).
std::vector<std::vector<uint8_t>> extractFramesFromMagnitude(const float* mag, size_t n,
                                                             double sampleRateHz);

} // namespace ModeS
