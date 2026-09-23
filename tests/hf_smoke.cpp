#include "Demod.h"
#include "HfDemod.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "HF smoke failure: " << message << '\n';
        std::exit(1);
    }
}

std::vector<std::complex<float>> tone(
    double rate, double seconds, double hz, float amplitude)
{
    const size_t count =
        static_cast<size_t>(std::llround(rate * seconds));
    std::vector<std::complex<float>> result(count);
    for (size_t index = 0; index < count; ++index) {
        const double phase =
            2.0 * kPi * hz * static_cast<double>(index) / rate;
        result[index] =
            std::polar(amplitude, static_cast<float>(phase));
    }
    return result;
}

double rms(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0;
    const size_t start = samples.size() / 3;
    double power = 0.0;
    for (size_t index = start; index < samples.size(); ++index)
        power += static_cast<double>(samples[index]) * samples[index];
    return std::sqrt(
        power / static_cast<double>(samples.size() - start));
}

} // namespace

int main() {
    constexpr double inputRate = 192000.0;
    constexpr double frequency = 14.2e6;
    double levelDb = -140.0;

    int wantedOwner = 1;
    int rejectedOwner = 2;
    const auto upper = tone(inputRate, 0.40, 1500.0, 0.15f);
    const auto lower = tone(inputRate, 0.40, -1500.0, 0.15f);

    const auto wanted = HfDemod::demodulate(
        &wantedOwner, upper, inputRate, frequency, frequency,
        DemodMode::USB, levelDb, 3000.0, -120.0, 1.0,
        6000.0, 0, 48000.0);
    const auto rejected = HfDemod::demodulate(
        &rejectedOwner, lower, inputRate, frequency, frequency,
        DemodMode::USB, levelDb, 3000.0, -120.0, 1.0,
        6000.0, 0, 48000.0);

    require(!wanted.empty(), "USB produced no audio");
    require(rms(wanted) > rms(rejected) * 20.0,
            "USB opposite-sideband rejection is below 26 dB");

    int highRateWantedOwner = 3;
    int highRateAliasOwner = 4;
    constexpr double highRate = 2.4e6;
    const auto highRateWantedIq = tone(
        highRate, 0.12, 1500.0, 0.15f);
    const auto highRateAliasIq = tone(
        highRate, 0.12, 49500.0, 0.90f);
    const auto highRateWanted = HfDemod::demodulate(
        &highRateWantedOwner, highRateWantedIq, highRate, frequency, frequency,
        DemodMode::USB, levelDb, 3000.0, -120.0, 1.0,
        6000.0, 0, 48000.0);
    const auto highRateAliased = HfDemod::demodulate(
        &highRateAliasOwner, highRateAliasIq, highRate, frequency, frequency,
        DemodMode::USB, levelDb, 3000.0, -120.0, 1.0,
        6000.0, 0, 48000.0);
    require(rms(highRateWanted) > rms(highRateAliased) * 20.0,
            "2.4 MS/s anti-alias rejection is below 26 dB");

    require(HfDemod::supports(DemodMode::AM), "AM ownership missing");
    require(HfDemod::supports(DemodMode::USB), "USB ownership missing");
    require(HfDemod::supports(DemodMode::LSB), "LSB ownership missing");
    require(HfDemod::supports(DemodMode::CW), "CW ownership missing");
    require(!HfDemod::supports(DemodMode::NFM),
            "NFM must remain on the legacy/P25-safe path");
    require(!HfDemod::supports(DemodMode::WFM),
            "WFM must remain on the legacy path");

    HfDemod::release(&wantedOwner);
    HfDemod::release(&rejectedOwner);
    HfDemod::release(&highRateWantedOwner);
    HfDemod::release(&highRateAliasOwner);
    std::cout << "HF standalone smoke passed\n";
    return 0;
}
