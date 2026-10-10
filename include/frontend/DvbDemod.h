#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ETSI EN 302 307-1 PLHEADER. Payload LDPC is not decoded here.
struct PlHeaderHit {
    bool found = false;
    int sofErrors = 26;
    std::size_t symbolIndex = 0;
    bool plsDecoded = false;
    int plsErrors = -1;
    int modcod = -1;
    bool shortFrame = false;
    bool pilots = false;
    std::string note;
};

// 26 SOF symbols, absolute π/2 BPSK.
std::vector<std::complex<float>> modulateDvbs2Sof();

// 90 PLHEADER symbols for MODCOD 0..31. Empty when MODCOD is outside that range.
// shortFrame is the TYPE MSB. pilots is the TYPE LSB.
std::vector<std::complex<float>> modulateDvbs2PlHeader(int modcod, bool shortFrame, bool pilots);

// SOF plus the (64,7) PLS code when 90 symbols follow the alignment.
// One constant phase is estimated from the SOF and removed. A frequency
// offset across the header is not tracked. LDPC payload is not decoded.
PlHeaderHit detectDvbs2PlHeader(const std::complex<float>* symbols, std::size_t count);

// Writes a clear MPEG-TS for the OS player. Scrambled packets are refused
// and are not written. This does not decrypt.
struct ClearTsPlayback {
    bool accepted = false;
    std::string reject;
};

ClearTsPlayback writeClearTsForPlayback(const std::uint8_t* data, std::size_t size, const std::string& path);
