#pragma once

#include <complex>
#include <cstddef>
#include <limits>
#include <vector>

enum class DemodMode;
struct FmMultiplexBlock;

namespace HfDemod {

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
