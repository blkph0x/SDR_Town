#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

// Short FECFRAME, nominal rate 1/2: 7032 BCH information bits, 7200 BCH
// bits, 16200 LDPC bits. Other rates are not implemented. This does not
// decrypt a scrambled transport stream.

bool encodeDvbs2ShortBch(const std::vector<int>& message, std::vector<int>& coded);
bool decodeDvbs2ShortBch(std::vector<int> coded, std::vector<int>& message);

bool encodeDvbs2ShortHalf(const std::vector<int>& message, std::vector<int>& codeword);

struct ShortHalfFec {
    bool ldpcConverged = false;
    bool bchOk = false;
    int iterations = 0;
    std::vector<int> messageBits;
    std::string note;
};

// Hard bits, 0 or 1, length 16200. Channel confidence is fixed.
ShortHalfFec decodeDvbs2ShortHalf(const std::vector<int>& hardBits);

// One sample per symbol. Short QPSK 1/2, no pilots: header, then 8100
// payload symbols. A constant frequency and phase are removed. Pilots,
// other rates, and raw oversampled IQ are not handled.
ShortHalfFec demodDvbs2ShortHalfFrame(const std::complex<float>* symbols, std::size_t count);
