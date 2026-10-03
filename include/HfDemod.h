#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

enum class DemodMode;
struct FmMultiplexBlock;

namespace HfDemod {

// Read-only, per-owner evidence. No log/file IO on the DSP thread.
struct Diagnostics {
    uint64_t blocks = 0, inputSamples = 0, resets = 0, rejectedBlocks = 0;
    uint64_t continuousCorrections = 0;
    double effectiveBandwidthHz = 0, effectiveLowPassHz = 0;
    uint32_t lastResetReasons = 0;
};
enum ResetReason : uint32_t { Start = 1, Explicit = 2, Configuration = 4, Identity = 8, InvalidInput = 16 };
Diagnostics diagnostics(const void* owner);

// Returns true only for the analogue HF modes owned by this isolated path.
bool supports(DemodMode mode) noexcept;

// Stateful HF receiver for AM, USB, LSB and CW. The owner is normally the
// Demodulator instance; state is isolated per owner and removed by release().
// Output length follows the persistent input/output clock; the legacy
// targetAudioSamples hint is ignored rather than stretching individual blocks.
std::vector<float> demodulate(
    const void* owner,
    const std::vector<std::complex<float>>& iq,
    double inputRateHz,
    double centerHz,
    double targetHz,
    DemodMode mode,
    double& rmsOutDb,
    double audioLowPassHz = 0.0,
    double squelchDb = -105.0,
    double audioGain = 1.0,
    double channelBandwidthHz = 0.0,
    size_t targetAudioSamples = 0,
    double outputRateHz = 48000.0,
    double externalSquelchLevelDb = std::numeric_limits<double>::quiet_NaN(),
    bool audioLowPassEnabled = true,
    FmMultiplexBlock* decoderAudio = nullptr,
    double dataIdentityHz = std::numeric_limits<double>::quiet_NaN());

void reset(const void* owner) noexcept;
void release(const void* owner) noexcept;

} // namespace HfDemod
