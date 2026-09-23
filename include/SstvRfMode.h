#pragma once

#include "Demod.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace SstvRfMode {

struct Selection {
    DemodMode mode = DemodMode::NFM;
    double confidence = 0.0;
    std::string reason;
};

inline bool supported(DemodMode mode) noexcept {
    return mode == DemodMode::NFM || mode == DemodMode::USB || mode == DemodMode::LSB;
}

inline DemodMode parseExplicit(const std::string& requested) noexcept {
    std::string value = requested;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "nfm") return DemodMode::NFM;
    if (value == "usb") return DemodMode::USB;
    if (value == "lsb") return DemodMode::LSB;
    return DemodMode::AUTO;
}

inline Selection select(const std::string& requested,
                        DemodMode currentMode,
                        double targetHz,
                        const std::vector<float>& spectrumDb = {},
                        double spectrumCenterHz = 0.0,
                        double spectrumRateHz = 0.0)
{
    const DemodMode explicitMode = parseExplicit(requested);
    if (supported(explicitMode)) return {explicitMode, 1.0, "explicit"};

    // Auto uses measured sideband energy first. This is important when the
    // receiver is still on its default NFM mode but the tuned HF SSTV signal is
    // actually USB or LSB.
    if (!spectrumDb.empty() && spectrumRateHz > 0.0 &&
        std::isfinite(spectrumCenterHz) && std::isfinite(targetHz)) {
        const double binHz = spectrumRateHz / static_cast<double>(spectrumDb.size());
        const double startHz = spectrumCenterHz - spectrumRateHz * 0.5;
        double upperPower = 0.0, lowerPower = 0.0;
        size_t upperBins = 0, lowerBins = 0;

        for (size_t i = 0; i < spectrumDb.size(); ++i) {
            const double hz = startHz + (static_cast<double>(i) + 0.5) * binHz;
            const double offset = hz - targetHz;
            const double absolute = std::abs(offset);
            if (absolute < 300.0 || absolute > 3200.0) continue;
            const double db = std::clamp(static_cast<double>(spectrumDb[i]), -180.0, 60.0);
            const double power = std::pow(10.0, db / 10.0);
            if (offset > 0.0) {
                upperPower += power;
                ++upperBins;
            } else {
                lowerPower += power;
                ++lowerBins;
            }
        }

        if (upperBins > 2 && lowerBins > 2 && upperPower > 0.0 && lowerPower > 0.0) {
            const double upperDb = 10.0 * std::log10(upperPower / static_cast<double>(upperBins));
            const double lowerDb = 10.0 * std::log10(lowerPower / static_cast<double>(lowerBins));
            const double deltaDb = upperDb - lowerDb;
            const double absDelta = std::abs(deltaDb);

            // A clear asymmetric SSTV/audio spectrum is sideband evidence.
            if (absDelta >= 3.0) {
                const double confidence = std::clamp(0.55 + (absDelta - 3.0) / 14.0, 0.55, 0.98);
                return {deltaDb > 0.0 ? DemodMode::USB : DemodMode::LSB,
                        confidence,
                        deltaDb > 0.0 ? "upper-sideband spectral energy" : "lower-sideband spectral energy"};
            }

            // Above HF, a roughly symmetric narrow voice/data channel is much
            // more likely to be FM SSTV than SSB. Do not use this rule on HF,
            // where AM carriers and direct-sampling images can also be symmetric.
            if (targetHz >= 30.0e6 && absDelta < 3.0)
                return {DemodMode::NFM, 0.72, "symmetric VHF/UHF channel"};
        }
    }

    // A selected sideband is useful prior knowledge on HF. Do not let a
    // stale/default NFM receiver mode override HF Auto when spectrum evidence
    // is weak; NFM is a sensible fallback only above the HF range.
    if (currentMode == DemodMode::USB || currentMode == DemodMode::LSB)
        return {currentMode, 0.82, "current sideband fallback"};
    if (currentMode == DemodMode::NFM && targetHz >= 30.0e6)
        return {currentMode, 0.82, "current NFM fallback"};

    // Evidence-free fallback follows common amateur SSB convention. It is
    // deliberately reported as a fallback so the UI never presents it as a
    // measured classification.
    if (targetHz >= 30.0e6) return {DemodMode::NFM, 0.45, "VHF/UHF fallback"};
    if (targetHz > 0.0 && targetHz < 10.0e6) return {DemodMode::LSB, 0.40, "HF band-plan fallback"};
    return {DemodMode::USB, 0.40, "HF band-plan fallback"};
}

inline const char* name(DemodMode mode) noexcept {
    switch (mode) {
        case DemodMode::NFM: return "NFM";
        case DemodMode::USB: return "USB";
        case DemodMode::LSB: return "LSB";
        default: return "AUTO";
    }
}

} // namespace SstvRfMode
