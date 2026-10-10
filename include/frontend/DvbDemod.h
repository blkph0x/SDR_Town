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
// The SOF starts the frequency estimate. After PLS decodes, the 90 known
// header symbols refit one frequency and one phase. This is not a
// second-order payload loop. LDPC is not decoded here.
PlHeaderHit detectDvbs2PlHeader(const std::complex<float>* symbols, std::size_t count);

// Symbols after the 90-symbol header, with the header frequency and phase removed.
// They are still physical-layer scrambled. Pilot symbols are still in this span.
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

// MODCOD 1..28. rateModcod is the FEC rate index 1..11 (1/4 .. 9/10).
// Short rate 9/10 does not exist for any modulation.
struct Dvbs2Modcod {
    bool known = false;
    int bitsPerSymbol = 0;
    int rateModcod = 0;
    bool readMsbLast = false;
};

Dvbs2Modcod dvbs2Modcod(int modcod);

// Symbols after the PLHEADER, including pilot symbols when pilots is set.
int dvbs2PayloadSymbols(int modcod, bool shortFrame, bool pilots);

// FEC bits to XFECFRAME symbols. Pilots, when requested, are the unmodulated
// carrier and consume the following PL Gold index. The result is not scrambled.
std::vector<std::complex<float>> modulateDvbs2Data(const int* fecBits, std::size_t bitCount,
    int modcod, bool shortFrame, bool pilots);

// Descramble Gold n=0, drop pilot symbols, undo the bit interleaver, and
// hard-slice. A pilot block sets the phase used on the following slots.
// These bits are pre-FEC.
struct Dvbs2Slice {
    bool sliced = false;
    std::vector<int> fecBits;
    std::string note;
};

Dvbs2Slice sliceDvbs2FecBits(const std::complex<float>* symbols, std::size_t count,
    int modcod, bool shortFrame, bool pilots);

// Root-raised-cosine samples to one sample per symbol. samplesPerSymbol is
// the integer nearest sampleRateHz/symbolRateHz and must be 2..8.
// rollOff is 0.20, 0.25, or 0.35. This is not a blind survey.
struct TimedSymbols {
    bool locked = false;
    int samplesPerSymbol = 0;
    std::vector<std::complex<float>> symbols;
    std::string note;
};

TimedSymbols recoverDvbs2Symbols(const std::complex<float>* samples, std::size_t count,
    double sampleRateHz, double symbolRateHz, double rollOff);

// Pulse shape used by the modulator and the matched filter.
std::vector<std::complex<float>> shapeDvbs2Rrc(const std::complex<float>* symbols, std::size_t count,
    int samplesPerSymbol, double rollOff);

// Cyclostationary peak of a shaped carrier. 0 when no rate is found.
double estimateDvbs2SymbolRateHz(const std::complex<float>* samples, std::size_t count, double sampleRateHz);

// Bytes of a user packet that continued from the previous baseband frame.
struct BbCarry {
    std::vector<std::uint8_t> tail;
};

// Same rules as extractClearTsFromBbFrame, keeping an unfinished packet for
// the next frame. Null-packet deletion reinserts empty PID 8191 packets.
// ISSY is rejected. Scrambled packets are omitted. This does not decrypt.
BbFrameTs appendClearTsFromBbFrame(BbCarry& carry, const std::uint8_t* data, std::size_t size);
