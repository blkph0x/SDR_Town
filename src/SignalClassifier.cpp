#include "SignalClassifier.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <array>

namespace {

double percentile(std::vector<double>& values, double p, double fallback)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
        return !std::isfinite(v);
    }), values.end());
    if (values.empty()) return fallback;
    p = std::clamp(p, 0.0, 1.0);
    const size_t idx = std::min(values.size() - 1, static_cast<size_t>(std::llround(p * (values.size() - 1))));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
    return values[idx];
}

double maxRange(const std::vector<float>& v, int lo, int hi, double fallback)
{
    if (v.empty()) return fallback;
    lo = std::clamp(lo, 0, static_cast<int>(v.size()) - 1);
    hi = std::clamp(hi, 0, static_cast<int>(v.size()) - 1);
    if (lo > hi) std::swap(lo, hi);
    double out = fallback;
    for (int i = lo; i <= hi; ++i) out = std::max(out, static_cast<double>(v[static_cast<size_t>(i)]));
    return out;
}

double meanRange(const std::vector<float>& v, int lo, int hi, double fallback)
{
    if (v.empty()) return fallback;
    lo = std::clamp(lo, 0, static_cast<int>(v.size()) - 1);
    hi = std::clamp(hi, 0, static_cast<int>(v.size()) - 1);
    if (lo > hi) std::swap(lo, hi);
    double sum = 0.0;
    int count = 0;
    for (int i = lo; i <= hi; ++i) {
        if (std::isfinite(v[static_cast<size_t>(i)])) {
            sum += v[static_cast<size_t>(i)];
            ++count;
        }
    }
    return count > 0 ? sum / count : fallback;
}

double linearPower(double db)
{
    if (!std::isfinite(db)) return 0.0;
    db = std::clamp(db, -160.0, 80.0);
    constexpr double kMinDb = -160.0;
    constexpr double kStepDb = 0.25;
    constexpr size_t kLutSize = 961; // -160.0 .. 80.0 dB inclusive.
    static const std::array<double, kLutSize> lut = [] {
        std::array<double, kLutSize> table{};
        for (size_t i = 0; i < table.size(); ++i) {
            table[i] = std::pow(10.0, (kMinDb + static_cast<double>(i) * kStepDb) / 10.0);
        }
        return table;
    }();
    const double pos = (db - kMinDb) / kStepDb;
    const size_t lo = std::min(static_cast<size_t>(pos), kLutSize - 1);
    const size_t hi = std::min(lo + 1, kLutSize - 1);
    const double frac = pos - static_cast<double>(lo);
    return lut[lo] * (1.0 - frac) + lut[hi] * frac;
}

SignalRecommendation makeStandard(SignalClass c, const SignalFeatures& f, double confidence, std::string reason)
{
    SignalRecommendation r;
    r.signalClass = c;
    r.features = f;
    r.confidence = std::clamp(confidence, 0.0, 0.99);
    r.reason = std::move(reason);
    r.estimatedBandwidthHz = std::max(1000.0, f.estimatedBandwidthHz);

    switch (c) {
        case SignalClass::WFM:
            r.label = "WFM Broadcast";
            r.demodMode = DemodMode::WFM;
            r.standardBandwidthHz = 180000.0;
            r.audioLowPassHz = 15000.0;
            r.rfFilterCutoffHz = 90000.0;
            r.filterKind = ClassifierFilterKind::WfmDeemphasisLowPass;
            break;
        case SignalClass::AM:
            r.label = "AM";
            r.demodMode = DemodMode::AM;
            r.standardBandwidthHz = 20000.0;
            r.audioLowPassHz = 9000.0;
            r.rfFilterCutoffHz = 10000.0;
            r.filterKind = ClassifierFilterKind::LowPass;
            break;
        case SignalClass::USB:
            r.label = "USB";
            r.demodMode = DemodMode::USB;
            r.standardBandwidthHz = 6000.0;
            r.audioLowPassHz = 3000.0;
            r.rfFilterCutoffHz = 3000.0;
            r.filterKind = ClassifierFilterKind::BandPass;
            break;
        case SignalClass::LSB:
            r.label = "LSB";
            r.demodMode = DemodMode::LSB;
            r.standardBandwidthHz = 6000.0;
            r.audioLowPassHz = 3000.0;
            r.rfFilterCutoffHz = 3000.0;
            r.filterKind = ClassifierFilterKind::BandPass;
            break;
        case SignalClass::CW:
            r.label = "CW";
            r.demodMode = DemodMode::CW;
            r.standardBandwidthHz = 1000.0;
            r.audioLowPassHz = 900.0;
            r.rfFilterCutoffHz = 500.0;
            r.filterKind = ClassifierFilterKind::BandPass;
            break;
        case SignalClass::P25Phase1:
            r.label = "P25 C4FM / Control";
            r.demodMode = DemodMode::NFM;
            r.standardBandwidthHz = 12500.0;
            r.audioLowPassHz = 6000.0;
            r.rfFilterCutoffHz = 6250.0;
            r.filterKind = ClassifierFilterKind::RootRaisedCosine;
            r.digital = true;
            r.disableAudioLpf = true;
            break;
        case SignalClass::DigitalFSK:
            r.label = "Digital FSK";
            r.demodMode = DemodMode::NFM;
            r.standardBandwidthHz = std::clamp(r.estimatedBandwidthHz, 6250.0, 25000.0);
            r.audioLowPassHz = std::clamp(r.standardBandwidthHz * 0.45, 3000.0, 12000.0);
            r.rfFilterCutoffHz = r.standardBandwidthHz * 0.5;
            r.filterKind = ClassifierFilterKind::LowPass;
            r.digital = true;
            r.disableAudioLpf = true;
            break;
        case SignalClass::NFM:
            r.label = "NFM";
            r.demodMode = DemodMode::NFM;
            r.standardBandwidthHz = r.estimatedBandwidthHz <= 18000.0 ? 12500.0 : 25000.0;
            r.audioLowPassHz = r.standardBandwidthHz <= 12500.0 ? 3000.0 : 4500.0;
            r.rfFilterCutoffHz = r.standardBandwidthHz * 0.5;
            r.filterKind = ClassifierFilterKind::LowPass;
            break;
        case SignalClass::Unknown:
        default:
            r.label = "Unknown";
            r.demodMode = DemodMode::NFM;
            r.standardBandwidthHz = std::clamp(r.estimatedBandwidthHz, 2500.0, 250000.0);
            r.audioLowPassHz = std::clamp(r.standardBandwidthHz * 0.45, 2500.0, 15000.0);
            r.rfFilterCutoffHz = r.standardBandwidthHz * 0.5;
            r.filterKind = ClassifierFilterKind::LowPass;
            break;
    }
    return r;
}

} // namespace

WaterfallRoiBuilder::WaterfallRoiBuilder(size_t maxFrames_) : maxFrames(std::max<size_t>(4, maxFrames_)) {}

void WaterfallRoiBuilder::pushSpectrum(const std::vector<float>& powerDb)
{
    if (powerDb.empty()) return;
    frames.push_back(powerDb);
    while (frames.size() > maxFrames) frames.pop_front();
}

void WaterfallRoiBuilder::clear()
{
    frames.clear();
}

ClassifierTile WaterfallRoiBuilder::buildTile(double sampleRateHz,
                                              double centerFreqHz,
                                              double targetFreqHz,
                                              double roiBandwidthHz,
                                              size_t width,
                                              size_t height) const
{
    ClassifierTile tile;
    if (frames.empty() || sampleRateHz <= 0.0 || width == 0 || height == 0) return tile;
    const auto& first = frames.front();
    if (first.empty()) return tile;

    tile.width = width;
    tile.height = height;
    tile.pixels.assign(width * height, 0.0f);

    const double binHz = sampleRateHz / static_cast<double>(first.size());
    const double fullStart = centerFreqHz - sampleRateHz / 2.0;
    const double roiHz = std::clamp(roiBandwidthHz, binHz * 4.0, sampleRateHz);
    const double roiStart = targetFreqHz - roiHz * 0.5;

    std::vector<double> samples;
    samples.reserve(width * std::min(height, frames.size()));

    for (size_t y = 0; y < height; ++y) {
        const double framePos = (height <= 1 || frames.size() == 1)
            ? static_cast<double>(frames.size() - 1)
            : (static_cast<double>(y) / static_cast<double>(height - 1)) * static_cast<double>(frames.size() - 1);
        const size_t fi = std::min(frames.size() - 1, static_cast<size_t>(std::llround(framePos)));
        const auto& row = frames[fi];
        if (row.empty()) continue;
        for (size_t x = 0; x < width; ++x) {
            const double hz = roiStart + (static_cast<double>(x) + 0.5) * roiHz / static_cast<double>(width);
            const double bin = (hz - fullStart) / binHz - 0.5;
            const int bi = std::clamp(static_cast<int>(std::llround(bin)), 0, static_cast<int>(row.size()) - 1);
            const double db = row[static_cast<size_t>(bi)];
            tile.pixels[y * width + x] = static_cast<float>(db);
            samples.push_back(db);
        }
    }

    tile.minDb = percentile(samples, 0.05, -120.0);
    tile.maxDb = percentile(samples, 0.98, -20.0);
    if (tile.maxDb <= tile.minDb + 1.0) tile.maxDb = tile.minDb + 1.0;
    for (float& px : tile.pixels) {
        px = static_cast<float>(std::clamp((static_cast<double>(px) - tile.minDb) / (tile.maxDb - tile.minDb), 0.0, 1.0));
    }
    return tile;
}

AdvancedSignalClassifier& AdvancedSignalClassifier::instance()
{
    static AdvancedSignalClassifier cls;
    return cls;
}

SignalFeatures AdvancedSignalClassifier::extractSpectrumFeatures(const std::vector<float>& powerDb,
                                                                 double sampleRateHz,
                                                                 double centerFreqHz,
                                                                 double targetFreqHz,
                                                                 double maxSearchHz) const
{
    SignalFeatures f;
    if (powerDb.size() < 16 || sampleRateHz <= 0.0 || !std::isfinite(sampleRateHz)) return f;

    const int bins = static_cast<int>(powerDb.size());
    const double binHz = sampleRateHz / static_cast<double>(bins);
    const double fullStart = centerFreqHz - sampleRateHz / 2.0;
    const double rel = (targetFreqHz - fullStart) / binHz;
    if (!std::isfinite(rel) || rel < -2.0 || rel > bins + 2.0) return f;

    const int targetBin = std::clamp(static_cast<int>(std::llround(rel - 0.5)), 0, bins - 1);
    const int halfSearch = std::max(8, static_cast<int>(std::ceil(std::clamp(maxSearchHz, 5000.0, sampleRateHz * 0.5) / binHz)));
    const int lo = std::max(0, targetBin - halfSearch);
    const int hi = std::min(bins - 1, targetBin + halfSearch);

    std::vector<double> local;
    local.reserve(static_cast<size_t>(hi - lo + 1));
    int peakIdx = targetBin;
    double peak = powerDb[static_cast<size_t>(targetBin)];
    for (int i = lo; i <= hi; ++i) {
        const double v = powerDb[static_cast<size_t>(i)];
        local.push_back(v);
        if (v > peak) {
            peak = v;
            peakIdx = i;
        }
    }

    const double floorDb = percentile(local, 0.20, -120.0);
    const double snrDb = peak - floorDb;
    if (snrDb < 2.0) {
        f.valid = true;
        f.noiseFloorDb = floorDb;
        f.peakDb = peak;
        f.snrDb = snrDb;
        f.estimatedBandwidthHz = 2500.0;
        return f;
    }

    const double threshold = std::max(floorDb + 3.0, peak - std::clamp(snrDb * 0.65, 8.0, 22.0));
    const int quietRunNeeded = std::max(1, static_cast<int>(std::ceil(1200.0 / binHz)));

    int left = peakIdx;
    int quiet = 0;
    while (left > lo) {
        --left;
        if (powerDb[static_cast<size_t>(left)] > threshold) quiet = 0;
        else if (++quiet >= quietRunNeeded) {
            left += quietRunNeeded;
            break;
        }
    }
    int right = peakIdx;
    quiet = 0;
    while (right < hi) {
        ++right;
        if (powerDb[static_cast<size_t>(right)] > threshold) quiet = 0;
        else if (++quiet >= quietRunNeeded) {
            right -= quietRunNeeded;
            break;
        }
    }
    left = std::clamp(left, lo, hi);
    right = std::clamp(right, lo, hi);
    if (left > right) std::swap(left, right);

    const double occupiedHz = std::max(binHz, (right - left + 1) * binHz);
    const double estimatedHz = std::clamp(occupiedHz * 1.18, binHz, sampleRateHz);

    const int carrierBins = std::max(1, static_cast<int>(std::ceil(900.0 / binHz)));
    const double carrierDb = maxRange(powerDb, targetBin - carrierBins, targetBin + carrierBins, floorDb);
    const int sideHalf = std::max(2, static_cast<int>(std::ceil(std::clamp(estimatedHz * 0.10, 1500.0, 6000.0) / binHz)));
    const int sideOffset = std::max(carrierBins + sideHalf + 1, static_cast<int>(std::ceil(std::clamp(estimatedHz * 0.33, 3000.0, 12000.0) / binHz)));
    const double leftDb = maxRange(powerDb, targetBin - sideOffset - sideHalf, targetBin - sideOffset + sideHalf, floorDb);
    const double rightDb = maxRange(powerDb, targetBin + sideOffset - sideHalf, targetBin + sideOffset + sideHalf, floorDb);

    double arithmetic = 0.0;
    double geometricLog = 0.0;
    int signalBins = 0;
    for (int i = left; i <= right; ++i) {
        const double relDb = std::clamp(static_cast<double>(powerDb[static_cast<size_t>(i)]) - floorDb, -60.0, 80.0);
        const double p = std::max(1e-12, linearPower(relDb));
        arithmetic += p;
        geometricLog += std::log(p);
        ++signalBins;
    }
    double flatness = 0.0;
    if (signalBins > 0 && arithmetic > 0.0) {
        arithmetic /= signalBins;
        flatness = std::clamp(std::exp(geometricLog / signalBins) / arithmetic, 0.0, 1.0);
    }

    double symNum = 0.0;
    double symDen = 0.0;
    const int symBins = std::min(targetBin - left, right - targetBin);
    for (int k = 1; k <= symBins; ++k) {
        const double a = powerDb[static_cast<size_t>(targetBin - k)] - floorDb;
        const double b = powerDb[static_cast<size_t>(targetBin + k)] - floorDb;
        symNum += std::max(0.0, 1.0 - std::abs(a - b) / std::max(12.0, std::max(std::abs(a), std::abs(b))));
        symDen += 1.0;
    }

    f.valid = true;
    f.estimatedBandwidthHz = estimatedHz;
    f.occupiedBandwidthHz = occupiedHz;
    f.snrDb = snrDb;
    f.noiseFloorDb = floorDb;
    f.peakDb = peak;
    f.centerOffsetHz = (peakIdx - targetBin) * binHz;
    f.carrierDominanceDb = carrierDb - std::max(leftDb, rightDb);
    f.sidebandBalanceDb = std::abs(leftDb - rightDb);
    f.sidebandAsymmetryDb = rightDb - leftDb;
    f.symmetry = symDen > 0.0 ? std::clamp(symNum / symDen, 0.0, 1.0) : 0.0;
    f.spectralFlatness = flatness;
    return f;
}

SignalRecommendation AdvancedSignalClassifier::recommendFromFeatures(const SignalFeatures& f,
                                                                     double targetFreqHz) const
{
    if (!f.valid || f.snrDb < 3.0) {
        return makeStandard(SignalClass::Unknown, f, 0.15, "weak/no-signal fallback");
    }

    if (targetFreqHz >= 87.5e6 && targetFreqHz <= 108.0e6 && f.estimatedBandwidthHz >= 90000.0) {
        return makeStandard(SignalClass::WFM, f, 0.92, "FM broadcast band plus wide occupied bandwidth");
    }

    if (f.estimatedBandwidthHz > 80000.0) {
        return makeStandard(SignalClass::WFM, f, 0.84, "wideband FM-like occupied bandwidth");
    }

    const bool amShape =
        f.estimatedBandwidthHz >= 7000.0 &&
        f.estimatedBandwidthHz <= 32000.0 &&
        f.carrierDominanceDb >= 3.0 &&
        f.sidebandBalanceDb <= 8.0 &&
        f.symmetry >= 0.45 &&
        f.spectralFlatness < 0.55;
    if (amShape || (targetFreqHz >= 108.0e6 && targetFreqHz <= 137.0e6 && f.estimatedBandwidthHz <= 32000.0)) {
        return makeStandard(SignalClass::AM, f, amShape ? 0.86 : 0.72, amShape ? "carrier with balanced AM sidebands" : "airband AM frequency plan");
    }

    const bool p25Like =
        f.estimatedBandwidthHz >= 9000.0 &&
        f.estimatedBandwidthHz <= 17000.0 &&
        f.spectralFlatness >= 0.48 &&
        f.carrierDominanceDb < 6.0 &&
        f.snrDb >= 8.0;
    if (p25Like) {
        return makeStandard(SignalClass::P25Phase1, f, 0.78, "flat 12.5 kHz C4FM/P25-like spectral plateau");
    }

    const bool fskLike =
        f.estimatedBandwidthHz >= 4500.0 &&
        f.estimatedBandwidthHz <= 30000.0 &&
        f.spectralFlatness >= 0.42 &&
        f.carrierDominanceDb < 8.0;
    if (fskLike) {
        return makeStandard(SignalClass::DigitalFSK, f, 0.62, "flat digital FSK-like occupied bandwidth");
    }

    if (f.estimatedBandwidthHz <= 1200.0 && f.carrierDominanceDb >= 8.0) {
        return makeStandard(SignalClass::CW, f, 0.74, "very narrow carrier-like signal");
    }

    if (f.estimatedBandwidthHz <= 5000.0) {
        if (f.sidebandAsymmetryDb >= 3.0) {
            return makeStandard(SignalClass::USB, f, 0.66, "narrow asymmetric upper-sideband energy");
        }
        if (f.sidebandAsymmetryDb <= -3.0) {
            return makeStandard(SignalClass::LSB, f, 0.66, "narrow asymmetric lower-sideband energy");
        }
        return makeStandard(SignalClass::USB, f, 0.52, "narrow voice/CW-width fallback");
    }

    return makeStandard(SignalClass::NFM, f, 0.68, "narrow FM voice-width fallback");
}

SignalRecommendation AdvancedSignalClassifier::classifySpectrum(const std::vector<float>& powerDb,
                                                                double sampleRateHz,
                                                                double centerFreqHz,
                                                                double targetFreqHz,
                                                                double maxSearchHz) const
{
    auto f = extractSpectrumFeatures(powerDb, sampleRateHz, centerFreqHz, targetFreqHz, maxSearchHz);
    return recommendFromFeatures(f, targetFreqHz);
}

SignalRecommendation AdvancedSignalClassifier::classifyWaterfallTile(const ClassifierTile& tile,
                                                                     double sampleRateHz,
                                                                     double centerFreqHz,
                                                                     double targetFreqHz,
                                                                     double roiBandwidthHz) const
{
    if (!tile.valid() || tile.width < 8 || tile.height < 2) {
        SignalFeatures f;
        return makeStandard(SignalClass::Unknown, f, 0.10, "invalid waterfall tile");
    }

    std::vector<float> avg(tile.width, 0.0f);
    std::vector<double> rowPeaks;
    rowPeaks.reserve(tile.height);
    for (size_t y = 0; y < tile.height; ++y) {
        float rowPeak = 0.0f;
        for (size_t x = 0; x < tile.width; ++x) {
            const float v = tile.pixels[y * tile.width + x];
            avg[x] += v;
            rowPeak = std::max(rowPeak, v);
        }
        rowPeaks.push_back(rowPeak);
    }
    for (float& v : avg) v /= static_cast<float>(tile.height);

    // Reconstruct a relative-dB row from the normalized tile. This lets the same
    // spectrum feature extractor serve both FFT-only and waterfall-ROI paths.
    std::vector<float> pseudo(tile.width);
    const double dbSpan = std::max(1.0, tile.maxDb - tile.minDb);
    for (size_t i = 0; i < tile.width; ++i) {
        pseudo[i] = static_cast<float>(tile.minDb + avg[i] * dbSpan);
    }

    auto f = extractSpectrumFeatures(pseudo, roiBandwidthHz, targetFreqHz, targetFreqHz, roiBandwidthHz * 0.5);
    const double peakMean = std::accumulate(rowPeaks.begin(), rowPeaks.end(), 0.0) / std::max<size_t>(1, rowPeaks.size());
    double var = 0.0;
    double duty = 0.0;
    for (double p : rowPeaks) {
        var += (p - peakMean) * (p - peakMean);
        if (p > 0.30) duty += 1.0;
    }
    var /= std::max<size_t>(1, rowPeaks.size());
    f.temporalStability = std::clamp(1.0 - std::sqrt(var) * 2.0, 0.0, 1.0);
    f.dutyCycle = duty / std::max<size_t>(1, rowPeaks.size());

    auto r = recommendFromFeatures(f, targetFreqHz);
    if (r.digital && f.temporalStability > 0.65 && f.dutyCycle > 0.60) {
        r.confidence = std::min(0.95, r.confidence + 0.08);
        r.reason += "; stable waterfall ROI";
    } else if (f.dutyCycle < 0.20) {
        r.confidence *= 0.75;
        r.reason += "; low duty-cycle waterfall ROI";
    }
    return r;
}

std::string signalClassToString(SignalClass c)
{
    switch (c) {
        case SignalClass::WFM: return "WFM";
        case SignalClass::NFM: return "NFM";
        case SignalClass::AM: return "AM";
        case SignalClass::USB: return "USB";
        case SignalClass::LSB: return "LSB";
        case SignalClass::CW: return "CW";
        case SignalClass::P25Phase1: return "P25 C4FM";
        case SignalClass::DigitalFSK: return "Digital FSK";
        case SignalClass::Unknown:
        default: return "Unknown";
    }
}

std::string classifierFilterKindToString(ClassifierFilterKind k)
{
    switch (k) {
        case ClassifierFilterKind::None: return "None";
        case ClassifierFilterKind::LowPass: return "Low-pass";
        case ClassifierFilterKind::BandPass: return "Band-pass";
        case ClassifierFilterKind::RootRaisedCosine: return "Root-raised cosine";
        case ClassifierFilterKind::WfmDeemphasisLowPass: return "WFM de-emphasis + LPF";
        default: return "Unknown";
    }
}
