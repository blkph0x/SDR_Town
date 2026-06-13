#pragma once

#include "Demod.h"

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

enum class SignalClass {
    Unknown,
    WFM,
    NFM,
    AM,
    USB,
    LSB,
    CW,
    P25Phase1,
    DigitalFSK
};

enum class ClassifierFilterKind {
    None,
    LowPass,
    BandPass,
    RootRaisedCosine,
    WfmDeemphasisLowPass
};

struct ClassifierTile {
    size_t width = 0;
    size_t height = 0;
    std::vector<float> pixels; // row-major, normalized 0..1 waterfall ROI
    double minDb = -120.0;
    double maxDb = -20.0;
    bool valid() const { return width > 0 && height > 0 && pixels.size() == width * height; }
};

struct SignalFeatures {
    bool valid = false;
    double estimatedBandwidthHz = 0.0;
    double occupiedBandwidthHz = 0.0;
    double snrDb = 0.0;
    double noiseFloorDb = -120.0;
    double peakDb = -120.0;
    double centerOffsetHz = 0.0;
    double carrierDominanceDb = 0.0;
    double sidebandBalanceDb = 0.0;
    double sidebandAsymmetryDb = 0.0;
    double symmetry = 0.0;          // 0..1
    double spectralFlatness = 0.0;  // 0..1, high for digital/noise-like plateaus
    double dutyCycle = 1.0;         // 0..1 over waterfall rows when available
    double temporalStability = 0.0; // 0..1
};

struct SignalRecommendation {
    SignalClass signalClass = SignalClass::Unknown;
    DemodMode demodMode = DemodMode::NFM;
    ClassifierFilterKind filterKind = ClassifierFilterKind::LowPass;
    std::string label = "Unknown";
    std::string reason;
    double confidence = 0.0;
    double estimatedBandwidthHz = 12500.0;
    double standardBandwidthHz = 12500.0;
    double audioLowPassHz = 3000.0;
    double rfFilterCutoffHz = 6250.0;
    bool digital = false;
    bool disableAudioLpf = false;
    SignalFeatures features;
};

class WaterfallRoiBuilder {
public:
    explicit WaterfallRoiBuilder(size_t maxFrames = 128);

    void pushSpectrum(const std::vector<float>& powerDb);
    void clear();

    ClassifierTile buildTile(double sampleRateHz,
                             double centerFreqHz,
                             double targetFreqHz,
                             double roiBandwidthHz,
                             size_t width = 256,
                             size_t height = 256) const;

private:
    size_t maxFrames = 128;
    std::deque<std::vector<float>> frames;
};

class AdvancedSignalClassifier {
public:
    static AdvancedSignalClassifier& instance();

    SignalRecommendation classifySpectrum(const std::vector<float>& powerDb,
                                          double sampleRateHz,
                                          double centerFreqHz,
                                          double targetFreqHz,
                                          double maxSearchHz = 300000.0) const;

    SignalRecommendation classifyWaterfallTile(const ClassifierTile& tile,
                                               double sampleRateHz,
                                               double centerFreqHz,
                                               double targetFreqHz,
                                               double roiBandwidthHz) const;

private:
    AdvancedSignalClassifier() = default;

    SignalFeatures extractSpectrumFeatures(const std::vector<float>& powerDb,
                                           double sampleRateHz,
                                           double centerFreqHz,
                                           double targetFreqHz,
                                           double maxSearchHz) const;

    SignalRecommendation recommendFromFeatures(const SignalFeatures& f,
                                               double targetFreqHz) const;
};

std::string signalClassToString(SignalClass c);
std::string classifierFilterKindToString(ClassifierFilterKind k);
