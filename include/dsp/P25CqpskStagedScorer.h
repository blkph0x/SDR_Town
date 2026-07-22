#pragma once

#include "dsp/P25DspTypes.h"

#include <array>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace p25dsp {

struct P25Phase2SyncPreview {
    int bestSyncErrors = 999;
    int bestInvertedErrors = 999;
    size_t bestOffsetDibits = 0;
    bool inverted = false;
    bool plausible = false;
};

// Cheap staged candidate gate before full protocol decode.
// Uses the same 40-bit sync word as P25LiveDecoder::Phase2FrameSyncWord.
P25Phase2SyncPreview scorePhase2SyncPreview(std::span<const int> dibits,
                                            bool synchronized) noexcept;

struct P25CqpskMappingParams {
    bool differential = true;
    bool conjugate = false;
    double rotationRad = 0.0;
    std::array<int, 4> permutation{0, 1, 2, 3};
};

// Map precomputed differential quadrants (0..3) to dibits using LUT logic.
// Equivalent to one pass of cqpskSymbolsToSoftDibits without re-running timing.
std::vector<int> mapQuadrantsToDibits(std::span<const uint8_t> quadrants,
                                      const P25CqpskMappingParams& mapping) noexcept;

// Classify complex differential symbol to QPSK quadrant 0..3.
uint8_t classifyDqpskQuadrant(float di, float dq) noexcept;

// Build differential quadrants from complex symbol stream once per timing phase.
std::vector<uint8_t> buildDifferentialQuadrants(std::span<const std::complex<double>> symbols,
                                                  bool differential,
                                                  bool conjugate,
                                                  double rotationRad) noexcept;

bool passesStagedCqpskGate(const P25Phase2SyncPreview& preview,
                           P25DemodState state,
                           bool fromLock) noexcept;

} // namespace p25dsp
