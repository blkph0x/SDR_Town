#pragma once

#include "SignalClassifier.h"

#include <optional>
#include <string>

struct ClassifierModelStatus {
    bool enabled = false;
    bool loaded = false;
    std::string modelPath;
    std::string backendName = "deterministic";
    std::string message = "Deterministic classifier active.";
};

class ClassifierModelBackend {
public:
    static ClassifierModelBackend& instance();

    bool loadModel(const std::string& path);
    void unloadModel();
    ClassifierModelStatus status() const;

    std::optional<SignalRecommendation> classifyTile(const ClassifierTile& tile,
                                                     double sampleRateHz,
                                                     double centerFreqHz,
                                                     double targetFreqHz,
                                                     double roiBandwidthHz) const;

private:
    ClassifierModelBackend() = default;

    ClassifierModelStatus state;
};
