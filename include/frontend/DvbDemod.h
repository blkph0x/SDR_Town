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
    float frequencyRadPerSymbol = 0.0f;
    std::string note;
};

// 26 SOF symbols, absolute π/2 BPSK.
std::vector<std::complex<float>> modulateDvbs2Sof();

// 90 PLHEADER symbols for MODCOD 0..31. Empty when MODCOD is outside that range.
// shortFrame is the TYPE MSB. pilots is the TYPE LSB.
std::vector<std::complex<float>> modulateDvbs2PlHeader(int modcod, bool shortFrame, bool pilots);

// SOF plus the (64,7) PLS code when 90 symbols follow the alignment.
// One constant frequency, in radians per symbol, and one constant phase are
// estimated from the SOF and removed across the 90-symbol header. This is
// not a payload tracking loop. LDPC payload is not decoded.
PlHeaderHit detectDvbs2PlHeader(const std::complex<float>* symbols, std::size_t count);

// Symbols after the 90-symbol header, with the SOF frequency and phase removed.
// They are still physical-layer scrambled. Pilot symbols are not removed.
struct PlDataSymbols {
    PlHeaderHit header;
    std::vector<std::complex<float>> data;
};

PlDataSymbols extractDvbs2DataSymbols(const std::complex<float>* symbols, std::size_t count, std::size_t dataSymbols);

// ETSI BB scrambler, polynomial 1+X^14+X^15, load 100101010000000.
// The same function descrambles. This is not conditional-access decryption.
std::vector<std::uint8_t> scrambleBbFrame(const std::uint8_t* data, std::size_t size);

// Descrambles a BBFRAME, checks the BBHEADER CRC-8, and copies only
// unscrambled MPEG-TS packets whose user-packet CRC matches. Scrambled
// packets are counted and omitted. LDPC is not run; the caller supplies
// the baseband bytes.
struct BbFrameTs {
    bool headerCrcOk = false;
    int packets = 0;
    int scrambledPackets = 0;
    std::vector<std::uint8_t> clearTs;
    std::string reject;
};

BbFrameTs extractClearTsFromBbFrame(const std::uint8_t* data, std::size_t size);

// PL Gold code n=0, then Gray QPSK hard decisions. MODCOD 1..11 only.
// These bits are pre-FEC. LDPC and BCH are not applied.
struct QpskHardBits {
    bool sliced = false;
    std::vector<int> bits;
    std::string note;
};

QpskHardBits sliceQpskAfterPlDescramble(const std::complex<float>* symbols, std::size_t count, int modcod);

// Gray QPSK, I is the first bit, 00 at angle π/4. Even bit counts only.
std::vector<std::complex<float>> modulateQpsk(const int* bits, std::size_t bitCount);

// Multiply by the n=0 PL Gold code. Descrambling is the conjugate, inside the slicer.
std::vector<std::complex<float>> scramblePlSymbols(const std::complex<float>* symbols, std::size_t count);

// Writes a clear MPEG-TS for the OS player. Scrambled packets are refused
// and are not written. This does not decrypt.
struct ClearTsPlayback {
    bool accepted = false;
    std::string reject;
};

ClearTsPlayback writeClearTsForPlayback(const std::uint8_t* data, std::size_t size, const std::string& path);
