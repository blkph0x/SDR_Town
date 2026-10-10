#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

// QPSK FECFRAME, short (16200) or normal (64800). Short rate 9/10 does not
// exist. 8PSK and APSK are not implemented. This does not decrypt a
// scrambled transport stream.

bool dvbs2QpskInfoBits(bool normalFrame, int modcod, int& messageBits, int& codewordBits);
bool encodeDvbs2Qpsk(bool normalFrame, int modcod,
    const std::vector<int>& message, std::vector<int>& codeword);

struct ShortHalfFec {
    bool ldpcConverged = false;
    bool bchOk = false;
    int iterations = 0;
    std::vector<int> messageBits;
    std::string note;
};

ShortHalfFec decodeDvbs2Qpsk(bool normalFrame, int modcod, const std::vector<int>& hardBits);

// One sample per symbol. QPSK, no pilots: header, then 8100 short or
// 32400 normal payload symbols. A constant frequency and phase are removed.
// Pilots and raw oversampled IQ are not handled.
ShortHalfFec demodDvbs2QpskFrame(const std::complex<float>* symbols, std::size_t count);

bool encodeDvbs2ShortBch(const std::vector<int>& message, std::vector<int>& coded);
bool decodeDvbs2ShortBch(std::vector<int> coded, std::vector<int>& message);
bool encodeDvbs2ShortHalf(const std::vector<int>& message, std::vector<int>& codeword);
ShortHalfFec decodeDvbs2ShortHalf(const std::vector<int>& hardBits);
ShortHalfFec demodDvbs2ShortHalfFrame(const std::complex<float>* symbols, std::size_t count);
