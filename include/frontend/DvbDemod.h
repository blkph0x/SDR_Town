#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ETSI EN 302 307-1 PL header. MODCOD stays unknown until LDPC demod exists.
struct PlHeaderHit {
    bool found = false;
    int sofErrors = 26;
    std::size_t symbolIndex = 0;
    int modcod = -1;
    std::string note;
};

// 26 SOF symbols, π/2 BPSK, differential. Used by the detector test.
std::vector<std::complex<float>> modulateDvbs2Sof();

// Searches symbols for the 26-bit SOF. found requires 4 or fewer bit errors.
PlHeaderHit detectDvbs2PlHeader(const std::complex<float>* symbols, std::size_t count);

// Writes a clear MPEG-TS for the OS player. Scrambled packets are refused
// and are not written. This does not decrypt.
struct ClearTsPlayback {
    bool accepted = false;
    std::string reject;
};

ClearTsPlayback writeClearTsForPlayback(const std::uint8_t* data, std::size_t size, const std::string& path);
